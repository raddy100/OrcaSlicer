#include "DrawPathGCodeGenerator.hpp"

#include "Model.hpp"

#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Slic3r {

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
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string DrawPathGCodeGenerator::generate(const DrawSession& session,
                                              const Vec2d&       instance_offset)
{
    if (session.is_empty())
        return generate_preamble(session) + generate_postamble();

    // Total XY offset to convert plate-relative → absolute machine coords.
    const Vec2d abs_offset = m_plate_origin + instance_offset;

    std::string gcode;
    gcode.reserve(1024 * 64);

    gcode += generate_preamble(session);

    for (const DrawLayer& layer : session.layers) {
        gcode += generate_layer(layer, abs_offset);
    }

    gcode += generate_postamble();
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
        gcode += generate_postamble();
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

    gcode += generate_postamble();
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

// ---------------------------------------------------------------------------
// Private: preamble
// ---------------------------------------------------------------------------

std::string DrawPathGCodeGenerator::generate_preamble(const DrawSession& session)
{
    std::string out;

    // Standard G-code preamble (G90, G21, M82/M83, reset E).
    out += m_writer.preamble();
    out += m_writer.reset_e(true);

    // Nozzle warm-up — use first-layer temperature and wait (M109).
    const int first_layer_temp = cfg_int_vec("nozzle_temperature_initial_layer");
    if (first_layer_temp > 0)
        out += m_writer.set_temperature(static_cast<unsigned int>(first_layer_temp), /*wait=*/true, /*tool=*/0);

    // Bed heat-up (M190). We use hot_plate_temp as a reasonable default.
    // PRD-MAP: "bed_temperature" → hot_plate_temp (index 0); adjust if a
    // different bed type is needed in the future.
    const int bed_temp = cfg_int_vec("hot_plate_temp");
    if (bed_temp > 0)
        out += m_writer.set_bed_temperature(bed_temp, /*wait=*/true);

    // Draw mode emits direct, plate-relative toolpaths for preview/simulation
    // without entering the normal slicing pipeline.  Do not append the printer
    // start template here: vendor profiles may contain purge/cleanup moves and
    // unexpanded placeholder conditionals that are valid for full prints, but
    // can leave the draw work area and trip preview plate-boundary checks for a
    // simple drawing.
    out += "; draw mode: custom printer start template suppressed\n";

    // Fan on.
    const double fan_min = cfg_float_vec("fan_min_speed");
    if (fan_min > 0.0)
        out += m_writer.set_fan(static_cast<unsigned int>(fan_min));

    // Move to the first layer's print height before beginning.
    // Use z_end (the actual print height), NOT z_start which is 0 for the first layer.
    if (!session.layers.empty()) {
        out += m_writer.travel_to_z(session.layers.front().z_end, "initial Z");
    }

    return out;
}

// ---------------------------------------------------------------------------
// Private: per-layer
// ---------------------------------------------------------------------------

std::string DrawPathGCodeGenerator::generate_layer(const DrawLayer& layer,
                                                    const Vec2d&     abs_offset)
{
    std::string out;

    const bool is_first = (layer.layer_index == 0);

    // Temperature for this layer.
    const int temp = is_first
        ? cfg_int_vec("nozzle_temperature_initial_layer")
        : cfg_int_vec("nozzle_temperature");
    if (temp > 0)
        out += m_writer.set_temperature(static_cast<unsigned int>(temp), /*wait=*/false, /*tool=*/0);

    // Speed for this layer (convert mm/s → mm/min for G-code F parameter).
    // PRD-MAP: "first_layer_speed" → initial_layer_speed (mm/s)
    //          "print_speed"       → outer_wall_speed     (mm/s)
    const double speed_mms = is_first
        ? cfg_float("initial_layer_speed")
        : cfg_float("outer_wall_speed");
    const double speed_mmmin = speed_mms * 60.0;
    if (speed_mmmin > 0.0)
        out += m_writer.set_speed(speed_mmmin, "draw layer speed");

    // All segments in a layer print at the same Z height (z_end).
    // There is no Z ramp within a layer — the entire layer is printed at a constant Z.
    const double layer_z = layer.z_end;

    for (const DrawSegment& seg : layer.segments) {
        const Vec2d abs_start = seg.start + abs_offset;
        const Vec2d abs_end   = seg.end   + abs_offset;

        // Verify head is at abs_start (X, Y, and Z); if not, emit a positioning travel first.
        const Vec3d& cur = m_writer.get_position();
        const bool need_reposition =
            std::abs(cur.x() - abs_start.x()) > 0.001 ||
            std::abs(cur.y() - abs_start.y()) > 0.001 ||
            std::abs(cur.z() - layer_z)       > 0.001;

        if (seg.is_travel) {
            // For travel segments: retract once, optionally visit abs_start, then go to abs_end.
            out += m_writer.retract();
            if (need_reposition)
                out += m_writer.travel_to_xyz(Vec3d(abs_start.x(), abs_start.y(), layer_z), "to travel-src");
            out += m_writer.travel_to_xyz(Vec3d(abs_end.x(), abs_end.y(), layer_z), "travel");
            out += m_writer.unretract();
        } else {
            // For extrusion segments: position head at abs_start if needed, then extrude to abs_end.
            if (need_reposition) {
                out += m_writer.retract();
                out += m_writer.travel_to_xyz(Vec3d(abs_start.x(), abs_start.y(), layer_z), "to seg start");
                out += m_writer.unretract();
            }
            const double dE = calc_extrusion(seg.length());
            out += m_writer.extrude_to_xyz(
                Vec3d(abs_end.x(), abs_end.y(), layer_z), dE, "draw extrude");
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// Private: postamble
// ---------------------------------------------------------------------------

std::string DrawPathGCodeGenerator::generate_postamble()
{
    std::string out;

    out += m_writer.retract();

    // Lift nozzle 10 mm above current position.
    const Vec3d& pos = m_writer.get_position();
    out += m_writer.travel_to_z(pos.z() + 10.0, "nozzle lift");

    // Turn heaters off (no wait).
    out += m_writer.set_temperature(0, /*wait=*/false, /*tool=*/0);

    // Fan off.
    out += m_writer.set_fan(0u);

    // See generate_preamble(): suppress the printer end template as well, as
    // Bambu-style cleanup/purge parking moves can be outside a draw-mode work
    // area's plate bounds and may still contain unsliced placeholders.
    out += "; draw mode: custom printer end template suppressed\n";

    out += m_writer.postamble();
    return out;
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
