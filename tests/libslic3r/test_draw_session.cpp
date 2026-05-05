#include <catch2/catch_all.hpp>

#include "libslic3r/DrawSession.hpp"

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

TEST_CASE("DrawSession: empty session", "[DrawSession]")
{
    DrawSession s;
    REQUIRE(s.is_empty());
    REQUIRE(s.layer_count() == 0);
    REQUIRE(s.active_layer == -1);
    REQUIRE(WithinAbs(0.0, 1e-9).match(s.total_height()));
    REQUIRE_FALSE(s.bounding_box().defined);
}

TEST_CASE("DrawSession: add_layer accumulates Z correctly", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2);
    s.add_layer(0.2);
    s.add_layer(0.2);

    REQUIRE(s.layer_count() == 3);
    REQUIRE(s.active_layer == 2);

    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0,  1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.2,  1e-9));
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.2,  1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.4,  1e-9));
    REQUIRE_THAT(s.layers[2].z_start, WithinAbs(0.4,  1e-9));
    REQUIRE_THAT(s.layers[2].z_end,   WithinAbs(0.6,  1e-9));

    REQUIRE_THAT(s.total_height(), WithinAbs(0.6, 1e-9));

    REQUIRE(s.layers[0].layer_index == 0);
    REQUIRE(s.layers[1].layer_index == 1);
    REQUIRE(s.layers[2].layer_index == 2);
}

TEST_CASE("DrawSession: is_empty with layers but no segments", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2);
    REQUIRE(s.is_empty()); // layers exist but no segments

    DrawSegment seg;
    seg.start      = Vec2d(0.0, 0.0);
    seg.end        = Vec2d(10.0, 0.0);
    seg.is_travel  = false;
    s.layers[0].segments.push_back(seg);

    REQUIRE_FALSE(s.is_empty());
}

TEST_CASE("DrawSession: bounding_box covers all segments", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2); // layer 0: z 0.0 → 0.2
    s.add_layer(0.3); // layer 1: z 0.2 → 0.5

    {
        DrawSegment seg;
        seg.start = Vec2d(5.0,  5.0);
        seg.end   = Vec2d(20.0, 5.0);
        seg.is_travel = false;
        s.layers[0].segments.push_back(seg);
    }
    {
        DrawSegment seg;
        seg.start = Vec2d(-2.0, 10.0);
        seg.end   = Vec2d(15.0, 30.0);
        seg.is_travel = false;
        s.layers[1].segments.push_back(seg);
    }

    BoundingBoxf3 bb = s.bounding_box();
    REQUIRE(bb.defined);

    REQUIRE_THAT(bb.min.x(), WithinAbs(-2.0, 1e-9));
    REQUIRE_THAT(bb.min.y(), WithinAbs(5.0,  1e-9));
    REQUIRE_THAT(bb.min.z(), WithinAbs(0.0,  1e-9));

    REQUIRE_THAT(bb.max.x(), WithinAbs(20.0, 1e-9));
    REQUIRE_THAT(bb.max.y(), WithinAbs(30.0, 1e-9));
    REQUIRE_THAT(bb.max.z(), WithinAbs(0.5,  1e-9));
}

TEST_CASE("DrawSession: clear resets all state", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2);
    s.add_layer(0.2);

    DrawSegment seg;
    seg.start = Vec2d(0.0, 0.0);
    seg.end   = Vec2d(5.0, 0.0);
    seg.is_travel = false;
    s.layers[0].segments.push_back(seg);

    s.clear();

    REQUIRE(s.is_empty());
    REQUIRE(s.layer_count() == 0);
    REQUIRE(s.active_layer == -1);
    REQUIRE_THAT(s.total_height(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("DrawSession: add_layer with different heights", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.3); // first layer height
    s.add_layer(0.2); // subsequent layer height

    REQUIRE(s.layer_count() == 2);
    REQUIRE_THAT(s.layers[0].layer_height(), WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[1].layer_height(), WithinAbs(0.2, 1e-9));
    REQUIRE_THAT(s.total_height(), WithinAbs(0.5, 1e-9));
}

TEST_CASE("DrawSession: DrawSegment length computation", "[DrawSession]")
{
    DrawSegment seg;
    seg.start     = Vec2d(0.0, 0.0);
    seg.end       = Vec2d(3.0, 4.0);
    seg.is_travel = false;

    REQUIRE_THAT(seg.length(), WithinAbs(5.0, 1e-9));
}

// TASK-008: Deep-copy semantics — modifying a copy must not affect the original.
TEST_CASE("DrawSession: copy constructor produces independent clone", "[DrawSession]")
{
    DrawSession original;
    original.add_layer(0.2);
    DrawSegment seg;
    seg.start = Vec2d(0, 0); seg.end = Vec2d(10, 0); seg.is_travel = false;
    original.layers[0].segments.push_back(seg);

    DrawSession copy = original; // copy constructor

    // Mutate the copy — original must be unchanged.
    copy.layers[0].segments[0].end = Vec2d(99, 99);
    copy.add_layer(0.3);

    REQUIRE(original.layer_count() == 1);
    REQUIRE_THAT(original.layers[0].segments[0].end.x(), WithinAbs(10.0, 1e-9));
}
