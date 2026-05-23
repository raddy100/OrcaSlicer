#include <catch2/catch_all.hpp>

#include "libslic3r/DrawPathMesh.hpp"
#include "libslic3r/DrawModeFeedback.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <cmath>

using namespace Slic3r;

TEST_CASE("DrawPathMesh: connected segments produce a joined mesh", "[DrawPathMesh]")
{
    DrawSession session;
    session.add_layer(0.2);

    DrawLayer& layer = session.layers.back();
    layer.segments.push_back({ Vec2d(0.0, 0.0), Vec2d(10.0, 0.0), false });
    layer.segments.push_back({ Vec2d(10.0, 0.0), Vec2d(10.0, 10.0), false });

    TriangleMesh mesh = make_draw_path_mesh(session, 0.4);

    REQUIRE_FALSE(mesh.empty());
    REQUIRE(its_number_of_patches(mesh.its) == 1);
}

TEST_CASE("DrawPathMesh: 15x15mm box centered at draw origin has correct bounding box",
    "[DrawPathMesh]")
{
    // A square drawn in centered-coordinate draw mode: corners at ±7.5mm.
    // This simulates exactly what the user does when drawing a 15×15mm box
    // with the origin at the draw canvas centre.
    DrawSession session;
    session.add_layer(0.2);
    DrawLayer& layer = session.layers.back();
    layer.segments.push_back({ Vec2d(-7.5, -7.5), Vec2d( 7.5, -7.5), false });
    layer.segments.push_back({ Vec2d( 7.5, -7.5), Vec2d( 7.5,  7.5), false });
    layer.segments.push_back({ Vec2d( 7.5,  7.5), Vec2d(-7.5,  7.5), false });
    layer.segments.push_back({ Vec2d(-7.5,  7.5), Vec2d(-7.5, -7.5), false });

    const double nozzle_d = 0.4;
    TriangleMesh mesh = make_draw_path_mesh(session, nozzle_d);

    REQUIRE_FALSE(mesh.empty());

    // Mesh should be a single connected body (all four walls share corners).
    REQUIRE(its_number_of_patches(mesh.its) == 1);

    // Bounding box: the nozzle half-width (0.2 mm) extrudes past each wall,
    // so the mesh should span roughly ±(7.5 + nozzle_d/2) = ±7.7 mm.
    const BoundingBoxf3 bb = mesh.bounding_box();
    const double half_path = nozzle_d * 0.5;   // 0.2 mm
    const double expect_half = 7.5 + half_path; // 7.7 mm

    // Each axis should extend at least 7.5 mm from the origin (the drawn wall)
    // and no more than 7.9 mm (one full nozzle diameter past the wall).
    REQUIRE(bb.min.x() < -(7.5 - 1e-3));
    REQUIRE(bb.max.x() >  (7.5 - 1e-3));
    REQUIRE_THAT(std::abs(bb.min.x()), Catch::Matchers::WithinAbs(expect_half, 0.5));
    REQUIRE_THAT(std::abs(bb.max.x()), Catch::Matchers::WithinAbs(expect_half, 0.5));
    REQUIRE_THAT(std::abs(bb.min.y()), Catch::Matchers::WithinAbs(expect_half, 0.5));
    REQUIRE_THAT(std::abs(bb.max.y()), Catch::Matchers::WithinAbs(expect_half, 0.5));

    // The entire mesh must fit inside the draw work area (±10 mm in each axis).
    REQUIRE(bb.min.x() >= -10.0 - 1e-3);
    REQUIRE(bb.max.x() <=  10.0 + 1e-3);
    REQUIRE(bb.min.y() >= -10.0 - 1e-3);
    REQUIRE(bb.max.y() <=  10.0 + 1e-3);
}

TEST_CASE("DrawPathMesh: travel breaks remain separate material islands", "[DrawPathMesh]")
{
    DrawSession session;
    session.add_layer(0.2);

    DrawLayer& layer = session.layers.back();
    layer.segments.push_back({ Vec2d(0.0, 0.0), Vec2d(10.0, 0.0), false });
    layer.segments.push_back({ Vec2d(10.0, 0.0), Vec2d(20.0, 0.0), true });
    layer.segments.push_back({ Vec2d(20.0, 0.0), Vec2d(30.0, 0.0), false });

    TriangleMesh mesh = make_draw_path_mesh(session, 0.4);

    REQUIRE_FALSE(mesh.empty());
    REQUIRE(its_number_of_patches(mesh.its) == 2);
}

// ---------------------------------------------------------------------------
// TASK-006: Arc / Bezier mesh rendering tests
// ---------------------------------------------------------------------------

TEST_CASE("DrawPathMesh: quarter-circle arc produces non-empty single-patch mesh", "[DrawPathMesh]")
{
    constexpr double R     = 10.0;
    const double    cos45  = std::cos(M_PI / 4.0);
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(R, 0.0), Vec2d(R * cos45, R * cos45), Vec2d(0.0, R)));

    TriangleMesh mesh = make_draw_path_mesh(session, 0.4);

    REQUIRE_FALSE(mesh.empty());
    REQUIRE(its_number_of_patches(mesh.its) == 1);
}

TEST_CASE("DrawPathMesh: line + arc connected produces single-patch mesh", "[DrawPathMesh]")
{
    constexpr double R    = 10.0;
    const double    cos45 = std::cos(M_PI / 4.0);
    DrawSession session;
    session.add_layer(0.2);
    // Line ends where the arc begins.
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(R, 0.0)));
    session.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(R, 0.0), Vec2d(R * cos45, R * cos45), Vec2d(0.0, R)));

    TriangleMesh mesh = make_draw_path_mesh(session, 0.4);

    REQUIRE_FALSE(mesh.empty());
    REQUIRE(its_number_of_patches(mesh.its) == 1);
}

TEST_CASE("DrawPathMesh: travel break between two arcs produces >= 2 patches", "[DrawPathMesh]")
{
    constexpr double R    = 5.0;
    const double    cos45 = std::cos(M_PI / 4.0);
    DrawSession session;
    session.add_layer(0.2);
    // Arc 1 (extruded)
    session.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(R, 0.0), Vec2d(R * cos45, R * cos45), Vec2d(0.0, R)));
    // Travel
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(0.0, R), Vec2d(20.0, 0.0), /*travel=*/true));
    // Arc 2 (extruded)
    session.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(20.0, 0.0), Vec2d(20.0 + R * cos45, R * cos45), Vec2d(20.0 + R, 0.0)));

    TriangleMesh mesh = make_draw_path_mesh(session, 0.4);

    REQUIRE_FALSE(mesh.empty());
    REQUIRE(its_number_of_patches(mesh.its) >= 2);
}

TEST_CASE("DrawPathMesh: cubic bezier segment produces non-empty mesh", "[DrawPathMesh]")
{
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(
        DrawSegment::make_bezier(Vec2d(0.0, 0.0), Vec2d(3.0, 8.0), Vec2d(7.0, 8.0), Vec2d(10.0, 0.0)));

    TriangleMesh mesh = make_draw_path_mesh(session, 0.4);

    REQUIRE_FALSE(mesh.empty());
    REQUIRE(its_number_of_patches(mesh.its) == 1);
}

TEST_CASE("DrawPathMesh: semicircle arc bbox extends beyond chord in Y", "[DrawPathMesh]")
{
    // Semicircle: centre (0,0), R=5; from (-5,0) through (0,5) to (5,0).
    // The chord is along Y=0; the arc bulges to Y=5 (+ nozzle half-width).
    constexpr double R    = 5.0;
    constexpr double nozzle = 0.4;
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(-R, 0.0), Vec2d(0.0, R), Vec2d(R, 0.0)));

    TriangleMesh mesh = make_draw_path_mesh(session, nozzle);

    REQUIRE_FALSE(mesh.empty());
    const BoundingBoxf3 bb = mesh.bounding_box();
    // Max Y should be at least R (the arc tip), because the arc samples form a tube that
    // includes the tip point at (0, R=5).  With nozzle half-width it should be ≥ R.
    REQUIRE(bb.max.y() >= R - 1e-3);
}

