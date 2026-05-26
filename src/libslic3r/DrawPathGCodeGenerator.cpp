#include "DrawPathGCodeGenerator.hpp"

#include "DrawModeFeedback.hpp"
#include "ExtrusionEntity.hpp"
#include "GCode/GCodeProcessor.hpp"
#include "Model.hpp"
#include "PlaceholderParser.hpp"

#include <cmath>
#include <algorithm>
#include <limits>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Slic3r {

namespace {

std::string gcode_processor_role_tag(ExtrusionRole role)
{
    return ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role) + ExtrusionEntity::role_to_string(role) + "\n";
}

} // namespace

DrawPathGCodeGenerator::DrawPathGCodeGenerator(const DynamicPrintConfig& full_config,
                                               const Vec2d&              plate_origin)
    : m_config(full_config)
    , m_plate_origin(plate_origin)
{
    // Apply merged config keys into GCodeWriter's internal GCodeConfig.
    // ignore_nonexistent = true so that process/filament keys (not in GCodeConfig)
    // are silently skipped.
    m_writer.config.apply(m_config, /*ignore_nonexistent=*/true);

    // Register extruder 0 after config is set (Extruder ctor reads GCodeConfig).
    m_writer.set_extruders({0});
    // Select extruder 0 as the active tool so filament() returns non-null.
    // set_extruders() adds extruders but leaves m_curr_extruder_id = -1.
    // toolchange() sets m_curr_extruder_id and m_curr_filament_extruder[0].
    m_writer.toolchange(0);

    // Initialize placeholder parser with the full config.
    m_placeholder_parser.apply_config(m_config);
    m_placeholder_parser.update_timestamp();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string DrawPathGCodeGenerator::generate(const DrawSession& session,
                                              const Vec2d&       instance_offset)
{
    if (session.is_empty())
        return generate_preamble(session) + generate_postamble(session);

    // Total XY offset to convert plate-relative → absolute machine coords.
    const Vec2d abs_offset = m_plate_origin + instance_offset;

    std::string gcode;
    gcode.reserve(1024 * 64);

    gcode += generate_preamble(session);

    for (const DrawLayer& layer : session.layers) {
        gcode += generate_layer(layer, abs_offset);
    }

    gcode += generate_postamble(session);
    return gcode;
}

std::string DrawPathGCodeGenerator::generate_batch(const std::vector<BatchItem>& items)
{
    if (items.empty())
        return "";

    // Find the first non-empty session for preamble initialization.
    const DrawSession* first_nonempty = nullptr;
    for (const auto& [sess, _] : items)
        if (sess && !sess->is_empty()) { first_nonempty = sess; break; }

    std::string gcode;
    gcode.reserve(1024 * 128);

    if (!first_nonempty) {
        // All empty — still emit preamble + postamble.
        DrawSession empty;
        gcode += generate_preamble(empty);
        gcode += generate_postamble(empty);
        return gcode;
    }

    gcode += generate_preamble(*first_nonempty);

    for (const auto& [sess, inst_offset] : items) {
        if (!sess || sess->is_empty()) continue;
        const Vec2d abs_offset = m_plate_origin + inst_offset;
        for (const DrawLayer& layer : sess->layers) {
            gcode += generate_layer(layer, abs_offset);
        }
    }

    gcode += generate_postamble(*first_nonempty);
    return gcode;
}

bool DrawPathGCodeGenerator::is_draw_path_object(const ModelObject* object)
{
    return object != nullptr && object->is_draw_path_object();
}

bool DrawPathGCodeGenerator::contains_only_draw_path_objects(const std::vector<ModelObject*>& objects)
{
    return !objects.empty() &&
        std::all_of(objects.begin(), objects.end(), &DrawPathGCodeGenerator::is_draw_path_object);
}

std::vector<DrawPathGCodeGenerator::BatchItem>
DrawPathGCodeGenerator::collect_batch(const std::vector<ModelObject*>& objects)
{
    std::vector<BatchItem> batch;
    for (const ModelObject* object : objects) {
        if (!is_draw_path_object(object) || !object->draw_session)
            continue;

        for (const ModelInstance* instance : object->instances) {
            if (!instance)
                continue;
            const Vec3d offset = instance->get_offset();
            batch.emplace_back(object->draw_session.get(), Vec2d(offset.x(), offset.y()));
        }
    }
    return batch;
}

bool DrawPathGCodeGenerator::can_generate_for_objects(const std::vector<ModelObject*>& objects)
{
    return contains_only_draw_path_objects(objects) && !collect_batch(objects).empty();
}

std::string DrawPathGCodeGenerator::parent_path_for_file(const std::string& path)
{
    return boost::filesystem::path(path).parent_path().string();
}

bool DrawPathGCodeGenerator::ensure_output_directory(const std::string& path,
                                                     std::string*       error_message)
{
    const boost::filesystem::path parent = boost::filesystem::path(path).parent_path();
    if (parent.empty()) {
        if (error_message)
            error_message->clear();
        return true;
    }

    boost::system::error_code ec;
    if (boost::filesystem::exists(parent, ec)) {
        if (ec) {
            if (error_message)
                *error_message = "Cannot check output directory '" + parent.string() + "': " + ec.message();
            return false;
        }
        if (!boost::filesystem::is_directory(parent, ec) || ec) {
            if (error_message)
                *error_message = "Output parent path is not a directory: " + parent.string();
            return false;
        }
        if (error_message)
            error_message->clear();
        return true;
    }

    ec.clear();
    if (!boost::filesystem::create_directories(parent, ec) && ec) {
        if (error_message)
            *error_message = "Cannot create output directory '" + parent.string() + "': " + ec.message();
        return false;
    }

    if (error_message)
        error_message->clear();
    return true;
}

bool DrawPathGCodeGenerator::write_gcode_file(const std::string& path,
                                               const std::string& gcode,
                                               std::string*       error_message)
{
    if (!ensure_output_directory(path, error_message))
        return false;

    boost::nowide::ofstream out(path, std::ios::binary);
    if (!out) {
        if (error_message)
            *error_message = "Cannot open output file for writing.";
        return false;
    }

    out.write(gcode.data(), static_cast<std::streamsize>(gcode.size()));
    if (!out) {
        if (error_message)
            *error_message = "Failed while writing G-code.";
        return false;
    }

    out.close();
    if (!out) {
        if (error_message)
            *error_message = "Failed to close G-code file after writing.";
        return false;
    }

    if (error_message)
        error_message->clear();
    return true;
}

// ---------------------------------------------------------------------------
// Private: preamble
// ---------------------------------------------------------------------------

std::string DrawPathGCodeGenerator::generate_preamble(const DrawSession& session)
{
    std::string out;

    prepare_placeholder_parser(session, 0, 0.0);

    out += gcode_processor_role_tag(erCustom);

    // Process file_start_gcode first, matching the normal slicer ordering.
    out += process_config_gcode_string("file_start_gcode", 0);

    // Standard G-code preamble (G90, G21, M82/M83, reset E).
    out += m_writer.preamble();
    out += m_writer.reset_e(true);

    const auto* machine_start_gcode = dynamic_cast<const ConfigOptionString*>(m_config.option("machine_start_gcode"));
    const bool has_machine_start_gcode = machine_start_gcode && machine_start_gcode->value.find_first_not_of(" \t\r\n") != std::string::npos;

    // Profile start G-code owns the printer-specific startup heat cycle.  Only
    // emit generic waits as a fallback for simple/minimal configs without a
    // machine start template.
    if (!has_machine_start_gcode) {
        // Nozzle warm-up — use first-layer temperature and wait (M109).
        const int first_layer_temp = cfg_int_vec("nozzle_temperature_initial_layer");
        if (first_layer_temp > 0)
            out += m_writer.set_temperature(static_cast<unsigned int>(first_layer_temp), /*wait=*/true, /*tool=*/0);

        // Bed heat-up (M190). We use hot_plate_temp as a reasonable default.
        const int bed_temp = cfg_int_vec("hot_plate_temp");
        if (bed_temp > 0)
            out += m_writer.set_bed_temperature(bed_temp, /*wait=*/true);
    }

    // Process profile start templates with placeholders expanded.  Do not append
    // raw templates: production printer profiles often contain conditionals and
    // computed expressions (for example Bambu-style max_layer_z guards).
    out += process_config_gcode_string("machine_start_gcode", 0);
    DynamicConfig filament_config;
    filament_config.set_key_value("filament_extruder_id", new ConfigOptionInt(0));
    filament_config.set_key_value("layer_num", new ConfigOptionInt(0));
    out += process_config_gcode_strings("filament_start_gcode", 0, &filament_config);

    // Move to the first layer's print height before beginning.
    // Use z_end (the actual print height), NOT z_start which is 0 for the first layer.
    if (!session.layers.empty()) {
        out += m_writer.travel_to_z(session.layers.front().z_end, "initial Z");
    }

    out += gcode_processor_role_tag(erExternalPerimeter);

    return out;
}

// ---------------------------------------------------------------------------
// Private: per-layer
// ---------------------------------------------------------------------------

std::string DrawPathGCodeGenerator::generate_layer(const DrawLayer& layer,
                                                    const Vec2d&     abs_offset)
{
    std::string out;

    // Layer progress marker for G-code viewer / progress tracking.
    out += ";LAYER:" + std::to_string(layer.layer_index) + "\n";

    // Per-layer fan speed (ramp from 0 to max over the configured layer range).
    out += m_writer.set_fan(static_cast<unsigned int>(calc_fan_speed_pct(layer.layer_index)));

    const bool is_first = (layer.layer_index == 0);

    // Temperature for this layer.
    const int temp = is_first
        ? cfg_int_vec("nozzle_temperature_initial_layer")
        : cfg_int_vec("nozzle_temperature");
    if (temp > 0)
        out += m_writer.set_temperature(static_cast<unsigned int>(temp), /*wait=*/false, /*tool=*/0);

    // Speed for this layer (convert mm/s → mm/min for G-code F parameter).
    const double speed_mms = is_first
        ? cfg_float("initial_layer_speed")
        : cfg_float("outer_wall_speed");
    const double speed_mmmin = speed_mms * 60.0;
    if (speed_mmmin > 0.0)
        out += m_writer.set_speed(speed_mmmin, "draw layer speed");

    // All segments in a layer print at the same Z height (z_end).
    const double layer_z = layer.z_end;

    // Read arc output mode once per layer.
    const bool native_arc_mode = cfg_int("draw_path_arc_output") != 0;

    for (const DrawSegment& seg : layer.segments) {
        const Vec2d abs_start = seg.start + abs_offset;
        const Vec2d abs_end   = seg.end   + abs_offset;

        // Verify head is at abs_start; if not, emit a positioning travel first.
        const Vec3d& cur = m_writer.get_position();
        const bool need_reposition =
            std::abs(cur.x() - abs_start.x()) > 0.001 ||
            std::abs(cur.y() - abs_start.y()) > 0.001 ||
            std::abs(cur.z() - layer_z)       > 0.001;

        if (seg.is_travel) {
            // Travel segment: retract, visit sampled waypoints without extruding, unretract.
            out += m_writer.retract();
            if (need_reposition)
                out += m_writer.travel_to_xyz(Vec3d(abs_start.x(), abs_start.y(), layer_z), "to travel-src");

            if (seg.type == DrawSegmentType::Line) {
                // Simple travel to end.
                out += m_writer.travel_to_xyz(Vec3d(abs_end.x(), abs_end.y(), layer_z), "travel");
            } else {
                // Sample arc/bezier and travel through each waypoint.
                const std::vector<Vec2d> pts = draw_sample_segment(seg);
                for (size_t i = 1; i < pts.size(); ++i) {
                    const Vec2d abs_pt = pts[i] + abs_offset;
                    out += m_writer.travel_to_xyz(Vec3d(abs_pt.x(), abs_pt.y(), layer_z), "travel");
                }
            }
            out += m_writer.unretract();

        } else {
            // Extrusion segment: position head at abs_start if needed, then extrude.
            if (need_reposition) {
                out += m_writer.retract();
                out += m_writer.travel_to_xyz(Vec3d(abs_start.x(), abs_start.y(), layer_z), "to seg start");
                out += m_writer.unretract();
            }

            if (seg.type == DrawSegmentType::Line) {
                // Standard G1 extrusion.
                const double dE = calc_extrusion(seg.length());
                out += m_writer.extrude_to_xyz(
                    Vec3d(abs_end.x(), abs_end.y(), layer_z), dE, "draw extrude");

            } else if (seg.type == DrawSegmentType::CircularArc && native_arc_mode) {
                // Native arc mode: try G2/G3.
                // Compute circumcenter in segment (plate-relative) coordinates.
                const Vec2d S = seg.start;
                const Vec2d P = seg.ctrl1; // through-point
                const Vec2d E = seg.end;

                // Circumcenter calculation inline (same as sampling helper).
                const double ax = S.x(), ay = S.y();
                const double bx = P.x(), by = P.y();
                const double cx = E.x(), cy = E.y();
                const double D  = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));

                if (std::abs(D) < 1e-10) {
                    // Collinear fallback: single G1.
                    const double dE = calc_extrusion(seg.length());
                    out += m_writer.extrude_to_xyz(
                        Vec3d(abs_end.x(), abs_end.y(), layer_z), dE, "draw arc fallback");
                } else {
                    const double sa = ax * ax + ay * ay;
                    const double sb = bx * bx + by * by;
                    const double sc = cx * cx + cy * cy;
                    const Vec2d local_center(
                        (sa * (by - cy) + sb * (cy - ay) + sc * (ay - by)) / D,
                        (sa * (cx - bx) + sb * (ax - cx) + sc * (bx - ax)) / D);

                    // center_offset = center_abs - current_abs_start
                    // Since abs_start = seg.start + abs_offset, and center_abs = local_center + abs_offset,
                    // center_offset = local_center - seg.start (the abs_offset cancels).
                    const Vec2d center_offset = local_center - seg.start;

                    // Determine CCW vs CW via cross product of (P-S) × (E-S).
                    const double cross = (P.x() - S.x()) * (E.y() - S.y())
                                       - (P.y() - S.y()) * (E.x() - S.x());
                    const bool is_ccw = (cross > 0.0);

                    // Use sampled length for accurate extrusion volume.
                    const double arc_length = draw_segment_sampled_length(seg);
                    const double dE = calc_extrusion(arc_length);

                    out += m_writer.extrude_arc_to_xy(
                        abs_end, center_offset, dE, is_ccw, "draw arc G2/G3");
                }

            } else {
                // Compatibility mode for arcs, or any bezier: G1 linearization.
                const std::vector<Vec2d> pts = draw_sample_segment(seg);
                for (size_t i = 1; i < pts.size(); ++i) {
                    const Vec2d abs_pt  = pts[i]     + abs_offset;
                    const Vec2d abs_prev = pts[i - 1] + abs_offset;
                    const double chord = (pts[i] - pts[i - 1]).norm();
                    const double dE    = calc_extrusion(chord);
                    out += m_writer.extrude_to_xyz(
                        Vec3d(abs_pt.x(), abs_pt.y(), layer_z), dE, "draw extrude");
                    (void)abs_prev; // used implicitly through previous writer position
                }
            }
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// Private: fan speed helper
// ---------------------------------------------------------------------------

int DrawPathGCodeGenerator::calc_fan_speed_pct(int layer_index) const
{
    int   close_fan_x  = cfg_int_vec("close_fan_the_first_x_layers");
    int   full_fan_lyr = cfg_int_vec("full_fan_speed_layer");
    float fan_max      = static_cast<float>(cfg_float_vec("fan_max_speed"));

    // Match CoolingBuffer: force at least 1 closed layer when ramping is on.
    if (close_fan_x <= 0 && full_fan_lyr > 0)
        close_fan_x = 1;

    if (layer_index < close_fan_x)
        return 0;

    // Draw layers are always short: assume full cooling intent (fan_max).
    float speed = fan_max;
    if (full_fan_lyr > close_fan_x && (layer_index + 1) < full_fan_lyr) {
        const float factor = float(layer_index + 1 - close_fan_x)
                           / float(full_fan_lyr  - close_fan_x);
        speed = std::clamp(fan_max * factor, 0.0f, 100.0f);
    }
    return static_cast<int>(speed + 0.5f);
}

// ---------------------------------------------------------------------------
// Private: postamble
// ---------------------------------------------------------------------------

std::string DrawPathGCodeGenerator::generate_postamble(const DrawSession& session)
{
    std::string out;

    out += m_writer.retract();

    // Lift nozzle 10 mm above current position.
    const Vec3d& pos = m_writer.get_position();
    out += m_writer.travel_to_z(pos.z() + 10.0, "nozzle lift");

    const double max_z = max_layer_z(session);
    prepare_placeholder_parser(session, static_cast<int>(session.layers.size()), max_z);

    out += gcode_processor_role_tag(erCustom);

    // Process profile end templates with the same placeholder context as the
    // normal slicer path.
    DynamicConfig filament_config;
    filament_config.set_key_value("filament_extruder_id", new ConfigOptionInt(0));
    filament_config.set_key_value("layer_num", new ConfigOptionInt(static_cast<int>(session.layers.size())));
    filament_config.set_key_value("layer_z", new ConfigOptionFloat(max_z));
    filament_config.set_key_value("max_layer_z", new ConfigOptionFloat(max_z));
    out += process_config_gcode_strings("filament_end_gcode", 0, &filament_config);
    out += process_config_gcode_string("machine_end_gcode", 0, &filament_config);

    // Turn heaters off (no wait).
    out += m_writer.set_temperature(0, /*wait=*/false, /*tool=*/0);

    // Fan off.
    out += m_writer.set_fan(0u);

    out += m_writer.postamble();
    return out;
}

// ---------------------------------------------------------------------------
// Private: placeholder expansion helpers
// ---------------------------------------------------------------------------

double DrawPathGCodeGenerator::max_layer_z(const DrawSession& session) const
{
    double max_z = 0.0;
    for (const DrawLayer& layer : session.layers)
        max_z = std::max(max_z, layer.z_end);
    return max_z;
}

void DrawPathGCodeGenerator::prepare_placeholder_parser(const DrawSession& session,
                                                        int                layer_num,
                                                        double             layer_z)
{
    const double max_z = max_layer_z(session);
    const int    extruder_id = 0;

    m_placeholder_parser.set("max_layer_z", max_z);
    m_placeholder_parser.set("layer_num", layer_num);
    m_placeholder_parser.set("layer_z", layer_z);
    m_placeholder_parser.set("total_layer_count", static_cast<int>(session.layers.size()));

    // Draw-mode exports currently use the first active filament/extruder.  The
    // printer profile templates nevertheless expect the same symbols that the
    // normal slicer defines after it computes tool ordering.
    m_placeholder_parser.set("initial_tool", extruder_id);
    m_placeholder_parser.set("initial_extruder", extruder_id);
    m_placeholder_parser.set("initial_no_support_tool", extruder_id);
    m_placeholder_parser.set("initial_no_support_extruder", extruder_id);
    m_placeholder_parser.set("current_extruder", extruder_id);
    m_placeholder_parser.set("first_tools", new ConfigOptionInts({extruder_id}));
    m_placeholder_parser.set("first_filaments", new ConfigOptionInts({extruder_id}));
    m_placeholder_parser.set("first_non_support_tools", new ConfigOptionInts({extruder_id}));
    m_placeholder_parser.set("first_non_support_filaments", new ConfigOptionInts({extruder_id}));
    m_placeholder_parser.set("has_wipe_tower", false);
    m_placeholder_parser.set("has_single_extruder_multi_material_priming", false);
    m_placeholder_parser.set("total_toolchanges", 0);
    m_placeholder_parser.set("current_object_idx", 0);

    int num_extruders = 1;
    if (const auto* nozzles = dynamic_cast<const ConfigOptionFloats*>(m_config.option("nozzle_diameter")))
        num_extruders = std::max<int>(1, static_cast<int>(nozzles->values.size()));
    m_placeholder_parser.set("num_extruders", num_extruders);
    std::vector<unsigned char> is_extruder_used(static_cast<size_t>(std::max(num_extruders, 1)), false);
    is_extruder_used.front() = true;
    m_placeholder_parser.set("is_extruder_used", new ConfigOptionBools(is_extruder_used));

    // Bed geometry variables are frequently used by purge/start templates.
    double bed_min_x = 0.0, bed_min_y = 0.0, bed_max_x = 0.0, bed_max_y = 0.0;
    if (const auto* printable_area = dynamic_cast<const ConfigOptionPoints*>(m_config.option("printable_area"));
        printable_area && !printable_area->values.empty()) {
        bed_min_x = bed_max_x = printable_area->values.front().x();
        bed_min_y = bed_max_y = printable_area->values.front().y();
        for (const Vec2d& p : printable_area->values) {
            bed_min_x = std::min(bed_min_x, p.x());
            bed_min_y = std::min(bed_min_y, p.y());
            bed_max_x = std::max(bed_max_x, p.x());
            bed_max_y = std::max(bed_max_y, p.y());
        }
    }
    m_placeholder_parser.set("print_bed_min", new ConfigOptionFloats({bed_min_x, bed_min_y}));
    m_placeholder_parser.set("print_bed_max", new ConfigOptionFloats({bed_max_x, bed_max_y}));
    m_placeholder_parser.set("print_bed_size", new ConfigOptionFloats({bed_max_x - bed_min_x, bed_max_y - bed_min_y}));

    // Temperature aliases used by Orca/Bambu profile G-code.  Prefer the
    // currently selected bed type, falling back to the hot-plate settings used
    // by the earlier draw-mode preamble.
    BedType bed_type = btPEI;
    if (const ConfigOption* bed_type_opt = m_config.option("curr_bed_type"))
        bed_type = static_cast<BedType>(bed_type_opt->getInt());

    const std::string first_bed_key = get_bed_temp_1st_layer_key(bed_type);
    const std::string bed_key       = get_bed_temp_key(bed_type);
    const ConfigOptionInts* first_bed_opt = first_bed_key.empty() ? nullptr : dynamic_cast<const ConfigOptionInts*>(m_config.option(first_bed_key));
    const ConfigOptionInts* bed_opt       = bed_key.empty()       ? nullptr : dynamic_cast<const ConfigOptionInts*>(m_config.option(bed_key));
    if (!first_bed_opt)
        first_bed_opt = dynamic_cast<const ConfigOptionInts*>(m_config.option("hot_plate_temp_initial_layer"));
    if (!bed_opt)
        bed_opt = dynamic_cast<const ConfigOptionInts*>(m_config.option("hot_plate_temp"));

    const int first_bed_temp = first_bed_opt && !first_bed_opt->values.empty() ? first_bed_opt->values.front() : cfg_int_vec("hot_plate_temp");
    if (first_bed_opt)
        m_placeholder_parser.set("bed_temperature_initial_layer", new ConfigOptionInts(*first_bed_opt));
    else
        m_placeholder_parser.set("bed_temperature_initial_layer", new ConfigOptionInts({first_bed_temp}));
    if (bed_opt)
        m_placeholder_parser.set("bed_temperature", new ConfigOptionInts(*bed_opt));
    else
        m_placeholder_parser.set("bed_temperature", new ConfigOptionInts({cfg_int_vec("hot_plate_temp")}));
    m_placeholder_parser.set("bed_temperature_initial_layer_single", first_bed_temp);
    m_placeholder_parser.set("first_layer_bed_temperature", new ConfigOptionInts({first_bed_temp}));
    if (const auto* nozzle_temps = dynamic_cast<const ConfigOptionInts*>(m_config.option("nozzle_temperature_initial_layer")))
        m_placeholder_parser.set("first_layer_temperature", new ConfigOptionInts(*nozzle_temps));

    if (const auto* chamber = dynamic_cast<const ConfigOptionInts*>(m_config.option("chamber_temperature"))) {
        int max_chamber = 0;
        for (int temp : chamber->values)
            max_chamber = std::max(max_chamber, temp);
        m_placeholder_parser.set("chamber_temperature", new ConfigOptionInts(*chamber));
        m_placeholder_parser.set("overall_chamber_temperature", max_chamber);
    }

    m_placeholder_parser.set("max_print_height", static_cast<int>(std::ceil(cfg_float("printable_height"))));
    m_placeholder_parser.set("max_print_z", static_cast<int>(std::ceil(max_z)));
    m_placeholder_parser.set("z_offset", cfg_float("z_offset"));
    m_placeholder_parser.set("print_time_sec", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Print_Time_Sec_Placeholder));
    m_placeholder_parser.set("used_filament_length", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Used_Filament_Length_Placeholder));

    const Vec3d& pos = m_writer.get_position();
    m_placeholder_parser.set("position", new ConfigOptionFloats({pos.x(), pos.y(), pos.z()}));
}

std::string DrawPathGCodeGenerator::process_gcode_template(const std::string& name,
                                                           const std::string& templ,
                                                           unsigned int       extruder_id,
                                                           const DynamicConfig* config_override) const
{
    if (templ.empty())
        return "";

    try {
        std::string expanded = m_placeholder_parser.process(templ, extruder_id, config_override);
        if (!expanded.empty() && expanded.back() != '\n')
            expanded += '\n';
        return expanded;
    } catch (const std::exception& e) {
        // A failed profile template should not crash exporting, but it must be
        // visible in the output so the user can fix the preset.
        return "; ERROR: Failed to process " + name + ": " + e.what() + "\n";
    }
}

std::string DrawPathGCodeGenerator::process_config_gcode_string(const std::string& key,
                                                                unsigned int       extruder_id,
                                                                const DynamicConfig* config_override) const
{
    const auto* opt = dynamic_cast<const ConfigOptionString*>(m_config.option(key));
    return opt ? process_gcode_template(key, opt->value, extruder_id, config_override) : "";
}

std::string DrawPathGCodeGenerator::process_config_gcode_strings(const std::string& key,
                                                                 unsigned int       extruder_id,
                                                                 const DynamicConfig* config_override) const
{
    const auto* opt = dynamic_cast<const ConfigOptionStrings*>(m_config.option(key));
    if (!opt || opt->values.empty())
        return "";

    const size_t idx = std::min<size_t>(extruder_id, opt->values.size() - 1);
    return process_gcode_template(key, opt->values[idx], extruder_id, config_override);
}

// ---------------------------------------------------------------------------
// Private: extrusion calculation
// ---------------------------------------------------------------------------

double DrawPathGCodeGenerator::calc_extrusion(double segment_length_mm) const
{
    if (segment_length_mm <= 0.0)
        return 0.0;

    // PRD-MAP: "layer_height" → layer_height from PrintObjectConfig.
    const double nozzle_d    = std::max(cfg_float_vec("nozzle_diameter"), 0.1);
    const double layer_h     = std::max(cfg_float("layer_height"),        0.05);
    const double filament_d  = std::max(cfg_float_vec("filament_diameter"), 0.1);
    // PRD-MAP: "extrusion_multiplier" → print_flow_ratio (defaults to 1.0).
    const double flow_ratio  = std::max(cfg_float("print_flow_ratio"), 0.01);

    const double filament_radius = filament_d / 2.0;
    const double filament_area   = M_PI * filament_radius * filament_radius;
    const double extrusion_area  = nozzle_d * layer_h; // rectangle approximation
    const double E_per_mm        = extrusion_area / filament_area * flow_ratio;
    return E_per_mm * segment_length_mm;
}

// ---------------------------------------------------------------------------
// Private: config helpers
// ---------------------------------------------------------------------------

double DrawPathGCodeGenerator::cfg_float(const std::string& key) const
{
    const ConfigOption* opt = m_config.option(key);
    if (!opt) return 0.0;
    return opt->getFloat();
}

int DrawPathGCodeGenerator::cfg_int(const std::string& key) const
{
    const ConfigOption* opt = m_config.option(key);
    if (!opt) return 0;
    // ConfigOptionBool does not support getInt() — handle it explicitly.
    if (opt->type() == coBool)
        return opt->getBool() ? 1 : 0;
    return opt->getInt();
}

double DrawPathGCodeGenerator::cfg_float_vec(const std::string& key) const
{
    const ConfigOption* opt = m_config.option(key);
    if (!opt) return 0.0;
    // ConfigOptionFloats / ConfigOptionPercents — use index 0.
    if (const auto* vec = dynamic_cast<const ConfigOptionFloats*>(opt))
        return vec->values.empty() ? 0.0 : vec->values.front();
    return opt->getFloat();
}

int DrawPathGCodeGenerator::cfg_int_vec(const std::string& key) const
{
    const ConfigOption* opt = m_config.option(key);
    if (!opt) return 0;
    // ConfigOptionInts — use index 0.
    if (const auto* vec = dynamic_cast<const ConfigOptionInts*>(opt))
        return vec->values.empty() ? 0 : vec->values.front();
    return opt->getInt();
}

} // namespace Slic3r
