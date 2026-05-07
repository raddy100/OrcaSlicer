#include <catch2/catch_all.hpp>

#include "libslic3r/DrawModeFeedback.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"

#include <boost/filesystem/operations.hpp>

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
