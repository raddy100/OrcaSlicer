#include <catch2/catch_all.hpp>

#include "libslic3r/DrawModeFeedback.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"

#include <boost/filesystem/operations.hpp>
#include <cmath>
#include <limits>
#include <memory>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

TEST_CASE("DrawModeFeedback: direction snap helpers are deterministic", "[DrawModeFeedback]")
{
    const Vec2d start(10.0, 10.0);

    Vec2d cardinal = draw_apply_direction_snap(start, Vec2d(17.0, 13.0), DrawDirectionSnapMode::Cardinal);
    REQUIRE_THAT(cardinal.x(), WithinAbs(17.0, 1e-9));
    REQUIRE_THAT(cardinal.y(), WithinAbs(10.0, 1e-9));

    cardinal = draw_apply_direction_snap(start, Vec2d(12.0, 18.0), DrawDirectionSnapMode::Cardinal);
    REQUIRE_THAT(cardinal.x(), WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(cardinal.y(), WithinAbs(18.0, 1e-9));

    const Vec2d diagonal = draw_apply_direction_snap(start, Vec2d(13.0, 16.0), DrawDirectionSnapMode::Diagonal45);
    REQUIRE_THAT((diagonal.x() - start.x()), WithinAbs((diagonal.y() - start.y()), 1e-9));
    REQUIRE(draw_segment_length_mm(start, diagonal) > 0.0);

    const Vec2d free = draw_apply_direction_snap(start, Vec2d(13.0, 16.0), DrawDirectionSnapMode::Free);
    REQUIRE_THAT(free.x(), WithinAbs(13.0, 1e-9));
    REQUIRE_THAT(free.y(), WithinAbs(16.0, 1e-9));
}

TEST_CASE("DrawModeFeedback: snap priority and typed length projection", "[DrawModeFeedback]")
{
    REQUIRE(draw_resolve_snap_mode(false, false) == DrawDirectionSnapMode::Cardinal);
    REQUIRE(draw_resolve_snap_mode(true, false) == DrawDirectionSnapMode::Diagonal45);
    REQUIRE(draw_resolve_snap_mode(true, true) == DrawDirectionSnapMode::Free);

    const Vec2d start(0.0, 0.0);
    const Vec2d horizontal = draw_project_typed_length(start, Vec2d(5.0, 0.0), 25.5);
    REQUIRE_THAT(horizontal.x(), WithinAbs(25.5, 1e-9));
    REQUIRE_THAT(horizontal.y(), WithinAbs(0.0, 1e-9));

    const Vec2d fallback = draw_project_typed_length(start, start, 12.0);
    REQUIRE_THAT(fallback.x(), WithinAbs(12.0, 1e-9));
    REQUIRE_THAT(fallback.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("DrawModeFeedback: length parsing and formatting", "[DrawModeFeedback]")
{
    REQUIRE(draw_parse_length_mm("25"));
    REQUIRE_THAT(*draw_parse_length_mm("25"), WithinAbs(25.0, 1e-9));
    REQUIRE_THAT(*draw_parse_length_mm("25.5"), WithinAbs(25.5, 1e-9));
    REQUIRE_THAT(*draw_parse_length_mm("25,5"), WithinAbs(25.5, 1e-9));
    REQUIRE_FALSE(draw_parse_length_mm(""));
    REQUIRE_FALSE(draw_parse_length_mm("0"));
    REQUIRE_FALSE(draw_parse_length_mm("-1"));
    REQUIRE_FALSE(draw_parse_length_mm("12.3.4"));

    REQUIRE(draw_format_length_mm(12.345) == "12.35 mm");
    REQUIRE(draw_format_angle_degrees(90.0) == "90.0°");
    REQUIRE(draw_format_coordinate_mm(Vec2d(-1.0, 2.5)) == "X: -1.00 mm  Y: 2.50 mm");
}

TEST_CASE("DrawModeFeedback: relative angles and segment nudging", "[DrawModeFeedback]")
{
    DrawLayer layer;
    layer.layer_index = 0;
    layer.z_start = 0.0;
    layer.z_end = 0.2;
    layer.segments.push_back({ Vec2d(0.0, 0.0), Vec2d(10.0, 0.0), false });
    layer.segments.push_back({ Vec2d(10.0, 0.0), Vec2d(10.0, 10.0), false });

    REQUIRE_THAT(draw_segment_relative_angle_degrees(layer, 0), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(draw_segment_relative_angle_degrees(layer, 1), WithinAbs(90.0, 1e-9));

    constexpr int WXK_LEFT_FOR_TEST = 314;
    constexpr int WXK_UP_FOR_TEST = 315;
    REQUIRE_THAT(draw_nudge_delta_from_key(WXK_LEFT_FOR_TEST, false).x(), WithinAbs(-0.1, 1e-9));
    REQUIRE_THAT(draw_nudge_delta_from_key(WXK_UP_FOR_TEST, true).y(), WithinAbs(1.0, 1e-9));

    const DrawSegment translated = draw_translate_segment(layer.segments.front(), Vec2d(0.1, -1.0));
    REQUIRE_THAT(translated.start.x(), WithinAbs(0.1, 1e-9));
    REQUIRE_THAT(translated.start.y(), WithinAbs(-1.0, 1e-9));
    REQUIRE_THAT(translated.end.x(), WithinAbs(10.1, 1e-9));
    REQUIRE_THAT(translated.end.y(), WithinAbs(-1.0, 1e-9));
    REQUIRE_THAT(translated.length(), WithinAbs(layer.segments.front().length(), 1e-9));
}

TEST_CASE("DrawModeFeedback: finds first non-empty restorable draw object", "[DrawModeFeedback]")
{
    Model model;

    ModelObject* normal = model.add_object("Normal", "", make_cube(10.0, 10.0, 10.0));
    normal->add_instance();

    ModelObject* empty_draw = model.add_object("EmptyDraw", "", make_cube(10.0, 10.0, 10.0));
    empty_draw->add_instance();
    empty_draw->config.set_key_value("draw_path_object", new ConfigOptionBool(true));
    empty_draw->draw_session = std::make_unique<DrawSession>();
    empty_draw->draw_session->add_layer(0.2);

    ModelObject* restorable = model.add_object("RestorableDraw", "", make_cube(10.0, 10.0, 10.0));
    restorable->add_instance();
    restorable->config.set_key_value("draw_path_object", new ConfigOptionBool(true));
    auto session = std::make_unique<DrawSession>();
    session->add_layer(0.2);
    session->layers[0].segments.push_back({ Vec2d(1.0, 2.0), Vec2d(3.0, 4.0), false });
    restorable->draw_session = std::move(session);

    int object_index = -1;
    ModelObject* found = draw_find_first_restorable_draw_object(model.objects, &object_index);
    REQUIRE(found == restorable);
    REQUIRE(object_index == 2);
}

TEST_CASE("Draw3mf: round-trip preserves draw mode display preferences", "[Draw3mf][DrawModeFeedback]")
{
    namespace fs = boost::filesystem;

    Model src_model;
    src_model.draw_mode_display_preferences.show_measurements = false;
    src_model.draw_mode_display_preferences.show_coordinates = true;

    ModelObject* obj = src_model.add_object("TestDrawPathPrefs", "", make_cube(10.0, 10.0, 10.0));
    obj->add_instance();
    obj->config.set_key_value("draw_path_object", new ConfigOptionBool(true));

    auto session = std::make_unique<DrawSession>();
    session->add_layer(0.2);
    session->layers[0].segments.push_back({ Vec2d(0.0, 0.0), Vec2d(10.0, 0.0), false });
    obj->draw_session = std::move(session);

    const fs::path tmp = fs::temp_directory_path() / "orca_test_draw_display_preferences.3mf";
    fs::remove(tmp);
    const std::string tmp_string = tmp.string();

    DynamicPrintConfig store_cfg;
    StoreParams sp;
    sp.path = tmp_string.c_str();
    sp.model = &src_model;
    sp.config = &store_cfg;
    REQUIRE(store_bbs_3mf(sp));

    Model dst_model;
    DynamicPrintConfig dst_cfg;
    ConfigSubstitutionContext ctx{ ForwardCompatibilitySubstitutionRule::Disable };
    PlateDataPtrs plate_data;
    bool is_bbl = false;
    bool is_orca = false;
    Semver ver;

    const bool loaded = load_bbs_3mf(
        tmp_string.c_str(),
        &dst_cfg, &ctx, &dst_model,
        &plate_data,
        nullptr,
        &is_bbl, &is_orca, &ver,
        nullptr,
        LoadStrategy::LoadModel);

    fs::remove(tmp);
    release_PlateData_list(plate_data);

    REQUIRE(loaded);
    REQUIRE_FALSE(dst_model.draw_mode_display_preferences.show_measurements);
    REQUIRE(dst_model.draw_mode_display_preferences.show_coordinates);
    REQUIRE(dst_model.objects.size() == 1);
    REQUIRE(dst_model.objects.front()->draw_session != nullptr);
}

TEST_CASE("DrawModeFeedback: zoom factor clamping for 2-20 mm work", "[DrawModeFeedback]")
{
    REQUIRE_THAT(DRAW_MODE_DEFAULT_ZOOM_FACTOR, WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(DRAW_MODE_MIN_ZOOM_FACTOR, WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(DRAW_MODE_MAX_ZOOM_FACTOR, WithinAbs(10.0, 1e-9));

    REQUIRE_THAT(draw_clamp_zoom_factor(0.1), WithinAbs(DRAW_MODE_MIN_ZOOM_FACTOR, 1e-9));
    REQUIRE_THAT(draw_clamp_zoom_factor(DRAW_MODE_DEFAULT_ZOOM_FACTOR), WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(draw_clamp_zoom_factor(5.0), WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(draw_clamp_zoom_factor(15.0), WithinAbs(DRAW_MODE_MAX_ZOOM_FACTOR, 1e-9));
    REQUIRE_THAT(draw_clamp_zoom_factor(std::numeric_limits<double>::infinity()), WithinAbs(DRAW_MODE_DEFAULT_ZOOM_FACTOR, 1e-9));
}

TEST_CASE("DrawModeFeedback: scale indicator shows 10 mm default grid spacing", "[DrawModeFeedback]")
{
    REQUIRE_THAT(DRAW_MODE_SCALE_GRID_MM, WithinAbs(10.0, 1e-9));

    constexpr double plate_w = 256.0;
    constexpr double canvas_inner = 512.0;

    // At zoom=1.0, 256mm plate with 512px canvas inner width:
    // 10mm should be 10/256 * 512 = 20 pixels.
    REQUIRE(draw_scale_bar_length_pixels(DRAW_MODE_SCALE_GRID_MM, plate_w, 1.0, canvas_inner) == 20);

    // At zoom=2.0, visible width is 128mm:
    // 10mm should be 10/128 * 512 = 40 pixels.
    REQUIRE(draw_scale_bar_length_pixels(DRAW_MODE_SCALE_GRID_MM, plate_w, 2.0, canvas_inner) == 40);
    REQUIRE(draw_scale_bar_length_pixels(DRAW_MODE_SCALE_GRID_MM, plate_w, 0.5, canvas_inner) == 10);
    REQUIRE(draw_scale_bar_length_pixels(DRAW_MODE_SCALE_GRID_MM, plate_w, 10.0, canvas_inner) == 200);
    REQUIRE(draw_scale_bar_length_pixels(DRAW_MODE_SCALE_GRID_MM, plate_w, 1.0, 0.0) == 0);
}

TEST_CASE("DrawModeFeedback: pan bounds keep zoomed plate reachable", "[DrawModeFeedback]")
{
    const Vec2d centered_zoomed_out = draw_clamp_pan_offset(Vec2d(20.0, 20.0), 256.0, 256.0, 0.5);
    REQUIRE_THAT(centered_zoomed_out.x(), WithinAbs(-128.0, 1e-9));
    REQUIRE_THAT(centered_zoomed_out.y(), WithinAbs(-128.0, 1e-9));

    const Vec2d upper_left = draw_clamp_pan_offset(Vec2d(-10.0, -10.0), 256.0, 256.0, 2.0);
    REQUIRE_THAT(upper_left.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(upper_left.y(), WithinAbs(0.0, 1e-9));

    const Vec2d lower_right = draw_clamp_pan_offset(Vec2d(300.0, 300.0), 256.0, 256.0, 2.0);
    REQUIRE_THAT(lower_right.x(), WithinAbs(128.0, 1e-9));
    REQUIRE_THAT(lower_right.y(), WithinAbs(128.0, 1e-9));
}

// ---------------------------------------------------------------------------
// TASK-003 + TASK-004: Path sampling and updated translate/display helpers
// ---------------------------------------------------------------------------

TEST_CASE("DrawPathSampling: line segment returns exactly 2 points", "[DrawPathSampling]")
{
    const DrawSegment seg = DrawSegment::make_line(Vec2d(1.0, 2.0), Vec2d(7.0, 6.0));
    const std::vector<Vec2d> pts = draw_sample_segment(seg);
    REQUIRE(pts.size() == 2);
    REQUIRE_THAT(pts.front().x(), WithinAbs(1.0, 1e-12));
    REQUIRE_THAT(pts.front().y(), WithinAbs(2.0, 1e-12));
    REQUIRE_THAT(pts.back().x(),  WithinAbs(7.0, 1e-12));
    REQUIRE_THAT(pts.back().y(),  WithinAbs(6.0, 1e-12));
}

TEST_CASE("DrawPathSampling: 90-degree CCW arc - first==start, last==end, all on circle", "[DrawPathSampling]")
{
    // Centre (0,0), R=10; from (10,0) through (7.071,7.071) to (0,10) — CCW quarter circle.
    constexpr double R = 10.0;
    const double cos45 = std::cos(M_PI / 4.0);
    const DrawSegment seg = DrawSegment::make_arc(
        Vec2d(R, 0.0), Vec2d(R * cos45, R * cos45), Vec2d(0.0, R));

    const std::vector<Vec2d> pts = draw_sample_segment(seg, 0.01);
    REQUIRE(pts.size() >= 2);

    // First and last must be exact.
    REQUIRE_THAT(pts.front().x(), WithinAbs(R,   1e-10));
    REQUIRE_THAT(pts.front().y(), WithinAbs(0.0, 1e-10));
    REQUIRE_THAT(pts.back().x(),  WithinAbs(0.0, 1e-10));
    REQUIRE_THAT(pts.back().y(),  WithinAbs(R,   1e-10));

    // All intermediate points should lie on the circle within tolerance.
    for (const Vec2d& p : pts) {
        REQUIRE(std::isfinite(p.x()));
        REQUIRE(std::isfinite(p.y()));
        REQUIRE_THAT(p.norm(), WithinAbs(R, 0.02)); // within 2 × chord-tol of circle
    }
}

TEST_CASE("DrawPathSampling: 180-degree arc - first==start, last==end", "[DrawPathSampling]")
{
    // Centre (0,5), R=5; from (0,0) through (5,5) to (0,10) — CCW semicircle.
    const DrawSegment seg = DrawSegment::make_arc(Vec2d(0.0, 0.0), Vec2d(5.0, 5.0), Vec2d(0.0, 10.0));
    const std::vector<Vec2d> pts = draw_sample_segment(seg, 0.05);
    REQUIRE(pts.size() >= 2);
    REQUIRE_THAT(pts.front().x(), WithinAbs(0.0,  1e-10));
    REQUIRE_THAT(pts.front().y(), WithinAbs(0.0,  1e-10));
    REQUIRE_THAT(pts.back().x(),  WithinAbs(0.0,  1e-10));
    REQUIRE_THAT(pts.back().y(),  WithinAbs(10.0, 1e-10));
}

TEST_CASE("DrawPathSampling: CCW arc has CCW point order", "[DrawPathSampling]")
{
    // Arc from (10,0) through (7.071,7.071) to (0,10): CCW — x decreases, y increases.
    constexpr double R = 10.0;
    const double cos45 = std::cos(M_PI / 4.0);
    const DrawSegment seg = DrawSegment::make_arc(
        Vec2d(R, 0.0), Vec2d(R * cos45, R * cos45), Vec2d(0.0, R));

    const std::vector<Vec2d> pts = draw_sample_segment(seg, 0.05);
    REQUIRE(pts.size() >= 3);

    // For CCW quarter circle from (10,0) to (0,10): x decreases overall.
    REQUIRE(pts.front().x() > pts.back().x());
    REQUIRE(pts.front().y() < pts.back().y());

    // Through-point (the middle arc point) should have positive x and y.
    const Vec2d& mid = pts[pts.size() / 2];
    REQUIRE(mid.x() > 0.0);
    REQUIRE(mid.y() > 0.0);
}

TEST_CASE("DrawPathSampling: CW arc has CW point order", "[DrawPathSampling]")
{
    // Arc from (0,10) through (7.071,7.071) to (10,0): CW — x increases, y decreases.
    constexpr double R = 10.0;
    const double cos45 = std::cos(M_PI / 4.0);
    const DrawSegment seg = DrawSegment::make_arc(
        Vec2d(0.0, R), Vec2d(R * cos45, R * cos45), Vec2d(R, 0.0));

    const std::vector<Vec2d> pts = draw_sample_segment(seg, 0.05);
    REQUIRE(pts.size() >= 2);
    REQUIRE_THAT(pts.front().x(), WithinAbs(0.0, 1e-10));
    REQUIRE_THAT(pts.front().y(), WithinAbs(R,   1e-10));
    REQUIRE_THAT(pts.back().x(),  WithinAbs(R,   1e-10));
    REQUIRE_THAT(pts.back().y(),  WithinAbs(0.0, 1e-10));
    // x should increase going from start to end.
    REQUIRE(pts.back().x() > pts.front().x());
}

TEST_CASE("DrawPathSampling: bezier returns first==start, last==end, intermediates finite", "[DrawPathSampling]")
{
    const DrawSegment seg = DrawSegment::make_bezier(
        Vec2d(0.0, 0.0), Vec2d(3.0, 8.0), Vec2d(7.0, 8.0), Vec2d(10.0, 0.0));

    const std::vector<Vec2d> pts = draw_sample_segment(seg, 0.05);
    REQUIRE(pts.size() >= 2);
    REQUIRE_THAT(pts.front().x(), WithinAbs(0.0,  1e-10));
    REQUIRE_THAT(pts.front().y(), WithinAbs(0.0,  1e-10));
    REQUIRE_THAT(pts.back().x(),  WithinAbs(10.0, 1e-10));
    REQUIRE_THAT(pts.back().y(),  WithinAbs(0.0,  1e-10));

    for (const Vec2d& p : pts) {
        REQUIRE(std::isfinite(p.x()));
        REQUIRE(std::isfinite(p.y()));
    }
}

TEST_CASE("DrawPathSampling: tighter tolerance produces >= points as looser", "[DrawPathSampling]")
{
    const DrawSegment seg = DrawSegment::make_arc(
        Vec2d(10.0, 0.0), Vec2d(0.0, 10.0), Vec2d(-10.0, 0.0)); // semicircle

    const auto pts_tight = draw_sample_segment(seg, 0.005);
    const auto pts_loose = draw_sample_segment(seg, 0.5);
    REQUIRE(pts_tight.size() >= pts_loose.size());
}

TEST_CASE("DrawPathSampling: degenerate arc (collinear) returns {start, end} without crash", "[DrawPathSampling]")
{
    // All three points on a horizontal line → collinear → degenerate.
    const DrawSegment seg = DrawSegment::make_arc(Vec2d(0, 0), Vec2d(5, 0), Vec2d(10, 0));
    const std::vector<Vec2d> pts = draw_sample_segment(seg);
    REQUIRE(pts.size() == 2);
    REQUIRE_THAT(pts.front().x(), WithinAbs(0.0,  1e-10));
    REQUIRE_THAT(pts.back().x(),  WithinAbs(10.0, 1e-10));

    // Same point (fully degenerate).
    const DrawSegment seg2 = DrawSegment::make_arc(Vec2d(3, 3), Vec2d(3, 3), Vec2d(3, 3));
    REQUIRE_NOTHROW(draw_sample_segment(seg2));
}

TEST_CASE("DrawPathSampling: sampled length for line equals chord length", "[DrawPathSampling]")
{
    const DrawSegment seg = DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(3.0, 4.0));
    REQUIRE_THAT(draw_segment_sampled_length(seg), WithinAbs(5.0, 1e-9));
}

TEST_CASE("DrawPathSampling: sampled length for 90-degree arc of R=10 is close to PI*10/2", "[DrawPathSampling]")
{
    constexpr double R        = 10.0;
    constexpr double expected = M_PI * R / 2.0; // 90° arc length
    const double cos45 = std::cos(M_PI / 4.0);
    const DrawSegment seg = DrawSegment::make_arc(
        Vec2d(R, 0.0), Vec2d(R * cos45, R * cos45), Vec2d(0.0, R));

    const double length = draw_segment_sampled_length(seg, 0.001);
    REQUIRE_THAT(length, WithinAbs(expected, expected * 0.01)); // within 1%
}

// ---------------------------------------------------------------------------
// TASK-004: translate_segment and draw_display_length_mm for arc/bezier
// ---------------------------------------------------------------------------

TEST_CASE("DrawModeFeedback: translate_segment moves all points for Line", "[DrawModeFeedback]")
{
    const DrawSegment seg = DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(5.0, 0.0));
    const Vec2d delta(3.0, -1.0);
    const DrawSegment t = draw_translate_segment(seg, delta);
    REQUIRE_THAT(t.start.x(), WithinAbs(3.0, 1e-12));
    REQUIRE_THAT(t.start.y(), WithinAbs(-1.0, 1e-12));
    REQUIRE_THAT(t.end.x(),   WithinAbs(8.0, 1e-12));
    REQUIRE_THAT(t.end.y(),   WithinAbs(-1.0, 1e-12));
}

TEST_CASE("DrawModeFeedback: translate_segment moves start/ctrl1/end for CircularArc", "[DrawModeFeedback]")
{
    const DrawSegment seg = DrawSegment::make_arc(Vec2d(10.0, 0.0), Vec2d(7.07, 7.07), Vec2d(0.0, 10.0));
    const Vec2d delta(2.0, 1.0);
    const DrawSegment t = draw_translate_segment(seg, delta);

    REQUIRE(t.type == DrawSegmentType::CircularArc);
    REQUIRE_THAT(t.start.x(),  WithinAbs(12.0,  1e-9));
    REQUIRE_THAT(t.start.y(),  WithinAbs(1.0,   1e-9));
    REQUIRE_THAT(t.ctrl1.x(), WithinAbs(9.07,  1e-9));
    REQUIRE_THAT(t.ctrl1.y(), WithinAbs(8.07,  1e-9));
    REQUIRE_THAT(t.end.x(),   WithinAbs(2.0,   1e-9));
    REQUIRE_THAT(t.end.y(),   WithinAbs(11.0,  1e-9));
}

TEST_CASE("DrawModeFeedback: translate_segment moves start/ctrl1/ctrl2/end for CubicBezier", "[DrawModeFeedback]")
{
    const DrawSegment seg = DrawSegment::make_bezier(
        Vec2d(0.0, 0.0), Vec2d(3.0, 8.0), Vec2d(7.0, 8.0), Vec2d(10.0, 0.0));
    const Vec2d delta(-1.0, 2.0);
    const DrawSegment t = draw_translate_segment(seg, delta);

    REQUIRE(t.type == DrawSegmentType::CubicBezier);
    REQUIRE_THAT(t.start.x(), WithinAbs(-1.0, 1e-12));
    REQUIRE_THAT(t.start.y(), WithinAbs(2.0,  1e-12));
    REQUIRE_THAT(t.ctrl1.x(), WithinAbs(2.0,  1e-12));
    REQUIRE_THAT(t.ctrl1.y(), WithinAbs(10.0, 1e-12));
    REQUIRE_THAT(t.ctrl2.x(), WithinAbs(6.0,  1e-12));
    REQUIRE_THAT(t.ctrl2.y(), WithinAbs(10.0, 1e-12));
    REQUIRE_THAT(t.end.x(),   WithinAbs(9.0,  1e-12));
    REQUIRE_THAT(t.end.y(),   WithinAbs(2.0,  1e-12));
}

TEST_CASE("DrawModeFeedback: draw_display_length_mm for line matches chord norm", "[DrawModeFeedback]")
{
    const DrawSegment seg = DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(3.0, 4.0));
    REQUIRE_THAT(draw_display_length_mm(seg), WithinAbs(5.0, 1e-9));
}

TEST_CASE("DrawModeFeedback: draw_display_length_mm for 90-degree arc of R=10 is close to PI*5", "[DrawModeFeedback]")
{
    constexpr double R        = 10.0;
    constexpr double expected = M_PI * R / 2.0;
    const double cos45 = std::cos(M_PI / 4.0);
    const DrawSegment seg = DrawSegment::make_arc(
        Vec2d(R, 0.0), Vec2d(R * cos45, R * cos45), Vec2d(0.0, R));
    const double len = draw_display_length_mm(seg);
    REQUIRE_THAT(len, WithinAbs(expected, expected * 0.01));
}
