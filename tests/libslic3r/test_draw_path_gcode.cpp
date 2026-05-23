#include <catch2/catch_all.hpp>
#include "libslic3r/DrawPathGCodeGenerator.hpp"
#include "libslic3r/DrawSession.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"

#include <algorithm>
#include <cmath>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <utility>

using namespace Slic3r;

// Build a minimal merged DynamicPrintConfig with the keys DrawPathGCodeGenerator needs.
static DynamicPrintConfig make_test_config(double nozzle_d = 0.4,
                                           double layer_h  = 0.2,
                                           double filament_d = 1.75,
                                           double flow_ratio  = 1.0)
{
    DynamicPrintConfig cfg;
    // gcode_flavor (needed by GCodeWriter preamble formatting)
    cfg.set_key_value("gcode_flavor", new ConfigOptionEnum<GCodeFlavor>(gcfMarlinLegacy));
    cfg.set_key_value("printer_model", new ConfigOptionString("Unit Test Printer"));

    // Nozzle geometry
    cfg.set_key_value("nozzle_diameter",  new ConfigOptionFloats({ nozzle_d }));
    cfg.set_key_value("filament_diameter", new ConfigOptionFloats({ filament_d }));

    // Temperatures
    cfg.set_key_value("nozzle_temperature",               new ConfigOptionInts({ 200 }));
    cfg.set_key_value("nozzle_temperature_initial_layer", new ConfigOptionInts({ 210 }));
    cfg.set_key_value("hot_plate_temp",                   new ConfigOptionInts({ 60 }));
    cfg.set_key_value("hot_plate_temp_initial_layer",     new ConfigOptionInts({ 60 }));
    cfg.set_key_value("curr_bed_type",                    new ConfigOptionEnum<BedType>(btPEI));
    cfg.set_key_value("chamber_temperature",              new ConfigOptionInts({ 0 }));
    cfg.set_key_value("printable_height",                 new ConfigOptionFloat(250.0));
    cfg.set_key_value("printable_area",                   new ConfigOptionPoints({
        Vec2d(0.0, 0.0), Vec2d(250.0, 0.0), Vec2d(250.0, 250.0), Vec2d(0.0, 250.0) }));

    // Speeds (mm/s)
    cfg.set_key_value("outer_wall_speed",    new ConfigOptionFloat(50.0));
    cfg.set_key_value("initial_layer_speed", new ConfigOptionFloat(30.0));
    cfg.set_key_value("outer_wall_acceleration", new ConfigOptionFloat(1000.0));

    // Retraction
    cfg.set_key_value("retraction_length", new ConfigOptionFloats({ 0.8 }));
    cfg.set_key_value("retraction_speed",  new ConfigOptionFloats({ 45.0 }));
    cfg.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats({ 8.0 }));
    cfg.set_key_value("filament_type", new ConfigOptionStrings({ "PLA" }));
    cfg.set_key_value("enable_pressure_advance", new ConfigOptionBools({ false }));
    cfg.set_key_value("pressure_advance", new ConfigOptionFloats({ 0.02 }));

    // Process
    cfg.set_key_value("layer_height",     new ConfigOptionFloat(layer_h));
    cfg.set_key_value("print_flow_ratio", new ConfigOptionFloat(flow_ratio));

    // Fan
    cfg.set_key_value("fan_min_speed", new ConfigOptionFloats({ 0.0 }));

    // Machine limits (required by apply_print_config internals via GCodeConfig)
    cfg.set_key_value("use_relative_e_distances", new ConfigOptionBool(false));

    return cfg;
}

// Build a small two-layer DrawSession for reuse.
static DrawSession make_test_session()
{
    DrawSession session;
    // Layer 0: 1 extrusion + 1 travel.
    session.add_layer(0.2);
    DrawLayer& l0 = session.layers.back();
    DrawSegment s0; s0.start = Vec2d(0, 0); s0.end = Vec2d(10, 0); s0.is_travel = false;
    DrawSegment s1; s1.start = Vec2d(10, 0); s1.end = Vec2d(20, 0); s1.is_travel = true;
    l0.segments.push_back(s0);
    l0.segments.push_back(s1);

    // Layer 1: 2 extrusions.
    session.add_layer(0.2);
    DrawLayer& l1 = session.layers.back();
    DrawSegment s2; s2.start = Vec2d(20, 0); s2.end = Vec2d(20, 10); s2.is_travel = false;
    DrawSegment s3; s3.start = Vec2d(20, 10); s3.end = Vec2d(30, 10); s3.is_travel = false;
    l1.segments.push_back(s2);
    l1.segments.push_back(s3);

    return session;
}

static DrawSession make_centered_box_session()
{
    DrawSession session;
    session.add_layer(0.2);
    DrawLayer& layer = session.layers.back();
    layer.segments.push_back({ Vec2d(-7.5, -7.5), Vec2d( 7.5, -7.5), false });
    layer.segments.push_back({ Vec2d( 7.5, -7.5), Vec2d( 7.5,  7.5), false });
    layer.segments.push_back({ Vec2d( 7.5,  7.5), Vec2d(-7.5,  7.5), false });
    layer.segments.push_back({ Vec2d(-7.5,  7.5), Vec2d(-7.5, -7.5), false });
    return session;
}

static std::optional<double> extract_axis_value(const std::string& line, char axis)
{
    const std::string token = std::string(" ") + axis;
    auto ap = line.find(token);
    if (ap == std::string::npos)
        return std::nullopt;
    return std::stod(line.substr(ap + 2));
}

static std::size_t count_occurrences(const std::string& text, const std::string& needle)
{
    std::size_t count = 0;
    std::size_t pos   = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

struct ScopedBBLRoleTags {
    bool previous;

    ScopedBBLRoleTags()
        : previous(GCodeProcessor::s_IsBBLPrinter)
    {
        GCodeProcessor::s_IsBBLPrinter = true;
    }

    ~ScopedBBLRoleTags()
    {
        GCodeProcessor::s_IsBBLPrinter = previous;
    }
};

// ---------------------------------------------------------------------------
// TASK-005: calc_extrusion math
// ---------------------------------------------------------------------------
TEST_CASE("DrawPathGCodeGenerator: extrusion math matches PRD formula", "[DrawPathGCodeGenerator]")
{
    // E_per_mm = (nozzle_d * layer_h) / (pi * (filament_d/2)^2) * flow_ratio
    // = (0.4 * 0.2) / (pi * 0.875^2) * 1.0 ≈ 0.08 / 2.40528 ≈ 0.03326 per mm
    // * 10 mm → ≈ 0.3326 E
    const double expected_E = (0.4 * 0.2) / (M_PI * (1.75 / 2.0) * (1.75 / 2.0)) * 10.0;

    DynamicPrintConfig cfg = make_test_config(0.4, 0.2, 1.75, 1.0);
    DrawSession session;
    session.add_layer(0.2);
    DrawLayer& l = session.layers.back();
    DrawSegment seg; seg.start = Vec2d(0, 0); seg.end = Vec2d(10, 0); seg.is_travel = false;
    l.segments.push_back(seg);

    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    std::string gcode = gen.generate(session);

    // Extract positive E values from "G1 ... E..." lines (skip retraction/negative E).
    // OrcaSlicer's postamble emits a retraction (negative E); we want only extrusion.
    double actual_E = 0.0;
    std::size_t pos = 0;
    while ((pos = gcode.find("G1 ", pos)) != std::string::npos) {
        std::size_t line_end = gcode.find('\n', pos);
        auto e_pos = gcode.find(" E", pos);
        if (e_pos != std::string::npos && e_pos < line_end) {
            std::size_t val_start = e_pos + 2;
            double val = std::stod(gcode.substr(val_start, gcode.find_first_of(" \n\r", val_start) - val_start));
            if (val > 0.0)  // skip retraction (negative E)
                actual_E += val;
        }
        pos = (line_end != std::string::npos) ? line_end + 1 : std::string::npos;
    }

    REQUIRE_THAT(actual_E, Catch::Matchers::WithinRel(expected_E, 0.01));
}

// ---------------------------------------------------------------------------
// TASK-007: Generator-level tests
// ---------------------------------------------------------------------------
TEST_CASE("DrawPathGCodeGenerator: empty session produces preamble and postamble only", "[DrawPathGCodeGenerator]")
{
    // Use zero retraction so that the postamble retract doesn't emit any E moves.
    // An empty session should produce no material extrusion whatsoever.
    DynamicPrintConfig cfg = make_test_config();
    cfg.set_key_value("retraction_length", new ConfigOptionFloats({ 0.0 }));
    DrawSession session; // empty
    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    std::string gcode = gen.generate(session);

    // Must contain preamble content (G90) and postamble but no G1 extrusion/retraction moves.
    // Note: G92 E0 in the preamble is a position reset command (not extrusion) — we only check G1 lines.
    REQUIRE(gcode.find("G90") != std::string::npos);
    bool has_g1_e = false;
    std::size_t pos = 0;
    while ((pos = gcode.find("G1 ", pos)) != std::string::npos) {
        std::size_t line_end = gcode.find('\n', pos);
        if (gcode.find(" E", pos) < line_end) { has_g1_e = true; break; }
        pos = (line_end != std::string::npos) ? line_end + 1 : std::string::npos;
    }
    REQUIRE(!has_g1_e);
}

TEST_CASE("DrawPathGCodeGenerator: profile startup owns heat waits",
    "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_test_config();
    cfg.set_key_value("machine_start_gcode", new ConfigOptionString(
        "; profile heat start\n"
        "M190 S[bed_temperature_initial_layer_single] ; profile bed wait\n"
        "M109 S{first_layer_temperature[0]} ; profile nozzle wait\n"
        "; profile heat end\n"));

    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(make_centered_box_session(), Vec2d::Zero());

    const std::size_t profile_pos = gcode.find("; profile heat start");
    REQUIRE(profile_pos != std::string::npos);
    REQUIRE(gcode.find("M190 S60 ; profile bed wait") != std::string::npos);
    REQUIRE(gcode.find("M109 S210 ; profile nozzle wait") != std::string::npos);

    // The generator must not insert its generic waits before printer profile
    // startup; otherwise profiles that already wait would heat twice.
    REQUIRE(gcode.rfind("M190", profile_pos) == std::string::npos);
    REQUIRE(gcode.rfind("M109", profile_pos) == std::string::npos);
    REQUIRE(count_occurrences(gcode, "M190") == 1);
    REQUIRE(count_occurrences(gcode, "M109") == 1);
}

TEST_CASE("DrawPathGCodeGenerator: missing or empty profile startup uses fallback heat waits",
    "[DrawPathGCodeGenerator]")
{
    {
        DynamicPrintConfig cfg = make_test_config();
        DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
        const std::string gcode = gen.generate(make_centered_box_session(), Vec2d::Zero());

        REQUIRE(count_occurrences(gcode, "M190") == 1);
        REQUIRE(count_occurrences(gcode, "M109") == 1);
    }

    {
        DynamicPrintConfig cfg = make_test_config();
        cfg.set_key_value("machine_start_gcode", new ConfigOptionString(" \t\r\n"));
        DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
        const std::string gcode = gen.generate(make_centered_box_session(), Vec2d::Zero());

        REQUIRE(count_occurrences(gcode, "M190") == 1);
        REQUIRE(count_occurrences(gcode, "M109") == 1);
    }
}

TEST_CASE("DrawPathGCodeGenerator: write_gcode_file creates missing parent directories", "[DrawPathGCodeGenerator]")
{
    const boost::filesystem::path temp_root =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("draw-path-write-%%%%-%%%%");
    const boost::filesystem::path output_path = temp_root / "nested" / "draw_path.gcode";

    std::string error;
    REQUIRE(DrawPathGCodeGenerator::write_gcode_file(output_path.string(), "G90\n; draw path\n", &error));
    REQUIRE(error.empty());
    REQUIRE(boost::filesystem::exists(output_path));

    std::ifstream in(output_path.string(), std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(contents == "G90\n; draw path\n");

    boost::system::error_code ec;
    boost::filesystem::remove_all(temp_root, ec);
}

TEST_CASE("DrawPathGCodeGenerator: write_gcode_file writes explicit selected destination",
          "[DrawPathGCodeGenerator]")
{
    const char* output_path_env = std::getenv("ORCASLICER_DRAW_PATH_GCODE_TEST_OUTPUT_PATH");
    if (output_path_env == nullptr || output_path_env[0] == '\0') {
        SUCCEED("Optional selected-destination write probe skipped because no output path was provided.");
        return;
    }

    const boost::filesystem::path output_path(output_path_env);
    const std::string             gcode = "G90\n; selected draw path destination\n";

    std::string error;
    REQUIRE(DrawPathGCodeGenerator::write_gcode_file(output_path.string(), gcode, &error));
    REQUIRE(error.empty());
    REQUIRE(boost::filesystem::exists(output_path));

    std::ifstream in(output_path.string(), std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(contents == gcode);

    boost::system::error_code ec;
    boost::filesystem::remove(output_path, ec);
}

#ifdef _WIN32
TEST_CASE("DrawPathGCodeGenerator: parent_path_for_file preserves Windows drive destinations", "[DrawPathGCodeGenerator]")
{
    REQUIRE(DrawPathGCodeGenerator::parent_path_for_file("G:\\draw_path_output.gcode") == "G:\\");
    REQUIRE(DrawPathGCodeGenerator::parent_path_for_file("G:\\exports\\draw_path_output.gcode") == "G:\\exports");
}
#endif

TEST_CASE("DrawPathGCodeGenerator: single extrusion segment produces exactly one G1 E line", "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_test_config();
    DrawSession session;
    session.add_layer(0.2);
    DrawLayer& l = session.layers.back();
    DrawSegment seg; seg.start = Vec2d(0, 0); seg.end = Vec2d(10, 0); seg.is_travel = false;
    l.segments.push_back(seg);

    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    std::string gcode = gen.generate(session);

    // Count G1 lines that contain positive " E" values (extrusion only; skip retraction which is negative E).
    // OrcaSlicer's postamble emits a retraction G1 E- line — that must not be counted.
    int count = 0;
    std::size_t pos = 0;
    while ((pos = gcode.find("G1 ", pos)) != std::string::npos) {
        std::size_t line_end = gcode.find('\n', pos);
        auto e_pos = gcode.find(" E", pos);
        if (e_pos != std::string::npos && e_pos < line_end) {
            std::size_t val_start = e_pos + 2;
            double val = std::stod(gcode.substr(val_start, gcode.find_first_of(" \n\r", val_start) - val_start));
            if (val > 0.0) ++count;
        }
        pos = (line_end != std::string::npos) ? line_end + 1 : std::string::npos;
    }
    REQUIRE(count == 1);
}

TEST_CASE("DrawPathGCodeGenerator: connected extrusion segments stay continuous at shared endpoints",
    "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_test_config();
    cfg.set_key_value("retraction_length", new ConfigOptionFloats({ 0.0 }));

    DrawSession session;
    session.add_layer(0.2);
    DrawLayer& layer = session.layers.back();
    layer.segments.push_back({ Vec2d(10.0, 10.0), Vec2d(20.0, 10.0), false });
    layer.segments.push_back({ Vec2d(20.0, 10.0), Vec2d(20.0, 20.0), false });

    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(session);

    bool seen_first_extrusion = false;
    bool checked_join = false;
    double previous_e = 0.0;

    std::size_t pos = 0;
    while ((pos = gcode.find("G1 ", pos)) != std::string::npos) {
        std::size_t line_end = gcode.find('\n', pos);
        if (line_end == std::string::npos)
            line_end = gcode.size();

        const auto e_pos = gcode.find(" E", pos);
        if (e_pos != std::string::npos && e_pos < line_end) {
            const std::size_t val_start = e_pos + 2;
            const double e = std::stod(gcode.substr(val_start, gcode.find_first_of(" \n\r", val_start) - val_start));
            if (e > previous_e + 1e-9) {
                if (seen_first_extrusion) {
                    checked_join = true;
                    break;
                }
                seen_first_extrusion = true;
            }
            previous_e = e;
        } else if (seen_first_extrusion) {
            FAIL("Inserted a travel move between connected extrusion segments");
        }

        pos = line_end + 1;
    }

    REQUIRE(checked_join);
}

TEST_CASE("DrawPathGCodeGenerator: Z values are strictly non-decreasing", "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_test_config();
    DrawSession session = make_test_session();

    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    std::string gcode = gen.generate(session);

    // Parse all Z values mentioned in G0/G1 lines.
    std::vector<double> zvals;
    std::size_t pos = 0;
    while (pos < gcode.size()) {
        std::size_t line_end = gcode.find('\n', pos);
        if (line_end == std::string::npos) line_end = gcode.size();
        std::string line = gcode.substr(pos, line_end - pos);
        if ((line.rfind("G0 ", 0) == 0 || line.rfind("G1 ", 0) == 0)) {
            auto z_pos = line.find(" Z");
            if (z_pos != std::string::npos) {
                zvals.push_back(std::stod(line.substr(z_pos + 2)));
            }
        }
        pos = line_end + 1;
    }

    REQUIRE(!zvals.empty());
    for (std::size_t i = 1; i < zvals.size(); ++i) {
        REQUIRE(zvals[i] >= zvals[i - 1] - 1e-9);
    }
}

TEST_CASE("DrawPathGCodeGenerator: travel segment produces G0 without extruding E", "[DrawPathGCodeGenerator]")
{
    // Use zero retraction so that retract/unretract don't emit G1 E lines —
    // making it easy to verify that a pure travel segment produces no extrusion.
    DynamicPrintConfig cfg = make_test_config();
    cfg.set_key_value("retraction_length", new ConfigOptionFloats({ 0.0 }));

    DrawSession session;
    session.add_layer(0.2);
    DrawLayer& l = session.layers.back();
    DrawSegment travel; travel.start = Vec2d(0, 0); travel.end = Vec2d(20, 0); travel.is_travel = true;
    l.segments.push_back(travel);

    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    std::string gcode = gen.generate(session);

    // OrcaSlicer's GCodeWriter emits G1 (with a travel feedrate) for all motion,
    // including travel moves — it does not emit G0. Verify there is at least one
    // G1 line without an E parameter (the travel segment), and with zero retraction
    // there must be no G1 E lines at all.
    bool has_g1_without_e = false;
    bool has_g1_e = false;
    std::size_t pos = 0;
    while ((pos = gcode.find("G1 ", pos)) != std::string::npos) {
        std::size_t line_end = gcode.find('\n', pos);
        if (line_end == std::string::npos) line_end = gcode.size();
        bool has_e = (gcode.find(" E", pos) < line_end);
        if (has_e)
            has_g1_e = true;
        else
            has_g1_without_e = true;
        pos = line_end + 1;
    }
    REQUIRE(has_g1_without_e);    // travel emits G1 without E
    REQUIRE(!has_g1_e);           // with zero retraction, no E moves at all
}

// ---------------------------------------------------------------------------
// TASK: Centered-coord workflow — 15×15mm box stays on the build plate
// ---------------------------------------------------------------------------
TEST_CASE("DrawPathGCodeGenerator: centered 15x15mm box with half-width offset stays on plate",
    "[DrawPathGCodeGenerator]")
{
    // Reproduce the exact workflow:
    //   1. User draws a 15×15mm square centred on the draw canvas (±7.5mm).
    //   2. apply_session_to_model() sets instance offset = (half_w, half_h) = (10, 10).
    //   3. GCode generator receives plate_origin=(0,0) and instance_offset=(10,10).
    //   4. Every G-code XY coordinate must be positive and within 0..20mm.

    DynamicPrintConfig cfg = make_test_config();
    cfg.set_key_value("retraction_length", new ConfigOptionFloats({ 0.0 }));

    DrawSession session = make_centered_box_session();

    // Plate at world origin; instance offset = half of the 20mm work area.
    const Vec2d plate_origin(0.0, 0.0);
    const Vec2d instance_offset(10.0, 10.0);

    DrawPathGCodeGenerator gen(cfg, plate_origin);
    const std::string gcode = gen.generate(session, instance_offset);

    REQUIRE_FALSE(gcode.empty());

    // Parse every G1 line that moves in XY and verify the coordinates are
    // within the expected on-plate range [2.3, 17.7] mm (7.5 ± 0.2 nozzle radius + 10 offset).
    bool found_xy = false;
    std::size_t pos = 0;
    while ((pos = gcode.find("G1 ", pos)) != std::string::npos) {
        std::size_t line_end = gcode.find('\n', pos);
        if (line_end == std::string::npos) line_end = gcode.size();
        const std::string line = gcode.substr(pos, line_end - pos);

        auto xv = extract_axis_value(line, 'X');
        auto yv = extract_axis_value(line, 'Y');

        if (xv) {
            found_xy = true;
            REQUIRE(*xv >= 2.0);   // must not be before plate start
            REQUIRE(*xv <= 18.0);  // must not exceed work area
        }
        if (yv) {
            found_xy = true;
            REQUIRE(*yv >= 2.0);
            REQUIRE(*yv <= 18.0);
        }

        pos = line_end + 1;
    }
    // At least some XY moves must have been emitted.
    REQUIRE(found_xy);
}

TEST_CASE("DrawPathGCodeGenerator: draw mode suppresses unsafe machine templates",
    "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_test_config();
    cfg.set_key_value("retraction_length", new ConfigOptionFloats({ 0.0 }));
    cfg.set_key_value("machine_start_gcode", new ConfigOptionString(
        "G28\n"
        "G1 X202 Y-3 F12000 ; purge outside draw area\n"
        "{if max_layer_z > 50}\n"
        "G1 X202 Y250\n"
        "{endif}"));
    cfg.set_key_value("machine_end_gcode", new ConfigOptionString(
        "{if max_layer_z > 50}\n"
        "G1 X202 Y250 F12000 ; cleanup\n"
        "G1 Y264.5 F3000\n"
        "M400\n"
        "{endif}"));

    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(make_centered_box_session(), Vec2d(10.0, 10.0));

    // Verify that machine start/end templates are now EXPANDED, not suppressed.
    REQUIRE(gcode.find("G28") != std::string::npos); // Start gcode should be present
    REQUIRE(gcode.find("X202 Y-3") != std::string::npos); // Purge move should be present

    // The conditional should be evaluated (max_layer_z for a 2mm box is ~0.6, so < 50).
    // The moves inside the {if} blocks should NOT appear since max_layer_z < 50.
    REQUIRE(gcode.find("X202 Y250") == std::string::npos);
    REQUIRE(gcode.find("Y264.5") == std::string::npos);
    REQUIRE(gcode.find("M400") == std::string::npos);

    // Unexpanded placeholders should NOT appear in the output.
    REQUIRE(gcode.find("{if") == std::string::npos);
    REQUIRE(gcode.find("max_layer_z") == std::string::npos);
    REQUIRE(gcode.find("{endif}") == std::string::npos);

    // Verify that draw extrusion moves are still within the expected range.
    bool found_xy = false;
    std::size_t pos = 0;
    while ((pos = gcode.find("G1 ", pos)) != std::string::npos) {
        std::size_t line_end = gcode.find('\n', pos);
        if (line_end == std::string::npos) line_end = gcode.size();
        const std::string line = gcode.substr(pos, line_end - pos);

        const auto xv = extract_axis_value(line, 'X');
        const auto yv = extract_axis_value(line, 'Y');
        const auto ev = extract_axis_value(line, 'E');

        // Only check coordinates for extrusion moves (those with E values).
        if (ev && *ev > 0.0) {
            if (xv) {
                found_xy = true;
                REQUIRE(*xv >= 2.0);
                REQUIRE(*xv <= 18.0);
            }
            if (yv) {
                found_xy = true;
                REQUIRE(*yv >= 2.0);
                REQUIRE(*yv <= 18.0);
            }
        }

        pos = line_end + 1;
    }
    REQUIRE(found_xy);

    GCodeProcessor processor;
    PrintConfig processor_config;
    processor.apply_config(processor_config);
    processor.initialize("draw-path-template-expansion.gcode");
    processor.initialize_result_moves();
    processor.process_buffer(gcode);
    processor.finalize(false);

    const GCodeProcessorResult& result = processor.get_result();
    bool found_extrusion_move = false;
    for (const GCodeProcessorResult::MoveVertex& move : result.moves) {
        if (move.delta_extruder <= 0.0f)
            continue;

        found_extrusion_move = true;
        // Draw extrusions should still be within the centered box bounds.
        REQUIRE(move.position.x() >= 2.0f);
        REQUIRE(move.position.x() <= 18.0f);
        REQUIRE(move.position.y() >= 2.0f);
        REQUIRE(move.position.y() <= 18.0f);
    }
    REQUIRE(found_extrusion_move);
}

TEST_CASE("DrawPathGCodeGenerator: profile purge extrusion is custom and does not fail plate check",
    "[DrawPathGCodeGenerator]")
{
    ScopedBBLRoleTags scoped_bbl_role_tags;

    DynamicPrintConfig cfg = make_test_config();
    cfg.set_key_value("retraction_length", new ConfigOptionFloats({ 0.0 }));
    cfg.set_key_value("machine_start_gcode", new ConfigOptionString(
        "G28\n"
        "G1 X128.5 Y-1.2 F20000 ; move to purge line outside printable area\n"
        "G92 E0\n"
        "G1 X140 Y-1.2 E6 F1200 ; purge extrusion outside printable area\n"
        "G92 E0\n"));

    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string      gcode = gen.generate(make_centered_box_session(), Vec2d(10.0, 10.0));

    REQUIRE(gcode.find("; FEATURE: Custom") != std::string::npos);
    REQUIRE(gcode.find("; FEATURE: Outer wall") != std::string::npos);
    REQUIRE(gcode.find("Y-1.2 E6") != std::string::npos);

    GCodeProcessor processor;
    PrintConfig    processor_config;
    processor.apply_config(processor_config);
    processor.initialize("draw-path-custom-purge.gcode");
    processor.initialize_result_moves();
    processor.process_buffer(gcode);

    const Pointfs printable_area { Vec2d(0.0, 0.0), Vec2d(250.0, 0.0), Vec2d(250.0, 250.0), Vec2d(0.0, 250.0) };
    REQUIRE(processor.check_multi_extruder_gcode_valid(
        1, printable_area, 250.0, Pointfs(), std::vector<Polygons>(), std::vector<double> { 250.0 },
        std::vector<int> { 1 }, std::vector<std::set<int>>()));
    processor.finalize(false);

    const GCodeProcessorResult& result = processor.get_result();
    REQUIRE(result.gcode_check_result.error_code == 0);

    bool found_custom_purge = false;
    bool found_draw_extrusion = false;
    for (const GCodeProcessorResult::MoveVertex& move : result.moves) {
        if (move.delta_extruder <= 0.0f)
            continue;

        if (move.position.y() < 0.0f) {
            found_custom_purge = true;
            REQUIRE(move.extrusion_role == erCustom);
        } else {
            found_draw_extrusion = true;
            REQUIRE(move.extrusion_role != erCustom);
            REQUIRE(move.position.x() >= 2.0f);
            REQUIRE(move.position.x() <= 18.0f);
            REQUIRE(move.position.y() >= 2.0f);
            REQUIRE(move.position.y() <= 18.0f);
        }
    }

    REQUIRE(found_custom_purge);
    REQUIRE(found_draw_extrusion);
}

TEST_CASE("DrawPathGCodeGenerator: placeholder expansion in start/end templates",
    "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_test_config();
    cfg.set_key_value("machine_start_gcode", new ConfigOptionString(
        "; Start template test\n"
        "; max_layer_z = [max_layer_z]\n"
        "G28 ; home\n"));
    cfg.set_key_value("machine_end_gcode", new ConfigOptionString(
        "; End template test\n"
        "; max_layer_z = [max_layer_z]\n"
        "{if max_layer_z < 10}; Low height detected{endif}\n"
        "M84 ; disable motors\n"));

    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(make_centered_box_session(), Vec2d::Zero());

    // Verify start template is present and expanded.
    REQUIRE(gcode.find("; Start template test") != std::string::npos);
    REQUIRE(gcode.find("G28 ; home") != std::string::npos);

    // Verify end template is present and expanded.
    REQUIRE(gcode.find("; End template test") != std::string::npos);
    REQUIRE(gcode.find("M84 ; disable motors") != std::string::npos);

    // The conditional should be evaluated (max_layer_z for a box is small).
    REQUIRE(gcode.find("; Low height detected") != std::string::npos);

    // No unexpanded placeholders should remain.
    REQUIRE(gcode.find("{if") == std::string::npos);
    REQUIRE(gcode.find("{endif}") == std::string::npos);

    // The [max_layer_z] placeholder should be replaced with an actual number.
    // We don't check the exact value, but ensure the placeholder itself is gone.
    const std::size_t start_pos = gcode.find("; max_layer_z =");
    REQUIRE(start_pos != std::string::npos);
    const std::size_t end_of_line = gcode.find('\n', start_pos);
    const std::string line = gcode.substr(start_pos, end_of_line - start_pos);
    REQUIRE(line.find("[max_layer_z]") == std::string::npos); // Should be expanded
}

TEST_CASE("DrawPathGCodeGenerator: Orca profile placeholders and filament gcodes are expanded",
    "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_test_config();
    cfg.set_key_value("machine_start_gcode", new ConfigOptionString(
        ";printer_model:[printer_model]\n"
        ";initial_filament:{filament_type[initial_extruder]}\n"
        "M140 S[bed_temperature_initial_layer_single]\n"
        "{if filament_type[initial_no_support_extruder]==\"PLA\"}M106 P3 S150{endif}\n"
        "M204 S{min(20000,max(1000,outer_wall_acceleration))}\n"
        "G1 X{print_bed_max[0]*0.5} Y-1.2 F20000\n"
        "SET_PRINT_STATS_INFO TOTAL_LAYER=[total_layer_count]\n"));
    cfg.set_key_value("filament_start_gcode", new ConfigOptionStrings({
        "; Filament start T[filament_extruder_id]\n" }));
    cfg.set_key_value("filament_end_gcode", new ConfigOptionStrings({
        "; Filament end layer [layer_num] max [max_layer_z]\n" }));
    cfg.set_key_value("machine_end_gcode", new ConfigOptionString(
        "{if max_layer_z > 50}G1 Z{min(max_layer_z+50, printable_height+0.5)} F20000{else}G1 Z100 F20000{endif}\n"
        "M84\n"));

    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(make_centered_box_session(), Vec2d::Zero());

    REQUIRE(gcode.find("; ERROR: Failed to process") == std::string::npos);
    REQUIRE(gcode.find("{if") == std::string::npos);
    REQUIRE(gcode.find("[") == std::string::npos);
    REQUIRE(gcode.find("M140 S60") != std::string::npos);
    REQUIRE(gcode.find("M106 P3 S150") != std::string::npos);
    REQUIRE(gcode.find("M204 S1000") != std::string::npos);
    REQUIRE(gcode.find("G1 X125") != std::string::npos);
    REQUIRE(gcode.find("SET_PRINT_STATS_INFO TOTAL_LAYER=1") != std::string::npos);
    REQUIRE(gcode.find("; Filament start T0") != std::string::npos);
    REQUIRE(gcode.find("; Filament end layer 1 max ") != std::string::npos);
    REQUIRE(gcode.find("G1 Z100 F20000") != std::string::npos);
    REQUIRE(gcode.find("M84") != std::string::npos);
    REQUIRE(gcode.find("draw extrude") != std::string::npos);

    GCodeProcessor processor;
    PrintConfig processor_config;
    processor.apply_config(processor_config);
    processor.initialize("draw-path-orca-profile-placeholders.gcode");
    processor.initialize_result_moves();
    processor.process_buffer(gcode);
    processor.finalize(false);
    REQUIRE(!processor.get_result().moves.empty());
}

TEST_CASE("DrawPathGCodeGenerator: plate_origin shifts all XY coordinates", "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_test_config();
    DrawSession session;
    session.add_layer(0.2);
    DrawLayer& l = session.layers.back();
    DrawSegment seg; seg.start = Vec2d(0, 0); seg.end = Vec2d(10, 0); seg.is_travel = false;
    l.segments.push_back(seg);

    // Plate origin at X=100, Y=200.
    DrawPathGCodeGenerator gen(cfg, Vec2d(100.0, 200.0));
    std::string gcode = gen.generate(session);

    // The G1 extrusion line should contain X110 (10 + 100 offset).
    REQUIRE(gcode.find("X110") != std::string::npos);
}

TEST_CASE("DrawPathGCodeGenerator: draw-path plate helpers require all objects to be draw-path objects",
    "[DrawPathGCodeGenerator]")
{
    Model model;

    ModelObject* draw_object = model.add_object("DrawPathObject", "", make_cube(1.0, 1.0, 1.0));
    ModelInstance* draw_instance = draw_object->add_instance();
    draw_instance->set_offset(Vec3d(11.0, 22.0, 0.0));
    ModelInstance* second_draw_instance = draw_object->add_instance();
    second_draw_instance->set_offset(Vec3d(33.0, 44.0, 0.0));
    draw_object->config.set_key_value("draw_path_object", new ConfigOptionBool(true));

    auto draw_session = std::make_unique<DrawSession>();
    draw_session->add_layer(0.2);
    draw_session->layers.front().segments.push_back({ Vec2d(0.0, 0.0), Vec2d(5.0, 0.0), false });
    draw_object->draw_session = std::move(draw_session);

    ModelObject* copied_draw_object = model.add_object(*draw_object);
    copied_draw_object->instances.front()->set_offset(Vec3d(55.0, 66.0, 0.0));
    copied_draw_object->delete_instance(1);

    ModelObjectPtrs objects { draw_object, copied_draw_object };
    REQUIRE(DrawPathGCodeGenerator::contains_only_draw_path_objects(objects));

    std::vector<DrawPathGCodeGenerator::BatchItem> batch = DrawPathGCodeGenerator::collect_batch(objects);
    REQUIRE(batch.size() == 3);
    REQUIRE(batch[0].first == draw_object->draw_session.get());
    REQUIRE_THAT(batch[0].second.x(), Catch::Matchers::WithinAbs(11.0, 1e-9));
    REQUIRE_THAT(batch[0].second.y(), Catch::Matchers::WithinAbs(22.0, 1e-9));
    REQUIRE(batch[1].first == draw_object->draw_session.get());
    REQUIRE_THAT(batch[1].second.x(), Catch::Matchers::WithinAbs(33.0, 1e-9));
    REQUIRE_THAT(batch[1].second.y(), Catch::Matchers::WithinAbs(44.0, 1e-9));
    REQUIRE(batch[2].first == copied_draw_object->draw_session.get());
    REQUIRE_THAT(batch[2].second.x(), Catch::Matchers::WithinAbs(55.0, 1e-9));
    REQUIRE_THAT(batch[2].second.y(), Catch::Matchers::WithinAbs(66.0, 1e-9));

    ModelObject* normal_object = model.add_object("NormalObject", "", make_cube(1.0, 1.0, 1.0));
    normal_object->add_instance();
    objects.push_back(normal_object);

    REQUIRE_FALSE(DrawPathGCodeGenerator::contains_only_draw_path_objects(objects));
    REQUIRE_FALSE(DrawPathGCodeGenerator::can_generate_for_objects(objects));
}

TEST_CASE("DrawPathGCodeGenerator: recovered legacy copies can be batch generated for export",
    "[DrawPathGCodeGenerator]")
{
    Model model;

    ModelObject* source = model.add_object("DrawPathObject", "", make_cube(1.0, 1.0, 1.0));
    source->add_instance();
    source->instances.front()->set_offset(Vec3d(10.0, 20.0, 0.0));
    source->config.set_key_value("draw_path_object", new ConfigOptionBool(true));

    auto session = std::make_unique<DrawSession>();
    session->add_layer(0.2);
    session->layers.front().segments.push_back({ Vec2d(0.0, 0.0), Vec2d(5.0, 0.0), false });
    source->draw_session = std::move(session);

    ModelObject* recovered_copy = model.add_object(*source);
    recovered_copy->instances.front()->set_offset(Vec3d(30.0, 40.0, 0.0));
    recovered_copy->draw_session = std::make_unique<DrawSession>(*source->draw_session);

    ModelObjectPtrs objects { source, recovered_copy };
    REQUIRE(DrawPathGCodeGenerator::can_generate_for_objects(objects));

    DynamicPrintConfig cfg = make_test_config();
    cfg.set_key_value("retraction_length", new ConfigOptionFloats({ 0.0 }));
    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate_batch(DrawPathGCodeGenerator::collect_batch(objects));

    REQUIRE_FALSE(gcode.empty());
    REQUIRE(gcode.find("X15") != std::string::npos);
    REQUIRE(gcode.find("Y20") != std::string::npos);
    REQUIRE(gcode.find("X35") != std::string::npos);
    REQUIRE(gcode.find("Y40") != std::string::npos);
}

TEST_CASE("DrawPathGCodeGenerator: generated G-code can be written and copied to arbitrary path",
    "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_test_config();
    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(make_test_session());
    REQUIRE_FALSE(gcode.empty());

    namespace fs = boost::filesystem;
    const fs::path source = fs::temp_directory_path() / fs::unique_path("orca_draw_export_source_%%%%-%%%%.gcode");
    const fs::path target = fs::temp_directory_path() / fs::unique_path("orca_draw_export_target_%%%%-%%%%.gcode");

    std::string write_error;
    REQUIRE(DrawPathGCodeGenerator::write_gcode_file(source.string(), gcode, &write_error));
    REQUIRE(write_error.empty());
    REQUIRE(fs::exists(source));
    REQUIRE(fs::file_size(source) == gcode.size());

    std::string copy_error;
    REQUIRE(copy_file(source.string(), target.string(), copy_error, true) == CopyFileResult::SUCCESS);
    REQUIRE(fs::exists(target));
    REQUIRE(fs::file_size(target) == gcode.size());

    fs::remove(source);
    fs::remove(target);
}

TEST_CASE("DrawPathGCodeGenerator: generated G-code can populate preview processor result",
    "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_test_config();

    DrawSession session;
    session.add_layer(0.2);
    DrawLayer& layer = session.layers.back();
    layer.segments.push_back({ Vec2d(0.0, 0.0), Vec2d(10.0, 0.0), false });
    layer.segments.push_back({ Vec2d(10.0, 0.0), Vec2d(10.0, 10.0), false });

    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(session);

    GCodeProcessor processor;
    PrintConfig processor_config;
    processor.apply_config(processor_config);
    processor.initialize("draw-path-preview.gcode");
    processor.initialize_result_moves();
    processor.process_buffer(gcode);
    processor.finalize(false);

    const GCodeProcessorResult& result = processor.get_result();

    REQUIRE(result.filename == "draw-path-preview.gcode");
    REQUIRE(result.moves.size() > 1);

    const auto extrusion_move = std::find_if(result.moves.begin(), result.moves.end(),
        [](const GCodeProcessorResult::MoveVertex& move) { return move.delta_extruder > 0.0f; });
    REQUIRE(extrusion_move != result.moves.end());
}

// ---------------------------------------------------------------------------
// Integration smoke test: multi-layer full-path validation
// ---------------------------------------------------------------------------
TEST_CASE("DrawPathGCodeGenerator: multi-layer smoke test", "[DrawPathGCodeGenerator]")
{
    // Build a 3-layer session with a mix of extrusions and travel moves.
    // Layer 0: extrude 10mm, travel 5mm, extrude 8mm
    // Layer 1: extrude 15mm, travel 10mm
    // Layer 2: extrude 5mm
    DrawSession session;

    session.add_layer(0.2);
    {
        DrawLayer& l = session.layers.back();
        DrawSegment s0; s0.start = Vec2d(0,0);  s0.end = Vec2d(10,0);  s0.is_travel = false; l.segments.push_back(s0);
        DrawSegment s1; s1.start = Vec2d(10,0); s1.end = Vec2d(15,0);  s1.is_travel = true;  l.segments.push_back(s1);
        DrawSegment s2; s2.start = Vec2d(15,0); s2.end = Vec2d(23,0);  s2.is_travel = false; l.segments.push_back(s2);
    }
    session.add_layer(0.2);
    {
        DrawLayer& l = session.layers.back();
        DrawSegment s3; s3.start = Vec2d(0,0);  s3.end = Vec2d(15,0);  s3.is_travel = false; l.segments.push_back(s3);
        DrawSegment s4; s4.start = Vec2d(15,0); s4.end = Vec2d(25,0);  s4.is_travel = true;  l.segments.push_back(s4);
    }
    session.add_layer(0.2);
    {
        DrawLayer& l = session.layers.back();
        DrawSegment s5; s5.start = Vec2d(0,0);  s5.end = Vec2d(5,0);   s5.is_travel = false; l.segments.push_back(s5);
    }

    DynamicPrintConfig cfg = make_test_config(0.4, 0.2, 1.75, 1.0);
    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    std::string gcode = gen.generate(session);

    // 1. G-code must be non-empty and contain the preamble marker.
    REQUIRE(!gcode.empty());
    REQUIRE(gcode.find("G90") != std::string::npos);   // absolute positioning from preamble

    // 2. Temperature commands must appear (nozzle heat-up from preamble).
    REQUIRE(gcode.find("M104") != std::string::npos);  // set extruder temperature

    // 3. Z values must increase monotonically across layers (non-decreasing).
    std::vector<double> zvals;
    {
        std::size_t pos = 0;
        while (pos < gcode.size()) {
            std::size_t line_end = gcode.find('\n', pos);
            if (line_end == std::string::npos) line_end = gcode.size();
            std::string line = gcode.substr(pos, line_end - pos);
            if (line.size() >= 3 && (line[0] == 'G') && (line[1] == '0' || line[1] == '1') && line[2] == ' ') {
                auto z_pos = line.find(" Z");
                if (z_pos != std::string::npos)
                    zvals.push_back(std::stod(line.substr(z_pos + 2)));
            }
            pos = line_end + 1;
        }
    }
    REQUIRE(zvals.size() >= 3);  // at least one Z per layer
    for (std::size_t i = 1; i < zvals.size(); ++i)
        REQUIRE(zvals[i] >= zvals[i - 1] - 1e-9);

    // 4. Extrusion moves: use delta-based tracking (E_new > E_prev = positive material flow).
    //    With absolute E positioning, retractions bring E down; extrusions and unretracts
    //    bring it up. We count positive-delta G1 E lines: 4 extrusion segments
    //    + N unretracts (layer changes, post-travel) — expect at least 4.
    int extrusion_like_count = 0;
    double tracked_e = 0.0;  // E is reset to 0 by G92 E0 in preamble
    {
        std::size_t pos = 0;
        while ((pos = gcode.find("G1 ", pos)) != std::string::npos) {
            std::size_t line_end = gcode.find('\n', pos);
            auto e_pos = gcode.find(" E", pos);
            if (e_pos != std::string::npos && e_pos < line_end) {
                std::size_t val_start = e_pos + 2;
                double val = std::stod(gcode.substr(val_start,
                    gcode.find_first_of(" \n\r", val_start) - val_start));
                if (val > tracked_e + 1e-9)  // positive delta = material deposited or unretracted
                    ++extrusion_like_count;
                tracked_e = val;
            }
            pos = (line_end != std::string::npos) ? line_end + 1 : std::string::npos;
        }
    }
    // 4 extrusion segments + unretracts (layer changes + post-travel moves) → at least 4
    REQUIRE(extrusion_like_count >= 4);

    // 5. Travel segments: count G1 lines without E in the main body.
    //    Session has 2 travel segments (s1, s4) — but travel may also emit an
    //    unretract G1 before it, so we just verify at least 2 G1 lines have no E.
    int travel_count = 0;
    {
        std::size_t pos = 0;
        while ((pos = gcode.find("G1 ", pos)) != std::string::npos) {
            std::size_t line_end = gcode.find('\n', pos);
            if (gcode.find(" E", pos) >= line_end)  // no E on this line
                ++travel_count;
            pos = (line_end != std::string::npos) ? line_end + 1 : std::string::npos;
        }
    }
    REQUIRE(travel_count >= 2);

    // 6. Extrusion E deltas must be positive and bounded by a sane maximum.
    //    Max theoretical E for 15mm at 0.4×0.2 nozzle/layer: ~0.5
    {
        double prev_e = 0.0;
        std::size_t pos = 0;
        while ((pos = gcode.find("G1 ", pos)) != std::string::npos) {
            std::size_t line_end = gcode.find('\n', pos);
            auto e_pos = gcode.find(" E", pos);
            if (e_pos != std::string::npos && e_pos < line_end) {
                std::size_t val_start = e_pos + 2;
                double val = std::stod(gcode.substr(val_start,
                    gcode.find_first_of(" \n\r", val_start) - val_start));
                double delta = val - prev_e;
                if (delta > 1e-9)  // positive-delta move
                    REQUIRE_THAT(delta, Catch::Matchers::WithinAbs(0.0, 1.0));  // < 1mm E per move
                prev_e = val;
            }
            pos = (line_end != std::string::npos) ? line_end + 1 : std::string::npos;
        }
    }
}

// ---------------------------------------------------------------------------
// TASK-007: Arc and Bezier G-code generation tests
// ---------------------------------------------------------------------------

// Helper: count lines in gcode that START with a given prefix.
static std::size_t count_lines_starting_with(const std::string& gcode, const std::string& prefix)
{
    std::size_t count = 0;
    std::size_t pos   = 0;
    while (pos < gcode.size()) {
        std::size_t line_end = gcode.find('\n', pos);
        if (line_end == std::string::npos) line_end = gcode.size();
        const std::string line = gcode.substr(pos, line_end - pos);
        if (line.rfind(prefix, 0) == 0)
            ++count;
        pos = line_end + 1;
    }
    return count;
}

// Build a config with draw_path_arc_output set to the given value.
static DynamicPrintConfig make_arc_config(bool native_arc)
{
    DynamicPrintConfig cfg = make_test_config();
    cfg.set_key_value("retraction_length",     new ConfigOptionFloats({ 0.0 }));
    cfg.set_key_value("draw_path_arc_output",  new ConfigOptionBool(native_arc));
    return cfg;
}

// A quarter-circle CCW arc session: from (10,0) through (7.071,7.071) to (0,10).
static DrawSession make_ccw_arc_session()
{
    constexpr double R    = 10.0;
    const double    cos45 = std::cos(M_PI / 4.0);
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(R, 0.0), Vec2d(R * cos45, R * cos45), Vec2d(0.0, R)));
    return session;
}

// The same arc but traversed CW: from (0,10) through (7.071,7.071) to (10,0).
static DrawSession make_cw_arc_session()
{
    constexpr double R    = 10.0;
    const double    cos45 = std::cos(M_PI / 4.0);
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(0.0, R), Vec2d(R * cos45, R * cos45), Vec2d(R, 0.0)));
    return session;
}

// A cubic bezier session: gentle S-curve.
static DrawSession make_bezier_session()
{
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(
        DrawSegment::make_bezier(Vec2d(0.0, 0.0), Vec2d(3.0, 6.0), Vec2d(7.0, 6.0), Vec2d(10.0, 0.0)));
    return session;
}

TEST_CASE("DrawPathGCodeGenerator: arc in compat mode emits only G1, no G2/G3", "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_arc_config(/*native_arc=*/false);
    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(make_ccw_arc_session());

    REQUIRE(count_lines_starting_with(gcode, "G2 ") == 0);
    REQUIRE(count_lines_starting_with(gcode, "G3 ") == 0);
    // Should still produce extrusion G1 lines.
    REQUIRE(count_lines_starting_with(gcode, "G1 ") > 0);
}

TEST_CASE("DrawPathGCodeGenerator: bezier in compat mode emits only G1, no G2/G3", "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_arc_config(/*native_arc=*/false);
    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(make_bezier_session());

    REQUIRE(count_lines_starting_with(gcode, "G2 ") == 0);
    REQUIRE(count_lines_starting_with(gcode, "G3 ") == 0);
}

TEST_CASE("DrawPathGCodeGenerator: compat mode arc extrusion total matches arc length within 5%",
    "[DrawPathGCodeGenerator]")
{
    // Quarter-circle R=10 → arc length = π*10/2 ≈ 15.708 mm.
    constexpr double R            = 10.0;
    constexpr double arc_length   = M_PI * R / 2.0;
    constexpr double layer_h      = 0.2;
    constexpr double nozzle_d     = 0.4;
    constexpr double filament_d   = 1.75;
    const double     e_per_mm     = (nozzle_d * layer_h) / (M_PI * (filament_d / 2.0) * (filament_d / 2.0));
    const double     expected_E   = e_per_mm * arc_length;

    DynamicPrintConfig cfg = make_arc_config(/*native_arc=*/false);
    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(make_ccw_arc_session());

    // E is absolute: the total extrusion for the arc is the maximum E seen
    // across all G1 lines (which is the last cumulative E before retraction).
    double max_extrusion_E = 0.0;
    std::size_t pos = 0;
    while ((pos = gcode.find("G1 ", pos)) != std::string::npos) {
        std::size_t line_end = gcode.find('\n', pos);
        auto e_pos = gcode.find(" E", pos);
        if (e_pos != std::string::npos && e_pos < line_end) {
            const double val = std::stod(gcode.substr(e_pos + 2,
                gcode.find_first_of(" \n\r", e_pos + 2) - (e_pos + 2)));
            if (val > max_extrusion_E)
                max_extrusion_E = val;
        }
        pos = (line_end != std::string::npos) ? line_end + 1 : std::string::npos;
    }

    REQUIRE_THAT(max_extrusion_E, Catch::Matchers::WithinRel(expected_E, 0.05));
}

TEST_CASE("DrawPathGCodeGenerator: native mode arc emits G2 or G3 command", "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_arc_config(/*native_arc=*/true);
    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(make_ccw_arc_session());

    const std::size_t g2_count = count_lines_starting_with(gcode, "G2 ");
    const std::size_t g3_count = count_lines_starting_with(gcode, "G3 ");
    REQUIRE(g2_count + g3_count >= 1);
}

TEST_CASE("DrawPathGCodeGenerator: CCW arc in native mode emits G3 (not G2)", "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_arc_config(/*native_arc=*/true);
    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(make_ccw_arc_session());

    REQUIRE(count_lines_starting_with(gcode, "G3 ") >= 1);
    REQUIRE(count_lines_starting_with(gcode, "G2 ") == 0);
}

TEST_CASE("DrawPathGCodeGenerator: CW arc in native mode emits G2 (not G3)", "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_arc_config(/*native_arc=*/true);
    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(make_cw_arc_session());

    REQUIRE(count_lines_starting_with(gcode, "G2 ") >= 1);
    REQUIRE(count_lines_starting_with(gcode, "G3 ") == 0);
}

TEST_CASE("DrawPathGCodeGenerator: bezier in native mode still falls back to G1 linearization",
    "[DrawPathGCodeGenerator]")
{
    DynamicPrintConfig cfg = make_arc_config(/*native_arc=*/true);
    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(make_bezier_session());

    // Bezier should always be linearised — no G2/G3.
    REQUIRE(count_lines_starting_with(gcode, "G2 ") == 0);
    REQUIRE(count_lines_starting_with(gcode, "G3 ") == 0);
    // Must still emit extrusion G1 lines.
    REQUIRE(count_lines_starting_with(gcode, "G1 ") > 0);
}

TEST_CASE("DrawPathGCodeGenerator: travel arc emits no extrusion (no E on G1/G3/G2 lines)",
    "[DrawPathGCodeGenerator]")
{
    constexpr double R    = 10.0;
    const double    cos45 = std::cos(M_PI / 4.0);
    DrawSession session;
    session.add_layer(0.2);
    // Travel arc: is_travel = true
    session.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(R, 0.0), Vec2d(R * cos45, R * cos45), Vec2d(0.0, R), /*is_travel=*/true));

    DynamicPrintConfig cfg = make_arc_config(/*native_arc=*/false);
    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());
    const std::string gcode = gen.generate(session);

    // With zero retraction and a travel-only arc, no positive E extrusion should occur.
    bool has_positive_e = false;
    std::size_t pos = 0;
    while ((pos = gcode.find(" E", pos)) != std::string::npos) {
        const std::size_t val_start = pos + 2;
        const double val = std::stod(gcode.substr(val_start,
            gcode.find_first_of(" \n\r", val_start) - val_start));
        if (val > 1e-9) { has_positive_e = true; break; }
        pos = val_start;
    }
    REQUIRE_FALSE(has_positive_e);
}

TEST_CASE("DrawPathGCodeGenerator: degenerate arc (collinear) in native mode falls back to G1 without crash",
    "[DrawPathGCodeGenerator]")
{
    // Collinear arc — circumcenter determinant ≈ 0 — should produce a G1 fallback.
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(0.0, 0.0), Vec2d(5.0, 0.0), Vec2d(10.0, 0.0)));

    DynamicPrintConfig cfg = make_arc_config(/*native_arc=*/true);
    DrawPathGCodeGenerator gen(cfg, Vec2d::Zero());

    REQUIRE_NOTHROW([&]{ gen.generate(session); }());

    const std::string gcode = gen.generate(session);
    // Degenerate arc → G1 fallback, no G2/G3.
    REQUIRE(count_lines_starting_with(gcode, "G2 ") == 0);
    REQUIRE(count_lines_starting_with(gcode, "G3 ") == 0);
}
