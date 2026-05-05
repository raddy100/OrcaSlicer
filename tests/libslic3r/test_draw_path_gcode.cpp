#include <catch2/catch_all.hpp>
#include "libslic3r/DrawPathGCodeGenerator.hpp"
#include "libslic3r/DrawSession.hpp"
#include "libslic3r/PrintConfig.hpp"

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

    // Nozzle geometry
    cfg.set_key_value("nozzle_diameter",  new ConfigOptionFloats({ nozzle_d }));
    cfg.set_key_value("filament_diameter", new ConfigOptionFloats({ filament_d }));

    // Temperatures
    cfg.set_key_value("nozzle_temperature",               new ConfigOptionInts({ 200 }));
    cfg.set_key_value("nozzle_temperature_initial_layer", new ConfigOptionInts({ 210 }));
    cfg.set_key_value("hot_plate_temp",                   new ConfigOptionInts({ 60 }));

    // Speeds (mm/s)
    cfg.set_key_value("outer_wall_speed",    new ConfigOptionFloat(50.0));
    cfg.set_key_value("initial_layer_speed", new ConfigOptionFloat(30.0));

    // Retraction
    cfg.set_key_value("retraction_length", new ConfigOptionFloats({ 0.8 }));
    cfg.set_key_value("retraction_speed",  new ConfigOptionFloats({ 45.0 }));

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
