#include <catch2/catch_all.hpp>

#include "libslic3r/DrawPathMesh.hpp"
#include "libslic3r/TriangleMesh.hpp"

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

