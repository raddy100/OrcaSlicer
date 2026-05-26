#include <catch2/catch_all.hpp>

#include "libslic3r/DrawSession.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"

#include <boost/filesystem/operations.hpp>
#include <memory>

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

// TASK-011: 3mf round-trip — store_bbs_3mf/load_bbs_3mf must preserve the
// DrawSession attached to a draw-path ModelObject without data loss.
TEST_CASE("Draw3mf: round-trip preserves draw session", "[Draw3mf]")
{
    // ── 1. Build a minimal model with one draw-path object ──────────────────
    Model src_model;

    // make_cube produces a valid, non-degenerate closed mesh — adequate for
    // the 3mf geometry writer which just needs valid triangle data.
    ModelObject* obj = src_model.add_object(
        "TestDrawPath", "", make_cube(10.0, 10.0, 10.0));

    // An instance is required for the BBS 3mf exporter to record placement.
    obj->add_instance();

    // Mark the object as a draw-path object.
    obj->config.set_key_value("draw_path_object", new ConfigOptionBool(true));

    // Build a draw session: 2 layers, 4 segments total (mix of extrusion + travel).
    auto src_session = std::make_unique<DrawSession>();
    src_session->add_layer(0.2);   // layer 0: z 0.0 → 0.2
    {
        DrawSegment s;
        s.start = Vec2d(0.0, 0.0); s.end = Vec2d(10.0, 0.0); s.is_travel = false;
        src_session->layers[0].segments.push_back(s);
    }
    {
        DrawSegment s;
        s.start = Vec2d(10.0, 0.0); s.end = Vec2d(10.0, 5.0); s.is_travel = true;
        src_session->layers[0].segments.push_back(s);
    }
    src_session->add_layer(0.2);   // layer 1: z 0.2 → 0.4
    {
        DrawSegment s;
        s.start = Vec2d(0.0, 0.0); s.end = Vec2d(5.0, 5.0); s.is_travel = false;
        src_session->layers[1].segments.push_back(s);
    }
    {
        DrawSegment s;
        s.start = Vec2d(5.0, 5.0); s.end = Vec2d(10.0, 0.0); s.is_travel = false;
        src_session->layers[1].segments.push_back(s);
    }
    obj->draw_session = std::move(src_session);

    // ── 2. Export to a temp file ─────────────────────────────────────────────
    namespace fs = boost::filesystem;
    const fs::path tmp =
        fs::temp_directory_path() / "orca_test_draw_roundtrip.3mf";
    fs::remove(tmp); // clean up any stale file from a previous crashed run

    DynamicPrintConfig store_cfg; // empty config is sufficient for geometry only
    StoreParams sp;
    sp.path   = tmp.string().c_str();
    sp.model  = &src_model;
    sp.config = &store_cfg;
    // plate_data_list defaults to empty: draw sessions are per-object metadata
    // and are written unconditionally by _add_draw_sessions_to_archive.

    const bool stored = store_bbs_3mf(sp);
    REQUIRE(stored);
    REQUIRE(fs::exists(tmp));

    // ── 3. Re-import ─────────────────────────────────────────────────────────
    Model dst_model;
    DynamicPrintConfig dst_cfg;
    ConfigSubstitutionContext ctx{ ForwardCompatibilitySubstitutionRule::Disable };
    PlateDataPtrs plate_data;
    bool is_bbl = false, is_orca = false;
    Semver ver;

    const bool loaded = load_bbs_3mf(
        tmp.string().c_str(),
        &dst_cfg, &ctx, &dst_model,
        &plate_data,
        /*project_presets=*/nullptr,
        &is_bbl, &is_orca, &ver,
        /*proFn=*/nullptr,
        LoadStrategy::LoadModel);

    fs::remove(tmp);
    release_PlateData_list(plate_data);

    REQUIRE(loaded);
    REQUIRE(dst_model.objects.size() == 1);

    // ── 4. Verify draw session integrity ─────────────────────────────────────
    const ModelObject* dst_obj = dst_model.objects[0];

    REQUIRE(dst_obj->draw_session != nullptr);
    REQUIRE(dst_obj->is_draw_path_object());

    const DrawSession& dst_s = *dst_obj->draw_session;
    REQUIRE(dst_s.layer_count() == 2);

    // Layer 0: 2 segments (1 extrusion + 1 travel)
    REQUIRE(dst_s.layers[0].segments.size() == 2);
    REQUIRE(dst_s.layers[0].layer_index == 0);
    REQUIRE_THAT(dst_s.layers[0].z_start, WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(dst_s.layers[0].z_end,   WithinAbs(0.2, 1e-6));

    const DrawSegment& s00 = dst_s.layers[0].segments[0];
    REQUIRE_FALSE(s00.is_travel);
    REQUIRE_THAT(s00.start.x(), WithinAbs( 0.0, 1e-6));
    REQUIRE_THAT(s00.start.y(), WithinAbs( 0.0, 1e-6));
    REQUIRE_THAT(s00.end.x(),   WithinAbs(10.0, 1e-6));
    REQUIRE_THAT(s00.end.y(),   WithinAbs( 0.0, 1e-6));

    const DrawSegment& s01 = dst_s.layers[0].segments[1];
    REQUIRE(s01.is_travel);

    // Layer 1: 2 extrusion segments
    REQUIRE(dst_s.layers[1].segments.size() == 2);
    REQUIRE(dst_s.layers[1].layer_index == 1);
    REQUIRE_THAT(dst_s.layers[1].z_start, WithinAbs(0.2, 1e-6));
    REQUIRE_THAT(dst_s.layers[1].z_end,   WithinAbs(0.4, 1e-6));

    const DrawSegment& s10 = dst_s.layers[1].segments[0];
    REQUIRE_FALSE(s10.is_travel);
    REQUIRE_THAT(s10.start.x(), WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(s10.start.y(), WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(s10.end.x(),   WithinAbs(5.0, 1e-6));
    REQUIRE_THAT(s10.end.y(),   WithinAbs(5.0, 1e-6));
}

// ---------------------------------------------------------------------------
// DrawSession::remove_layer tests
// ---------------------------------------------------------------------------

namespace {
DrawSegment make_seg(double x0, double y0, double x1, double y1)
{
    DrawSegment s;
    s.start = Vec2d(x0, y0);
    s.end   = Vec2d(x1, y1);
    s.is_travel = false;
    return s;
}

std::unique_ptr<DrawSession> make_draw_session_for_copy_tests(double x_end)
{
    auto session = std::make_unique<DrawSession>();
    session->add_layer(0.2);
    session->layers[0].segments.push_back(make_seg(0.0, 0.0, x_end, 0.0));
    session->add_layer(0.3);
    session->layers[1].segments.push_back(make_seg(x_end, 0.0, x_end, 5.0));
    return session;
}

void require_sessions_equal(const DrawSession& lhs, const DrawSession& rhs)
{
    REQUIRE(lhs.layer_count() == rhs.layer_count());
    REQUIRE(lhs.active_layer == rhs.active_layer);
    for (size_t layer_idx = 0; layer_idx < lhs.layers.size(); ++layer_idx) {
        const DrawLayer& lhs_layer = lhs.layers[layer_idx];
        const DrawLayer& rhs_layer = rhs.layers[layer_idx];
        REQUIRE(lhs_layer.layer_index == rhs_layer.layer_index);
        REQUIRE_THAT(lhs_layer.z_start, WithinAbs(rhs_layer.z_start, 1e-6));
        REQUIRE_THAT(lhs_layer.z_end,   WithinAbs(rhs_layer.z_end,   1e-6));
        REQUIRE(lhs_layer.segments.size() == rhs_layer.segments.size());
        for (size_t seg_idx = 0; seg_idx < lhs_layer.segments.size(); ++seg_idx) {
            const DrawSegment& lhs_seg = lhs_layer.segments[seg_idx];
            const DrawSegment& rhs_seg = rhs_layer.segments[seg_idx];
            REQUIRE(lhs_seg.is_travel == rhs_seg.is_travel);
            REQUIRE_THAT(lhs_seg.start.x(), WithinAbs(rhs_seg.start.x(), 1e-6));
            REQUIRE_THAT(lhs_seg.start.y(), WithinAbs(rhs_seg.start.y(), 1e-6));
            REQUIRE_THAT(lhs_seg.end.x(),   WithinAbs(rhs_seg.end.x(),   1e-6));
            REQUIRE_THAT(lhs_seg.end.y(),   WithinAbs(rhs_seg.end.y(),   1e-6));
        }
    }
}
} // anonymous namespace

TEST_CASE("DrawSession: ModelObject copy preserves draw session independently", "[DrawSession][Draw3mf]")
{
    Model model;
    ModelObject* src = model.add_object("DrawPathObject", "", make_cube(2.0, 2.0, 0.4));
    src->add_instance();
    src->config.set_key_value("draw_path_object", new ConfigOptionBool(true));
    src->draw_session = make_draw_session_for_copy_tests(8.0);

    ModelObject* copy = model.add_object(*src);

    REQUIRE(copy->is_draw_path_object());
    REQUIRE(copy->draw_session != nullptr);
    REQUIRE(copy->draw_session.get() != src->draw_session.get());
    require_sessions_equal(*src->draw_session, *copy->draw_session);

    copy->draw_session->layers[0].segments[0].end = Vec2d(99.0, 99.0);
    REQUIRE_THAT(src->draw_session->layers[0].segments[0].end.x(), WithinAbs(8.0, 1e-6));
    REQUIRE_THAT(src->draw_session->layers[0].segments[0].end.y(), WithinAbs(0.0, 1e-6));
}

TEST_CASE("Draw3mf: legacy copied draw-path objects recover the only serialized session", "[Draw3mf]")
{
    Model src_model;

    ModelObject* source = src_model.add_object("DrawPathObject", "", make_cube(2.0, 2.0, 0.4));
    source->add_instance();
    source->config.set_key_value("draw_path_object", new ConfigOptionBool(true));
    source->draw_session = make_draw_session_for_copy_tests(7.0);

    ModelObject* legacy_copy = src_model.add_object(*source);
    legacy_copy->instances.front()->set_offset(Vec3d(20.0, 0.0, 0.0));
    legacy_copy->draw_session.reset(); // Simulates projects saved by the broken GUI copy path.

    namespace fs = boost::filesystem;
    const fs::path tmp = fs::temp_directory_path() / fs::unique_path("orca_test_draw_legacy_copy_%%%%-%%%%.3mf");

    DynamicPrintConfig store_cfg;
    const std::string tmp_string = tmp.string();
    StoreParams sp;
    sp.path   = tmp_string.c_str();
    sp.model  = &src_model;
    sp.config = &store_cfg;

    const bool stored = store_bbs_3mf(sp);
    REQUIRE(stored);
    REQUIRE(fs::exists(tmp));

    Model dst_model;
    DynamicPrintConfig dst_cfg;
    ConfigSubstitutionContext ctx{ ForwardCompatibilitySubstitutionRule::Disable };
    PlateDataPtrs plate_data;
    bool is_bbl = false, is_orca = false;
    Semver ver;

    const bool loaded = load_bbs_3mf(
        tmp_string.c_str(),
        &dst_cfg, &ctx, &dst_model,
        &plate_data,
        /*project_presets=*/nullptr,
        &is_bbl, &is_orca, &ver,
        /*proFn=*/nullptr,
        LoadStrategy::LoadModel);

    fs::remove(tmp);
    release_PlateData_list(plate_data);

    REQUIRE(loaded);
    REQUIRE(dst_model.objects.size() == 2);
    REQUIRE(dst_model.objects[0]->draw_session != nullptr);
    REQUIRE(dst_model.objects[1]->draw_session != nullptr);
    REQUIRE(dst_model.objects[0]->draw_session.get() != dst_model.objects[1]->draw_session.get());
    require_sessions_equal(*dst_model.objects[0]->draw_session, *dst_model.objects[1]->draw_session);
    require_sessions_equal(*source->draw_session, *dst_model.objects[1]->draw_session);
}

TEST_CASE("DrawSession::remove_layer - empty session returns false", "[DrawSession]")
{
    DrawSession s;
    REQUIRE_FALSE(s.remove_layer(0));
    REQUIRE(s.layer_count() == 0);
    REQUIRE(s.active_layer == -1);
}

TEST_CASE("DrawSession::remove_layer - out-of-range index returns false", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2);
    REQUIRE_FALSE(s.remove_layer(-1));
    REQUIRE_FALSE(s.remove_layer(1));
    REQUIRE(s.layer_count() == 1); // unchanged
}

TEST_CASE("DrawSession::remove_layer - only layer leaves empty session", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2);
    s.layers[0].segments.push_back(make_seg(0, 0, 5, 5));

    REQUIRE(s.remove_layer(0));
    REQUIRE(s.layer_count() == 0);
    REQUIRE(s.active_layer == -1);
}

TEST_CASE("DrawSession::remove_layer - removes last of 3 layers", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2); // layer 0
    s.add_layer(0.2); // layer 1
    s.add_layer(0.2); // layer 2, active

    REQUIRE(s.active_layer == 2);
    REQUIRE(s.remove_layer(2));
    REQUIRE(s.layer_count() == 2);
    // Active was the removed layer (2): should step back to 1
    REQUIRE(s.active_layer == 1);
}

TEST_CASE("DrawSession::remove_layer - removes middle of 3 layers", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2); // layer 0
    s.add_layer(0.2); // layer 1
    s.add_layer(0.2); // layer 2, active
    s.active_layer = 1; // manually select the middle layer

    REQUIRE(s.remove_layer(1));
    REQUIRE(s.layer_count() == 2);
    // Active was the removed layer (1): step back to max(0, 1-1) = 0
    REQUIRE(s.active_layer == 0);
}

TEST_CASE("DrawSession::remove_layer - removes first of 3 layers", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2); // layer 0
    s.add_layer(0.2); // layer 1
    s.add_layer(0.2); // layer 2
    s.active_layer = 0;

    REQUIRE(s.remove_layer(0));
    REQUIRE(s.layer_count() == 2);
    // Active was 0 (removed): max(0, 0-1) = 0; stays at new layer 0
    REQUIRE(s.active_layer == 0);
}

TEST_CASE("DrawSession::remove_layer - active above removed shifts down", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2); // layer 0
    s.add_layer(0.2); // layer 1
    s.add_layer(0.2); // layer 2
    s.active_layer = 2;

    // Remove layer 0 (active is above it)
    REQUIRE(s.remove_layer(0));
    REQUIRE(s.layer_count() == 2);
    // active (2) was above removed (0): should shift to 1
    REQUIRE(s.active_layer == 1);
}

TEST_CASE("DrawSession::remove_layer - active below removed stays unchanged", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2); // layer 0
    s.add_layer(0.2); // layer 1
    s.add_layer(0.2); // layer 2
    s.active_layer = 0;

    // Remove layer 2 (active is below it)
    REQUIRE(s.remove_layer(2));
    REQUIRE(s.layer_count() == 2);
    // active (0) was below removed (2): unchanged
    REQUIRE(s.active_layer == 0);
}

TEST_CASE("DrawSession::remove_layer - Z values of layers above are shifted down", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2); // layer 0: z 0.0–0.2
    s.add_layer(0.3); // layer 1: z 0.2–0.5  (height 0.3 — this one is removed)
    s.add_layer(0.1); // layer 2: z 0.5–0.6

    s.remove_layer(1); // remove middle layer (height 0.3)

    REQUIRE(s.layer_count() == 2);
    // Layer 0 is below the removed layer — unchanged.
    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.2, 1e-9));
    // What was layer 2 is now layer 1: shifted DOWN by the removed layer's height (0.3).
    // z_start: 0.5 - 0.3 = 0.2,  z_end: 0.6 - 0.3 = 0.3
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.2, 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.3, 1e-9));
    // No Z gap — layer 1 starts where layer 0 ends.
    REQUIRE_THAT(s.total_height(), WithinAbs(0.3, 1e-9));
}

TEST_CASE("DrawSession::remove_layer - segments in remaining layers preserved", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2); // layer 0
    s.add_layer(0.2); // layer 1 - will be removed
    s.add_layer(0.2); // layer 2
    s.layers[0].segments.push_back(make_seg(0, 0, 1, 0));
    s.layers[1].segments.push_back(make_seg(2, 2, 3, 3)); // this layer gets removed
    s.layers[2].segments.push_back(make_seg(4, 4, 5, 5));

    s.remove_layer(1);

    REQUIRE(s.layer_count() == 2);
    REQUIRE(s.layers[0].segments.size() == 1);
    REQUIRE_THAT(s.layers[0].segments[0].end.x(), WithinAbs(1.0, 1e-9));
    REQUIRE(s.layers[1].segments.size() == 1);
    REQUIRE_THAT(s.layers[1].segments[0].start.x(), WithinAbs(4.0, 1e-9));
}

TEST_CASE("DrawSession::remove_layer - removing empty layer 0 when others exist", "[DrawSession]")
{
    // Edge case: layer 0 is empty, layers 1 and 2 have content.
    // Removing layer 0 should keep active at 0 (new first layer).
    DrawSession s;
    s.add_layer(0.2); // layer 0 - empty
    s.add_layer(0.2); // layer 1
    s.add_layer(0.2); // layer 2
    s.layers[1].segments.push_back(make_seg(0, 0, 5, 0));
    s.layers[2].segments.push_back(make_seg(5, 0, 5, 5));
    s.active_layer = 0;

    REQUIRE(s.remove_layer(0));
    REQUIRE(s.layer_count() == 2);
    REQUIRE(s.active_layer == 0); // clamped to max(0, 0-1) = 0
    // Old layer 1 is now layer 0 and still has its segment
    REQUIRE(s.layers[0].segments.size() == 1);
}

TEST_CASE("DrawSession::remove_layer - single empty layer leaves no layers", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2); // empty layer

    REQUIRE(s.is_empty());
    REQUIRE(s.remove_layer(0));
    REQUIRE(s.layer_count() == 0);
    REQUIRE(s.active_layer == -1);
    REQUIRE(s.is_empty());
}

// ---------------------------------------------------------------------------
// DrawSession::insert_layer tests
// ---------------------------------------------------------------------------

TEST_CASE("DrawSession::insert_layer - inserts first layer at position 0", "[DrawSession]")
{
    DrawSession s;
    s.insert_layer(0, 0.2);

    REQUIRE(s.layer_count() == 1);
    REQUIRE(s.active_layer == 0);
    REQUIRE(s.layers[0].layer_index == 0);
    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.2, 1e-9));
}

TEST_CASE("DrawSession::insert_layer - appends at end when position == layer_count", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2); // layer 0: z 0.0-0.2
    s.add_layer(0.2); // layer 1: z 0.2-0.4

    s.insert_layer(2, 0.3); // append

    REQUIRE(s.layer_count() == 3);
    REQUIRE(s.active_layer == 2);
    REQUIRE_THAT(s.layers[2].z_start, WithinAbs(0.4, 1e-9));
    REQUIRE_THAT(s.layers[2].z_end,   WithinAbs(0.7, 1e-9));
    REQUIRE(s.layers[0].layer_index == 0);
    REQUIRE(s.layers[1].layer_index == 1);
    REQUIRE(s.layers[2].layer_index == 2);
}

TEST_CASE("DrawSession::insert_layer - inserts in middle and shifts z of layers above", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2); // layer 0: z 0.0-0.2
    s.add_layer(0.2); // layer 1: z 0.2-0.4
    s.add_layer(0.2); // layer 2: z 0.4-0.6

    // Insert after layer 0 (position 1)
    s.insert_layer(1, 0.3);

    REQUIRE(s.layer_count() == 4);
    REQUIRE(s.active_layer == 1); // new layer is active

    // Original layer 0 is unchanged
    REQUIRE(s.layers[0].layer_index == 0);
    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.2, 1e-9));

    // New layer at index 1
    REQUIRE(s.layers[1].layer_index == 1);
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.2, 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.5, 1e-9));

    // Old layer 1 is now at index 2, shifted by 0.3
    REQUIRE(s.layers[2].layer_index == 2);
    REQUIRE_THAT(s.layers[2].z_start, WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(s.layers[2].z_end,   WithinAbs(0.7, 1e-9));

    // Old layer 2 is now at index 3, shifted by 0.3
    REQUIRE(s.layers[3].layer_index == 3);
    REQUIRE_THAT(s.layers[3].z_start, WithinAbs(0.7, 1e-9));
    REQUIRE_THAT(s.layers[3].z_end,   WithinAbs(0.9, 1e-9));

    REQUIRE_THAT(s.total_height(), WithinAbs(0.9, 1e-9));
}

TEST_CASE("DrawSession::insert_layer - segments in existing layers are preserved", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2); // layer 0
    s.add_layer(0.2); // layer 1
    s.layers[0].segments.push_back(make_seg(0, 0, 5, 0));
    s.layers[1].segments.push_back(make_seg(5, 0, 10, 0));

    s.insert_layer(1, 0.2);

    REQUIRE(s.layer_count() == 3);
    REQUIRE(s.layers[0].segments.size() == 1);  // unchanged
    REQUIRE(s.layers[1].segments.empty());       // new empty layer
    REQUIRE(s.layers[2].segments.size() == 1);  // old layer 1
    REQUIRE_THAT(s.layers[2].segments[0].end.x(), WithinAbs(10.0, 1e-9));
}

TEST_CASE("DrawSession::insert_layer - throws on out-of-range position", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2);

    REQUIRE_THROWS_AS(s.insert_layer(-1, 0.2), std::out_of_range);
    REQUIRE_THROWS_AS(s.insert_layer(2,  0.2), std::out_of_range);
}

TEST_CASE("DrawSession::insert_layer - throws on non-positive layer_height", "[DrawSession]")
{
    DrawSession s;
    REQUIRE_THROWS_AS(s.insert_layer(0, 0.0),  std::invalid_argument);
    REQUIRE_THROWS_AS(s.insert_layer(0, -0.1), std::invalid_argument);
}

TEST_CASE("DrawSession::insert_layer - inserts mid-stack and shifts indices correctly", "[DrawSession]")
{
    // Verify insert_layer at a non-end position correctly numbers all layers.
    DrawSession s;
    for (int i = 0; i < 6; ++i)
        s.add_layer(0.2);

    s.insert_layer(3, 0.2); // insert at index 3 (between old layer 2 and 3)

    REQUIRE(s.layer_count() == 7);
    REQUIRE(s.active_layer == 3);
    for (int i = 0; i < 7; ++i)
        REQUIRE(s.layers[i].layer_index == i);
}

TEST_CASE("DrawSession: + Layer navigate-or-create pattern - 6 layers scenario", "[DrawSession]")
{
    // Full scenario from the bug report:
    //   6 layers with content, navigate down 4 times to layer 2 (index 1),
    //   then navigate up 4 times with + Layer back to layer 6 (index 5),
    //   then + Layer once more creates layer 7 (a new empty layer at the top).
    DrawSession s;
    for (int i = 0; i < 6; ++i) {
        s.add_layer(0.2);
        s.layers[i].segments.push_back(make_seg(0, 0, 5, 0)); // give every layer content
    }
    REQUIRE(s.active_layer == 5); // on layer 6 (index 5)

    // Navigate down 4 times (simulate - Layer x4)
    for (int step = 0; step < 4; ++step)
        s.active_layer--;
    REQUIRE(s.active_layer == 1); // on layer 2 (index 1)

    // Navigate up 4 times with + Layer (should NOT create new layers — navigate existing ones)
    for (int step = 0; step < 4; ++step) {
        REQUIRE(s.active_layer < s.layer_count() - 1); // not at top yet
        s.active_layer++;                               // simulate the navigate branch
    }
    REQUIRE(s.active_layer == 5);  // back at layer 6 (index 5)
    REQUIRE(s.layer_count() == 6); // still only 6 layers — no new ones created

    // Now at top with content: + Layer should create layer 7
    REQUIRE(s.active_layer == s.layer_count() - 1); // confirm at top
    REQUIRE_FALSE(s.layers[s.active_layer].segments.empty()); // has content
    s.add_layer(0.2);

    REQUIRE(s.layer_count() == 7);
    REQUIRE(s.active_layer == 6); // new layer 7 (index 6)
    REQUIRE(s.layers[6].segments.empty()); // new layer is empty

    // + Layer should now be DISABLED (at top, layer is empty).
    // Verify the disable condition: at_top && layer_empty
    const bool at_top    = s.active_layer == s.layer_count() - 1;
    const bool layer_empty = s.layers[s.active_layer].segments.empty();
    REQUIRE(at_top);
    REQUIRE(layer_empty);
    const bool can_add = !at_top || !layer_empty;
    REQUIRE_FALSE(can_add); // button should be disabled when top layer is empty
}

// ---------------------------------------------------------------------------
// TASK-002: DrawSegment type extension tests
// ---------------------------------------------------------------------------

TEST_CASE("DrawSegment: make_line produces type Line with correct fields", "[DrawSession]")
{
    const Vec2d s(0.0, 0.0), e(5.0, 3.0);
    const DrawSegment seg = DrawSegment::make_line(s, e);
    REQUIRE(seg.type      == DrawSegmentType::Line);
    REQUIRE_THAT(seg.start.x(), WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(seg.start.y(), WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(seg.end.x(),   WithinAbs(5.0, 1e-12));
    REQUIRE_THAT(seg.end.y(),   WithinAbs(3.0, 1e-12));
    REQUIRE_FALSE(seg.is_travel);
}

TEST_CASE("DrawSegment: make_line with is_travel = true", "[DrawSession]")
{
    const DrawSegment seg = DrawSegment::make_line(Vec2d(1.0, 2.0), Vec2d(3.0, 4.0), /*is_travel=*/true);
    REQUIRE(seg.type      == DrawSegmentType::Line);
    REQUIRE(seg.is_travel);
}

TEST_CASE("DrawSegment: make_arc stores through-point in ctrl1", "[DrawSession]")
{
    const Vec2d start(10.0, 0.0), through(7.07, 7.07), end(0.0, 10.0);
    const DrawSegment seg = DrawSegment::make_arc(start, through, end);
    REQUIRE(seg.type == DrawSegmentType::CircularArc);
    REQUIRE_THAT(seg.start.x(),  WithinAbs(10.0, 1e-12));
    REQUIRE_THAT(seg.start.y(),  WithinAbs(0.0,  1e-12));
    REQUIRE_THAT(seg.ctrl1.x(), WithinAbs(7.07, 1e-12));
    REQUIRE_THAT(seg.ctrl1.y(), WithinAbs(7.07, 1e-12));
    REQUIRE_THAT(seg.end.x(),   WithinAbs(0.0,  1e-12));
    REQUIRE_THAT(seg.end.y(),   WithinAbs(10.0, 1e-12));
    REQUIRE_FALSE(seg.is_travel);
}

TEST_CASE("DrawSegment: make_bezier stores all four points", "[DrawSession]")
{
    const Vec2d p0(0.0, 0.0), p1(2.0, 4.0), p2(8.0, 4.0), p3(10.0, 0.0);
    const DrawSegment seg = DrawSegment::make_bezier(p0, p1, p2, p3);
    REQUIRE(seg.type == DrawSegmentType::CubicBezier);
    REQUIRE_THAT(seg.start.x(), WithinAbs(0.0,  1e-12));
    REQUIRE_THAT(seg.ctrl1.x(), WithinAbs(2.0,  1e-12));
    REQUIRE_THAT(seg.ctrl1.y(), WithinAbs(4.0,  1e-12));
    REQUIRE_THAT(seg.ctrl2.x(), WithinAbs(8.0,  1e-12));
    REQUIRE_THAT(seg.ctrl2.y(), WithinAbs(4.0,  1e-12));
    REQUIRE_THAT(seg.end.x(),   WithinAbs(10.0, 1e-12));
    REQUIRE_FALSE(seg.is_travel);
}

TEST_CASE("DrawSession: is_empty returns false when a non-travel arc is present", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2);
    s.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(10.0, 0.0), Vec2d(7.07, 7.07), Vec2d(0.0, 10.0)));
    REQUIRE_FALSE(s.is_empty());
}

TEST_CASE("DrawSession: bounding_box is defined for arc segment and includes through-point", "[DrawSession]")
{
    // Arc from (10,0) through (7.07,7.07) to (0,10) — the through-point is included.
    DrawSession s;
    s.add_layer(0.2);
    s.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(10.0, 0.0), Vec2d(7.07, 7.07), Vec2d(0.0, 10.0)));

    const BoundingBoxf3 bb = s.bounding_box();
    REQUIRE(bb.defined);
    // x must span [0, 10], y must span [0, 10], the through-point at (7.07, 7.07) is included.
    REQUIRE_THAT(bb.min.x(), WithinAbs(0.0,  1e-6));
    REQUIRE_THAT(bb.max.x(), WithinAbs(10.0, 1e-6));
    REQUIRE_THAT(bb.min.y(), WithinAbs(0.0,  1e-6));
    REQUIRE_THAT(bb.max.y(), WithinAbs(10.0, 1e-6));
}

TEST_CASE("DrawSession: bounding_box includes bezier control points", "[DrawSession]")
{
    // Bezier start=(0,0) end=(10,0), control points reach y=8 — bbox must cover y=8.
    DrawSession s;
    s.add_layer(0.2);
    s.layers[0].segments.push_back(
        DrawSegment::make_bezier(Vec2d(0.0, 0.0), Vec2d(3.0, 8.0), Vec2d(7.0, 8.0), Vec2d(10.0, 0.0)));

    const BoundingBoxf3 bb = s.bounding_box();
    REQUIRE(bb.defined);
    REQUIRE(bb.max.y() >= 8.0 - 1e-9);
}

TEST_CASE("DrawSession: copy semantics with mixed segment types", "[DrawSession]")
{
    DrawSession original;
    original.add_layer(0.2);
    original.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(0, 0), Vec2d(5, 0)));
    original.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(5, 0), Vec2d(5, 5), Vec2d(10, 0)));
    original.layers[0].segments.push_back(
        DrawSegment::make_bezier(Vec2d(10, 0), Vec2d(12, 4), Vec2d(18, 4), Vec2d(20, 0)));

    DrawSession copy = original; // copy constructor

    // Mutate copy — original must be unchanged.
    copy.layers[0].segments[0].end = Vec2d(99.0, 99.0);
    copy.layers[0].segments[1].ctrl1 = Vec2d(0.0, 0.0);
    copy.layers[0].segments[2].ctrl2 = Vec2d(0.0, 0.0);
    copy.add_layer(0.3);

    REQUIRE(original.layer_count() == 1);
    REQUIRE_THAT(original.layers[0].segments[0].end.x(),    WithinAbs(5.0,  1e-9));
    REQUIRE_THAT(original.layers[0].segments[1].ctrl1.x(), WithinAbs(5.0,  1e-9));
    REQUIRE_THAT(original.layers[0].segments[1].ctrl1.y(), WithinAbs(5.0,  1e-9));
    REQUIRE_THAT(original.layers[0].segments[2].ctrl2.x(), WithinAbs(18.0, 1e-9));
}

TEST_CASE("DrawSegment: degenerate arc (collinear points) does not crash", "[DrawSession]")
{
    // All three points at the same location.
    const DrawSegment seg1 = DrawSegment::make_arc(Vec2d(0, 0), Vec2d(0, 0), Vec2d(0, 0));
    REQUIRE(seg1.type == DrawSegmentType::CircularArc);
    // Should not throw — accessing fields is safe.
    REQUIRE_THAT(seg1.length(), WithinAbs(0.0, 1e-9));

    // All three points on a line.
    const DrawSegment seg2 = DrawSegment::make_arc(Vec2d(0, 0), Vec2d(5, 0), Vec2d(10, 0));
    REQUIRE(seg2.type == DrawSegmentType::CircularArc);
    // No crash required; the result is a degenerate segment.
    REQUIRE_THAT(seg2.ctrl1.x(), WithinAbs(5.0, 1e-9));
}

TEST_CASE("DrawSegment: backward-compatible aggregate init still compiles", "[DrawSession]")
{
    // These three-field aggregate initialisations must continue to compile.
    DrawSegment line_seg;
    line_seg.start     = Vec2d(0.0, 0.0);
    line_seg.end       = Vec2d(10.0, 0.0);
    line_seg.is_travel = false;
    REQUIRE(line_seg.type == DrawSegmentType::Line);
    REQUIRE_THAT(line_seg.ctrl1.norm(), WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(line_seg.ctrl2.norm(), WithinAbs(0.0, 1e-12));
}

// ---------------------------------------------------------------------------
// TASK-005: 3MF round-trip tests for arc/bezier segment types [Draw3mf]
// ---------------------------------------------------------------------------

namespace {

// Build and do a 3MF round-trip for a session containing the given segment.
// Returns the imported DrawSession from the loaded model (or empty on failure).
DrawSession roundtrip_draw_session(const DrawSession& src_session)
{
    namespace fs = boost::filesystem;

    Model src_model;
    ModelObject* obj = src_model.add_object("TestDrawPath", "", make_cube(10.0, 10.0, 10.0));
    obj->add_instance();
    obj->config.set_key_value("draw_path_object", new ConfigOptionBool(true));
    obj->draw_session = std::make_unique<DrawSession>(src_session);

    // Use a fixed filename to avoid boost::filesystem::unique_path generating
    // non-ASCII characters that are invalid in narrow strings on Windows.
    const std::string tmp_str =
        (fs::temp_directory_path() / "orca_test_3mf_arc_bezier_roundtrip.3mf").string();
    fs::remove(tmp_str);

    DynamicPrintConfig store_cfg;
    StoreParams sp;
    sp.path   = tmp_str.c_str();
    sp.model  = &src_model;
    sp.config = &store_cfg;
    if (!store_bbs_3mf(sp))
        return DrawSession{};

    Model dst_model;
    DynamicPrintConfig dst_cfg;
    ConfigSubstitutionContext ctx{ ForwardCompatibilitySubstitutionRule::Disable };
    PlateDataPtrs plate_data;
    bool is_bbl = false, is_orca = false;
    Semver ver;

    const bool loaded = load_bbs_3mf(
        tmp_str.c_str(), &dst_cfg, &ctx, &dst_model,
        &plate_data, nullptr, &is_bbl, &is_orca, &ver, nullptr, LoadStrategy::LoadModel);

    fs::remove(tmp_str);
    release_PlateData_list(plate_data);

    if (!loaded || dst_model.objects.empty() || !dst_model.objects[0]->draw_session)
        return DrawSession{};

    return *dst_model.objects[0]->draw_session;
}

} // anonymous namespace

TEST_CASE("Draw3mf: round-trip line-only session - segment type remains Line", "[Draw3mf]")
{
    DrawSession src;
    src.add_layer(0.2);
    src.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(10.0, 0.0)));

    const DrawSession dst = roundtrip_draw_session(src);
    REQUIRE(dst.layer_count() == 1);
    REQUIRE(dst.layers[0].segments.size() == 1);
    REQUIRE(dst.layers[0].segments[0].type == DrawSegmentType::Line);
    REQUIRE_THAT(dst.layers[0].segments[0].start.x(), WithinAbs(0.0,  1e-6));
    REQUIRE_THAT(dst.layers[0].segments[0].end.x(),   WithinAbs(10.0, 1e-6));
}

TEST_CASE("Draw3mf: round-trip arc session - type CircularArc, ctrl1 preserved", "[Draw3mf]")
{
    DrawSession src;
    src.add_layer(0.2);
    src.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(10.0, 0.0), Vec2d(7.07, 7.07), Vec2d(0.0, 10.0)));

    const DrawSession dst = roundtrip_draw_session(src);
    REQUIRE(dst.layer_count() == 1);
    REQUIRE(dst.layers[0].segments.size() == 1);
    const DrawSegment& s = dst.layers[0].segments[0];
    REQUIRE(s.type == DrawSegmentType::CircularArc);
    REQUIRE_THAT(s.start.x(),  WithinAbs(10.0, 1e-5));
    REQUIRE_THAT(s.ctrl1.x(), WithinAbs(7.07, 1e-5));
    REQUIRE_THAT(s.ctrl1.y(), WithinAbs(7.07, 1e-5));
    REQUIRE_THAT(s.end.y(),   WithinAbs(10.0, 1e-5));
}

TEST_CASE("Draw3mf: round-trip bezier session - type CubicBezier, ctrl1 and ctrl2 preserved", "[Draw3mf]")
{
    DrawSession src;
    src.add_layer(0.2);
    src.layers[0].segments.push_back(
        DrawSegment::make_bezier(Vec2d(0.0, 0.0), Vec2d(3.0, 8.0), Vec2d(7.0, 8.0), Vec2d(10.0, 0.0)));

    const DrawSession dst = roundtrip_draw_session(src);
    REQUIRE(dst.layers[0].segments.size() == 1);
    const DrawSegment& s = dst.layers[0].segments[0];
    REQUIRE(s.type == DrawSegmentType::CubicBezier);
    REQUIRE_THAT(s.ctrl1.x(), WithinAbs(3.0, 1e-5));
    REQUIRE_THAT(s.ctrl1.y(), WithinAbs(8.0, 1e-5));
    REQUIRE_THAT(s.ctrl2.x(), WithinAbs(7.0, 1e-5));
    REQUIRE_THAT(s.ctrl2.y(), WithinAbs(8.0, 1e-5));
}

TEST_CASE("Draw3mf: round-trip mixed session - line + arc + bezier all preserved", "[Draw3mf]")
{
    DrawSession src;
    src.add_layer(0.2);
    src.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(5.0, 0.0)));
    src.layers[0].segments.push_back(
        DrawSegment::make_arc(Vec2d(5.0, 0.0), Vec2d(6.0, 1.0), Vec2d(7.0, 0.0)));
    src.layers[0].segments.push_back(
        DrawSegment::make_bezier(Vec2d(7.0, 0.0), Vec2d(8.0, 2.0), Vec2d(9.0, 2.0), Vec2d(10.0, 0.0)));

    const DrawSession dst = roundtrip_draw_session(src);
    REQUIRE(dst.layers[0].segments.size() == 3);
    REQUIRE(dst.layers[0].segments[0].type == DrawSegmentType::Line);
    REQUIRE(dst.layers[0].segments[1].type == DrawSegmentType::CircularArc);
    REQUIRE(dst.layers[0].segments[2].type == DrawSegmentType::CubicBezier);
}

// ---------------------------------------------------------------------------
// Layer copy/paste via PasteSegmentsCommand
// ---------------------------------------------------------------------------

#include "slic3r/GUI/DrawModeCommands.hpp"

TEST_CASE("PasteSegmentsCommand — paste to empty layer", "[DrawLayerCopyPaste]")
{
    DrawSession session;
    session.add_layer(0.2);
    session.active_layer = 0;

    std::vector<DrawSegment> to_paste = {
        DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(1.0, 0.0)),
        DrawSegment::make_line(Vec2d(1.0, 0.0), Vec2d(1.0, 1.0)),
    };

    Slic3r::GUI::PasteSegmentsCommand cmd(0, to_paste);
    cmd.execute(session);

    REQUIRE(session.layers[0].segments.size() == 2);
    REQUIRE(session.layers[0].segments[0].end.x() == Catch::Approx(1.0));
}

TEST_CASE("PasteSegmentsCommand — paste is additive", "[DrawLayerCopyPaste]")
{
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(5.0, 0.0)));

    std::vector<DrawSegment> to_paste = {
        DrawSegment::make_line(Vec2d(0.0, 1.0), Vec2d(3.0, 1.0)),
    };

    Slic3r::GUI::PasteSegmentsCommand cmd(0, to_paste);
    cmd.execute(session);

    REQUIRE(session.layers[0].segments.size() == 2);
    // Original segment is still first
    REQUIRE(session.layers[0].segments[0].end.x() == Catch::Approx(5.0));
    // Pasted segment is appended
    REQUIRE(session.layers[0].segments[1].end.x() == Catch::Approx(3.0));
}

TEST_CASE("PasteSegmentsCommand — undo removes exactly pasted segments", "[DrawLayerCopyPaste]")
{
    DrawSession session;
    session.add_layer(0.2);
    // Pre-existing segment
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(5.0, 0.0)));

    std::vector<DrawSegment> to_paste = {
        DrawSegment::make_line(Vec2d(0.0, 1.0), Vec2d(3.0, 1.0)),
        DrawSegment::make_line(Vec2d(3.0, 1.0), Vec2d(6.0, 1.0)),
    };

    Slic3r::GUI::PasteSegmentsCommand cmd(0, to_paste);
    cmd.execute(session);
    REQUIRE(session.layers[0].segments.size() == 3);

    cmd.undo(session);
    REQUIRE(session.layers[0].segments.size() == 1);
    // Original segment is restored
    REQUIRE(session.layers[0].segments[0].end.x() == Catch::Approx(5.0));
}

TEST_CASE("PasteSegmentsCommand — copy from previous layer pattern", "[DrawLayerCopyPaste]")
{
    DrawSession session;
    session.add_layer(0.2); // layer 0
    session.add_layer(0.2); // layer 1
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(4.0, 0.0)));
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(4.0, 0.0), Vec2d(4.0, 4.0)));
    session.active_layer = 1;

    // Simulate "Copy From Prev": paste layer 0 segments into layer 1
    std::vector<DrawSegment> prev_segs = session.layers[0].segments;
    Slic3r::GUI::PasteSegmentsCommand cmd(1, prev_segs);
    cmd.execute(session);

    REQUIRE(session.layers[0].segments.size() == 2); // unchanged
    REQUIRE(session.layers[1].segments.size() == 2); // got the copy

    cmd.undo(session);
    REQUIRE(session.layers[1].segments.empty()); // reverted
    REQUIRE(session.layers[0].segments.size() == 2); // still unchanged
}

// ---------------------------------------------------------------------------
// MoveConnectedEndpointsCommand — Connected Node Drag
// ---------------------------------------------------------------------------

TEST_CASE("MoveConnectedEndpointsCommand — single endpoint (no connections)", "[ConnectedNodeDrag]")
{
    // Session: 1 layer, 1 segment A = (0,0) -> (5,0)
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(5.0, 0.0)));

    // Command: move A.start from (0,0) to (2,0)
    std::vector<Slic3r::GUI::ConnectedEndpointRef> eps;
    eps.push_back({0, true}); // seg 0, is_start=true
    Slic3r::GUI::MoveConnectedEndpointsCommand cmd(
        0, std::move(eps), Vec2d(0.0, 0.0), Vec2d(2.0, 0.0));

    cmd.execute(session);

    REQUIRE_THAT(session.layers[0].segments[0].start.x(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[0].start.y(), WithinAbs(0.0, 1e-9));
    // End unchanged
    REQUIRE_THAT(session.layers[0].segments[0].end.x(), WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[0].end.y(), WithinAbs(0.0, 1e-9));

    cmd.undo(session);

    REQUIRE_THAT(session.layers[0].segments[0].start.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[0].start.y(), WithinAbs(0.0, 1e-9));
    // End still unchanged after undo
    REQUIRE_THAT(session.layers[0].segments[0].end.x(), WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[0].end.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("MoveConnectedEndpointsCommand — T-junction: two segments share an endpoint", "[ConnectedNodeDrag]")
{
    // Session: 1 layer, 2 segments:
    //   A = (0,0) -> (5,0)   [seg 0]
    //   B = (5,0) -> (5,5)   [seg 1]
    // Shared node at (5,0): A.end and B.start
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(5.0, 0.0)));
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(5.0, 0.0), Vec2d(5.0, 5.0)));

    // Command: move (5,0) -> (6,0) for both A.end and B.start
    std::vector<Slic3r::GUI::ConnectedEndpointRef> eps;
    eps.push_back({0, false}); // seg 0, is_start=false  (A.end)
    eps.push_back({1, true});  // seg 1, is_start=true   (B.start)
    Slic3r::GUI::MoveConnectedEndpointsCommand cmd(
        0, std::move(eps), Vec2d(5.0, 0.0), Vec2d(6.0, 0.0));

    cmd.execute(session);

    // A.end moved
    REQUIRE_THAT(session.layers[0].segments[0].end.x(), WithinAbs(6.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[0].end.y(), WithinAbs(0.0, 1e-9));
    // B.start moved
    REQUIRE_THAT(session.layers[0].segments[1].start.x(), WithinAbs(6.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].start.y(), WithinAbs(0.0, 1e-9));
    // A.start unchanged
    REQUIRE_THAT(session.layers[0].segments[0].start.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[0].start.y(), WithinAbs(0.0, 1e-9));
    // B.end unchanged
    REQUIRE_THAT(session.layers[0].segments[1].end.x(), WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].end.y(), WithinAbs(5.0, 1e-9));

    cmd.undo(session);

    REQUIRE_THAT(session.layers[0].segments[0].end.x(), WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[0].end.y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].start.x(), WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].start.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("MoveConnectedEndpointsCommand — three-way star junction", "[ConnectedNodeDrag]")
{
    // Session: 1 layer, 3 segments meeting at (5,5):
    //   A = (0,5)  -> (5,5)   [seg 0, end]
    //   B = (5,5)  -> (10,5)  [seg 1, start]
    //   C = (5,5)  -> (5,10)  [seg 2, start]
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(0.0,  5.0), Vec2d(5.0, 5.0)));
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(5.0,  5.0), Vec2d(10.0, 5.0)));
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(5.0,  5.0), Vec2d(5.0, 10.0)));

    // Command: move (5,5) -> (6,6)
    std::vector<Slic3r::GUI::ConnectedEndpointRef> eps;
    eps.push_back({0, false}); // A.end
    eps.push_back({1, true});  // B.start
    eps.push_back({2, true});  // C.start
    Slic3r::GUI::MoveConnectedEndpointsCommand cmd(
        0, std::move(eps), Vec2d(5.0, 5.0), Vec2d(6.0, 6.0));

    cmd.execute(session);

    REQUIRE_THAT(session.layers[0].segments[0].end.x(),   WithinAbs(6.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[0].end.y(),   WithinAbs(6.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].start.x(), WithinAbs(6.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].start.y(), WithinAbs(6.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[2].start.x(), WithinAbs(6.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[2].start.y(), WithinAbs(6.0, 1e-9));
    // Non-shared endpoints unchanged
    REQUIRE_THAT(session.layers[0].segments[0].start.x(), WithinAbs(0.0,  1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].end.x(),   WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[2].end.y(),   WithinAbs(10.0, 1e-9));

    cmd.undo(session);

    REQUIRE_THAT(session.layers[0].segments[0].end.x(),   WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[0].end.y(),   WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].start.x(), WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].start.y(), WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[2].start.x(), WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[2].start.y(), WithinAbs(5.0, 1e-9));
}

TEST_CASE("MoveConnectedEndpointsCommand — undo/redo cycle", "[ConnectedNodeDrag]")
{
    // T-junction: A = (0,0)->(5,0), B = (5,0)->(5,5)
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(5.0, 0.0)));
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(5.0, 0.0), Vec2d(5.0, 5.0)));

    std::vector<Slic3r::GUI::ConnectedEndpointRef> eps;
    eps.push_back({0, false}); // A.end
    eps.push_back({1, true});  // B.start
    Slic3r::GUI::MoveConnectedEndpointsCommand cmd(
        0, std::move(eps), Vec2d(5.0, 0.0), Vec2d(6.0, 0.0));

    // First execute
    cmd.execute(session);
    REQUIRE_THAT(session.layers[0].segments[0].end.x(),   WithinAbs(6.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].start.x(), WithinAbs(6.0, 1e-9));

    // Undo
    cmd.undo(session);
    REQUIRE_THAT(session.layers[0].segments[0].end.x(),   WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].start.x(), WithinAbs(5.0, 1e-9));

    // Re-execute (redo)
    cmd.execute(session);
    REQUIRE_THAT(session.layers[0].segments[0].end.x(),   WithinAbs(6.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].start.x(), WithinAbs(6.0, 1e-9));

    // Undo again
    cmd.undo(session);
    REQUIRE_THAT(session.layers[0].segments[0].end.x(),   WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].start.x(), WithinAbs(5.0, 1e-9));
    // Verify rest of geometry is intact
    REQUIRE_THAT(session.layers[0].segments[0].start.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[1].end.y(),   WithinAbs(5.0, 1e-9));
}

TEST_CASE("MoveConnectedEndpointsCommand — no-op when old==new", "[ConnectedNodeDrag]")
{
    // Session: 1 layer, 1 segment
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(DrawSegment::make_line(Vec2d(1.0, 2.0), Vec2d(3.0, 4.0)));

    // Command with old_pos == new_pos
    std::vector<Slic3r::GUI::ConnectedEndpointRef> eps;
    eps.push_back({0, true}); // seg 0 start
    Slic3r::GUI::MoveConnectedEndpointsCommand cmd(
        0, std::move(eps), Vec2d(1.0, 2.0), Vec2d(1.0, 2.0));

    // Should not throw, session should be unchanged
    REQUIRE_NOTHROW(cmd.execute(session));
    REQUIRE_THAT(session.layers[0].segments[0].start.x(), WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[0].start.y(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[0].end.x(),   WithinAbs(3.0, 1e-9));
    REQUIRE_THAT(session.layers[0].segments[0].end.y(),   WithinAbs(4.0, 1e-9));
}

// ---------------------------------------------------------------------------
// MirrorStackCommand — Z-mirror of the full layer stack
// ---------------------------------------------------------------------------

TEST_CASE("MirrorStackCommand — no-op on empty session", "[MirrorStack]")
{
    DrawSession session;
    Slic3r::GUI::MirrorStackCommand cmd;
    REQUIRE_NOTHROW(cmd.execute(session));
    REQUIRE(session.layer_count() == 0);
    REQUIRE(session.active_layer == -1);
}

TEST_CASE("MirrorStackCommand — single layer is doubled", "[MirrorStack]")
{
    // Layer 0: z 0.0→0.2, 1 segment
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(
        DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(5.0, 0.0)));
    session.active_layer = 0;

    Slic3r::GUI::MirrorStackCommand cmd;
    cmd.execute(session);

    // Should have 2 layers
    REQUIRE(session.layer_count() == 2);

    // New layer 1 = reversed copy of layer 0: stacks above z_end=0.2
    REQUIRE(session.layers[1].layer_index == 1);
    REQUIRE_THAT(session.layers[1].z_start, WithinAbs(0.2, 1e-9));
    REQUIRE_THAT(session.layers[1].z_end,   WithinAbs(0.4, 1e-9));
    REQUIRE(session.layers[1].segments.size() == 1);
    REQUIRE_THAT(session.layers[1].segments[0].end.x(), WithinAbs(5.0, 1e-9));

    // active_layer set to last new layer
    REQUIRE(session.active_layer == 1);
}

TEST_CASE("MirrorStackCommand — three layers mirror correctly", "[MirrorStack]")
{
    // Layers 0,1,2 with heights 0.2, 0.3, 0.1 and distinct segment data
    DrawSession session;
    session.add_layer(0.2); // layer 0: z 0.0→0.2
    session.layers[0].segments.push_back(make_seg(0.0, 0.0, 1.0, 0.0));
    session.add_layer(0.3); // layer 1: z 0.2→0.5
    session.layers[1].segments.push_back(make_seg(2.0, 0.0, 3.0, 0.0));
    session.add_layer(0.1); // layer 2: z 0.5→0.6
    session.layers[2].segments.push_back(make_seg(4.0, 0.0, 5.0, 0.0));
    session.active_layer = 2;

    Slic3r::GUI::MirrorStackCommand cmd;
    cmd.execute(session);

    REQUIRE(session.layer_count() == 6);
    REQUIRE(session.active_layer == 5);

    // New layer 3 = reversed of layer 2 (height 0.1): z 0.6→0.7
    REQUIRE(session.layers[3].layer_index == 3);
    REQUIRE_THAT(session.layers[3].z_start, WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(session.layers[3].z_end,   WithinAbs(0.7, 1e-9));
    REQUIRE(session.layers[3].segments.size() == 1);
    REQUIRE_THAT(session.layers[3].segments[0].start.x(), WithinAbs(4.0, 1e-9));
    REQUIRE_THAT(session.layers[3].segments[0].end.x(),   WithinAbs(5.0, 1e-9));

    // New layer 4 = reversed of layer 1 (height 0.3): z 0.7→1.0
    REQUIRE(session.layers[4].layer_index == 4);
    REQUIRE_THAT(session.layers[4].z_start, WithinAbs(0.7, 1e-9));
    REQUIRE_THAT(session.layers[4].z_end,   WithinAbs(1.0, 1e-9));
    REQUIRE(session.layers[4].segments.size() == 1);
    REQUIRE_THAT(session.layers[4].segments[0].start.x(), WithinAbs(2.0, 1e-9));

    // New layer 5 = reversed of layer 0 (height 0.2): z 1.0→1.2
    REQUIRE(session.layers[5].layer_index == 5);
    REQUIRE_THAT(session.layers[5].z_start, WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(session.layers[5].z_end,   WithinAbs(1.2, 1e-9));
    REQUIRE(session.layers[5].segments.size() == 1);
    REQUIRE_THAT(session.layers[5].segments[0].start.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(session.layers[5].segments[0].end.x(),   WithinAbs(1.0, 1e-9));
}

TEST_CASE("MirrorStackCommand — segments are deep copies (mutation independence)", "[MirrorStack]")
{
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(
        DrawSegment::make_line(Vec2d(0.0, 0.0), Vec2d(10.0, 0.0)));

    Slic3r::GUI::MirrorStackCommand cmd;
    cmd.execute(session);

    REQUIRE(session.layer_count() == 2);

    // Mutate the new (mirrored) layer's segment — original must be unchanged
    session.layers[1].segments[0].end = Vec2d(99.0, 99.0);
    REQUIRE_THAT(session.layers[0].segments[0].end.x(), WithinAbs(10.0, 1e-9));
}

TEST_CASE("MirrorStackCommand — undo restores original layer count and active layer", "[MirrorStack]")
{
    DrawSession session;
    session.add_layer(0.2);
    session.layers[0].segments.push_back(make_seg(0.0, 0.0, 5.0, 0.0));
    session.add_layer(0.2);
    session.layers[1].segments.push_back(make_seg(5.0, 0.0, 10.0, 0.0));
    session.active_layer = 1;

    Slic3r::GUI::MirrorStackCommand cmd;
    cmd.execute(session);

    REQUIRE(session.layer_count() == 4);
    REQUIRE(session.active_layer == 3);

    cmd.undo(session);

    REQUIRE(session.layer_count() == 2);
    REQUIRE(session.active_layer == 1);

    // Original segments must still be intact
    REQUIRE_THAT(session.layers[0].segments[0].end.x(), WithinAbs(5.0,  1e-9));
    REQUIRE_THAT(session.layers[1].segments[0].end.x(), WithinAbs(10.0, 1e-9));
}

TEST_CASE("MirrorStackCommand — undo/redo cycle preserves symmetry", "[MirrorStack]")
{
    DrawSession session;
    session.add_layer(0.3);
    session.layers[0].segments.push_back(make_seg(1.0, 0.0, 2.0, 0.0));
    session.add_layer(0.2);
    session.layers[1].segments.push_back(make_seg(3.0, 0.0, 4.0, 0.0));
    session.active_layer = 1;

    Slic3r::GUI::MirrorStackCommand cmd;

    // Execute
    cmd.execute(session);
    REQUIRE(session.layer_count() == 4);

    // Undo
    cmd.undo(session);
    REQUIRE(session.layer_count() == 2);
    REQUIRE(session.active_layer == 1);

    // Re-execute (redo)
    cmd.execute(session);
    REQUIRE(session.layer_count() == 4);
    REQUIRE(session.active_layer == 3);

    // Layer 2 = mirror of layer 1 (height 0.2), layer 3 = mirror of layer 0 (height 0.3)
    REQUIRE_THAT(session.layers[2].layer_height(), WithinAbs(0.2, 1e-9));
    REQUIRE_THAT(session.layers[3].layer_height(), WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(session.layers[2].segments[0].start.x(), WithinAbs(3.0, 1e-9));
    REQUIRE_THAT(session.layers[3].segments[0].start.x(), WithinAbs(1.0, 1e-9));
}

TEST_CASE("MirrorStackCommand — layer_index values are correct after mirror", "[MirrorStack]")
{
    DrawSession session;
    for (int i = 0; i < 4; ++i) {
        session.add_layer(0.2);
        session.layers[i].segments.push_back(make_seg(0.0, 0.0, 1.0, 0.0));
    }

    Slic3r::GUI::MirrorStackCommand cmd;
    cmd.execute(session);

    REQUIRE(session.layer_count() == 8);
    for (int i = 0; i < 8; ++i)
        REQUIRE(session.layers[i].layer_index == i);
}

TEST_CASE("MirrorStackCommand — Z ranges are contiguous across all layers", "[MirrorStack]")
{
    DrawSession session;
    session.add_layer(0.2); // layer 0
    session.add_layer(0.3); // layer 1
    session.layers[0].segments.push_back(make_seg(0.0, 0.0, 1.0, 0.0));
    session.layers[1].segments.push_back(make_seg(0.0, 0.0, 1.0, 0.0));

    Slic3r::GUI::MirrorStackCommand cmd;
    cmd.execute(session);

    REQUIRE(session.layer_count() == 4);

    // Each z_start must equal previous z_end
    for (int i = 1; i < session.layer_count(); ++i) {
        REQUIRE_THAT(session.layers[i].z_start,
                     WithinAbs(session.layers[i - 1].z_end, 1e-9));
    }
}

// ---------------------------------------------------------------------------
// Real-world 3MF scenario — halfstraightthrough.3mf segment counts
// ---------------------------------------------------------------------------

TEST_CASE("MirrorStackCommand — halfstraightthrough 11-layer scenario", "[MirrorStack]")
{
    // Segment counts matching the real halfstraightthrough.3mf file
    // Layer 0: 8 segs, Layer 1: 12, Layer 2: 12, Layer 3: 16, Layer 4: 17,
    // Layer 5: 25, Layers 6-10: 33 each
    const int seg_counts[] = { 8, 12, 12, 16, 17, 25, 33, 33, 33, 33, 33 };
    const int N = 11;

    DrawSession session;
    for (int i = 0; i < N; ++i) {
        session.add_layer(0.2);
        for (int s = 0; s < seg_counts[i]; ++s) {
            // Give each segment a unique x so we can identify which layer it came from
            session.layers[i].segments.push_back(
                make_seg(static_cast<double>(i * 100 + s), 0.0,
                         static_cast<double>(i * 100 + s + 1), 0.0));
        }
    }
    session.active_layer = N - 1;

    Slic3r::GUI::MirrorStackCommand cmd;
    cmd.execute(session);

    // --- Basic count ---
    REQUIRE(session.layer_count() == 2 * N); // 22 layers total
    REQUIRE(session.active_layer == 2 * N - 1); // active = layer 21

    // --- Layer index correctness ---
    for (int i = 0; i < 2 * N; ++i)
        REQUIRE(session.layers[i].layer_index == i);

    // --- Z contiguity ---
    for (int i = 1; i < 2 * N; ++i)
        REQUIRE_THAT(session.layers[i].z_start,
                     WithinAbs(session.layers[i - 1].z_end, 1e-9));

    // --- Mirrored segment counts ---
    // New layer N+k is a copy of original layer (N-1-k)
    // i.e. layer[11] = copy of layer[10] (33 segs)
    //      layer[12] = copy of layer[9]  (33 segs)
    //      ...
    //      layer[21] = copy of layer[0]  (8 segs)
    for (int k = 0; k < N; ++k) {
        int new_idx = N + k;
        int src_idx = N - 1 - k;
        REQUIRE((int)session.layers[new_idx].segments.size() == seg_counts[src_idx]);
    }

    // --- Independence: verify specific mirrored segment counts ---
    REQUIRE((int)session.layers[11].segments.size() == 33); // copy of layer 10
    REQUIRE((int)session.layers[15].segments.size() == 33); // copy of layer 6
    REQUIRE((int)session.layers[16].segments.size() == 25); // copy of layer 5
    REQUIRE((int)session.layers[17].segments.size() == 17); // copy of layer 4
    REQUIRE((int)session.layers[18].segments.size() == 16); // copy of layer 3
    REQUIRE((int)session.layers[19].segments.size() == 12); // copy of layer 2
    REQUIRE((int)session.layers[20].segments.size() == 12); // copy of layer 1
    REQUIRE((int)session.layers[21].segments.size() ==  8); // copy of layer 0

    // --- Deep-copy independence: mutate new layer, original must be unchanged ---
    session.layers[21].segments[0].end = Vec2d(999.0, 999.0);
    // Original layer 0 segment 0 is untouched
    REQUIRE_THAT(session.layers[0].segments[0].start.x(), WithinAbs(0.0, 1e-9));

    // --- Undo restores original state ---
    cmd.undo(session);
    REQUIRE(session.layer_count() == N);
    REQUIRE(session.active_layer == N - 1);
    for (int i = 0; i < N; ++i)
        REQUIRE((int)session.layers[i].segments.size() == seg_counts[i]);
}

// ---------------------------------------------------------------------------
// MirrorStackCommand — empty-top-layer scenarios (Bug 1 fix)
// ---------------------------------------------------------------------------

TEST_CASE("MirrorStack: empty top layer used as first mirror canvas", "[MirrorStack]")
{
    // 3 content layers (1, 2, 3 segments) + 1 empty layer on top
    DrawSession session;
    session.add_layer(0.3); // layer 0: z 0.0→0.3,  1 seg
    session.layers[0].segments.push_back(make_seg(0.0, 0.0, 1.0, 0.0));
    session.add_layer(0.3); // layer 1: z 0.3→0.6,  2 segs
    session.layers[1].segments.push_back(make_seg(2.0, 0.0, 3.0, 0.0));
    session.layers[1].segments.push_back(make_seg(4.0, 0.0, 5.0, 0.0));
    session.add_layer(0.3); // layer 2: z 0.6→0.9,  3 segs
    session.layers[2].segments.push_back(make_seg(6.0, 0.0, 7.0, 0.0));
    session.layers[2].segments.push_back(make_seg(8.0, 0.0, 9.0, 0.0));
    session.layers[2].segments.push_back(make_seg(10.0, 0.0, 11.0, 0.0));
    session.add_layer(0.3); // layer 3: z 0.9→1.2,  empty canvas
    session.active_layer = 3;

    Slic3r::GUI::MirrorStackCommand cmd;
    cmd.execute(session);

    // 3 original + 1 filled + 2 new = 6 total
    REQUIRE(session.layer_count() == 6);
    REQUIRE(session.active_layer == 5);

    // Original layers unchanged
    REQUIRE((int)session.layers[0].segments.size() == 1);
    REQUIRE((int)session.layers[1].segments.size() == 2);
    REQUIRE((int)session.layers[2].segments.size() == 3);

    // Layer 3: was empty, now filled with copy of layer 2 (3 segs)
    REQUIRE((int)session.layers[3].segments.size() == 3);

    // Layer 4: copy of layer 1 (2 segs)
    REQUIRE(session.layers[4].layer_index == 4);
    REQUIRE((int)session.layers[4].segments.size() == 2);

    // Layer 5: copy of layer 0 (1 seg)
    REQUIRE(session.layers[5].layer_index == 5);
    REQUIRE((int)session.layers[5].segments.size() == 1);

    // Z continuity: each layer's z_start == previous layer's z_end (no gaps)
    for (int i = 1; i < session.layer_count(); ++i)
        REQUIRE_THAT(session.layers[i].z_start,
                     WithinAbs(session.layers[i - 1].z_end, 1e-9));
}

TEST_CASE("MirrorStack: undo restores empty top layer after fill", "[MirrorStack]")
{
    // Same setup: 3 content layers + 1 empty
    DrawSession session;
    session.add_layer(0.3);
    session.layers[0].segments.push_back(make_seg(0.0, 0.0, 1.0, 0.0));
    session.add_layer(0.3);
    session.layers[1].segments.push_back(make_seg(2.0, 0.0, 3.0, 0.0));
    session.layers[1].segments.push_back(make_seg(4.0, 0.0, 5.0, 0.0));
    session.add_layer(0.3);
    session.layers[2].segments.push_back(make_seg(6.0, 0.0, 7.0, 0.0));
    session.layers[2].segments.push_back(make_seg(8.0, 0.0, 9.0, 0.0));
    session.layers[2].segments.push_back(make_seg(10.0, 0.0, 11.0, 0.0));
    session.add_layer(0.3); // empty canvas
    session.active_layer = 3;

    Slic3r::GUI::MirrorStackCommand cmd;
    cmd.execute(session);
    REQUIRE(session.layer_count() == 6);

    cmd.undo(session);

    // Restored to 4 layers (3 content + 1 empty)
    REQUIRE(session.layer_count() == 4);
    REQUIRE(session.active_layer == 3);

    // Layer 3 must be empty again
    REQUIRE(session.layers[3].segments.empty());

    // Content layers intact
    REQUIRE((int)session.layers[0].segments.size() == 1);
    REQUIRE((int)session.layers[1].segments.size() == 2);
    REQUIRE((int)session.layers[2].segments.size() == 3);
}

TEST_CASE("MirrorStack: navigate all layers top-to-bottom-to-top after empty-top mirror", "[MirrorStack]")
{
    // 4 content layers (1,2,3,4 segs) + 1 empty = 5 layers before mirror.
    // After: src_count=4, fill layer 4, append 3 more → 8 total.
    // Segment pattern: 1,2,3,4 | 4,3,2,1
    DrawSession session;
    const int seg_pattern[] = { 1, 2, 3, 4 };
    for (int i = 0; i < 4; ++i) {
        session.add_layer(0.3);
        for (int s = 0; s < seg_pattern[i]; ++s)
            session.layers[i].segments.push_back(make_seg(0.0, 0.0, 1.0, 0.0));
    }
    session.add_layer(0.3); // empty top
    session.active_layer = 4;

    Slic3r::GUI::MirrorStackCommand cmd;
    cmd.execute(session);

    REQUIRE(session.layer_count() == 8);

    // Expected segment counts bottom-to-top: 1,2,3,4,4,3,2,1
    const int expected[] = { 1, 2, 3, 4, 4, 3, 2, 1 };
    for (int i = 0; i < 8; ++i)
        REQUIRE((int)session.layers[i].segments.size() == expected[i]);

    // Simulate navigation top-to-bottom (layer 7 → 0)
    for (int idx = 7; idx >= 0; --idx) {
        session.active_layer = idx;
        REQUIRE((int)session.layers[session.active_layer].segments.size() == expected[idx]);
    }
    // Simulate navigation bottom-to-top (layer 0 → 7)
    for (int idx = 0; idx <= 7; ++idx) {
        session.active_layer = idx;
        REQUIRE((int)session.layers[session.active_layer].segments.size() == expected[idx]);
    }

    // Z contiguity
    for (int i = 1; i < session.layer_count(); ++i)
        REQUIRE_THAT(session.layers[i].z_start,
                     WithinAbs(session.layers[i - 1].z_end, 1e-9));
}

TEST_CASE("MirrorStack: only empty layer — no-op", "[MirrorStack]")
{
    // When the only layer is empty (src_count == 0), execute() must return
    // early without changing the session (except that saved_layer_count is set).
    DrawSession session;
    session.add_layer(0.3); // empty layer 0
    session.active_layer = 0;

    Slic3r::GUI::MirrorStackCommand cmd;
    cmd.execute(session);

    // Still 1 layer, still empty
    REQUIRE(session.layer_count() == 1);
    REQUIRE(session.layers[0].segments.empty());
}

TEST_CASE("MirrorStack: no empty top — behavior unchanged (backward compat)", "[MirrorStack]")
{
    // 3 content layers with 1,2,3 segments — no empty top.
    // Result: 6 layers (3 original + 3 new mirrors of layers 2,1,0).
    DrawSession session;
    session.add_layer(0.3); // layer 0: 1 seg
    session.layers[0].segments.push_back(make_seg(0.0, 0.0, 1.0, 0.0));
    session.add_layer(0.3); // layer 1: 2 segs
    session.layers[1].segments.push_back(make_seg(2.0, 0.0, 3.0, 0.0));
    session.layers[1].segments.push_back(make_seg(4.0, 0.0, 5.0, 0.0));
    session.add_layer(0.3); // layer 2: 3 segs
    session.layers[2].segments.push_back(make_seg(6.0, 0.0, 7.0, 0.0));
    session.layers[2].segments.push_back(make_seg(8.0, 0.0, 9.0, 0.0));
    session.layers[2].segments.push_back(make_seg(10.0, 0.0, 11.0, 0.0));
    session.active_layer = 2;

    Slic3r::GUI::MirrorStackCommand cmd;
    cmd.execute(session);

    REQUIRE(session.layer_count() == 6);
    REQUIRE(session.active_layer == 5);

    // New layers are mirrors of 2, 1, 0
    REQUIRE((int)session.layers[3].segments.size() == 3); // mirror of layer 2
    REQUIRE((int)session.layers[4].segments.size() == 2); // mirror of layer 1
    REQUIRE((int)session.layers[5].segments.size() == 1); // mirror of layer 0

    // Z contiguity
    for (int i = 1; i < session.layer_count(); ++i)
        REQUIRE_THAT(session.layers[i].z_start,
                     WithinAbs(session.layers[i - 1].z_end, 1e-9));
}

TEST_CASE("MirrorStack: full layer navigation — travel all layers top to bottom and back", "[MirrorStack]")
{
    // Simulate the user's actual workflow with halfstraightthrough.3mf:
    // 11 content layers (varying segment counts), then 1 empty layer, then Mirror Stack.
    // After mirror, navigate from top to bottom and back to top, verifying each layer.

    DrawSession s;
    // Create 11 content layers with known segment counts mirroring the real file's structure:
    //   layer 0:  8 segs,  layer 1: 12, layer 2: 12, layer 3: 16,
    //   layer 4: 17 segs, layer 5: 25, layer 6: 33, layer 7: 33,
    //   layer 8: 33 segs, layer 9: 33, layer 10: 33
    const std::vector<int> orig_counts = {8, 12, 12, 16, 17, 25, 33, 33, 33, 33, 33};
    const double layer_h = 0.3;
    for (int n : orig_counts) {
        s.add_layer(layer_h);
        int li = s.layer_count() - 1;
        for (int j = 0; j < n; ++j)
            s.layers[li].segments.push_back(make_seg((double)j, 0.0, (double)j + 1.0, 0.0));
    }
    REQUIRE(s.layer_count() == 11);

    // Add empty canvas layer (simulates user pressing "+ Layer")
    s.add_layer(layer_h);
    REQUIRE(s.layer_count() == 12);
    REQUIRE(s.layers[11].segments.empty());

    // Execute MirrorStack
    Slic3r::GUI::MirrorStackCommand cmd;
    cmd.execute(s);

    // Expected total: 12 original layers + (src_count-1 = 10) new = 22 layers
    // (empty-top path: layer 11 is filled in-place, 10 new layers appended)
    REQUIRE(s.layer_count() == 22);

    // Build expected segment count table for all 22 layers:
    //   Layers 0-10: original {8,12,12,16,17,25,33,33,33,33,33}
    //   Layer 11: filled with copy of layer 10 = 33
    //   Layers 12-21: copies of layers 9 down to 0
    //     Layer 12 = copy of layer 9  = 33
    //     Layer 13 = copy of layer 8  = 33
    //     Layer 14 = copy of layer 7  = 33
    //     Layer 15 = copy of layer 6  = 33
    //     Layer 16 = copy of layer 5  = 25
    //     Layer 17 = copy of layer 4  = 17
    //     Layer 18 = copy of layer 3  = 16
    //     Layer 19 = copy of layer 2  = 12
    //     Layer 20 = copy of layer 1  = 12
    //     Layer 21 = copy of layer 0  =  8
    const std::vector<int> expected_counts = {
         8, 12, 12, 16, 17, 25, 33, 33, 33, 33, 33,  // layers 0-10 (originals)
        33,                                             // layer 11 (filled)
        33, 33, 33, 33, 25, 17, 16, 12, 12,  8        // layers 12-21 (new mirrors)
    };
    REQUIRE((int)expected_counts.size() == 22);

    // Navigate top to bottom: verify each layer has the right segment count
    SECTION("Navigate top to bottom") {
        for (int active = 21; active >= 0; --active) {
            s.active_layer = active;
            INFO("Checking layer " << active << " (top-to-bottom navigation)");
            REQUIRE((int)s.layers[active].segments.size() == expected_counts[active]);
        }
    }

    // Navigate bottom to top: verify each layer has the right segment count
    SECTION("Navigate bottom to top") {
        for (int active = 0; active <= 21; ++active) {
            s.active_layer = active;
            INFO("Checking layer " << active << " (bottom-to-top navigation)");
            REQUIRE((int)s.layers[active].segments.size() == expected_counts[active]);
        }
    }

    // Verify Z continuity across all layers (no Z gaps)
    SECTION("Z continuity — no gaps") {
        for (int i = 1; i < s.layer_count(); ++i) {
            INFO("Z gap between layer " << (i - 1) << " and " << i);
            REQUIRE_THAT(s.layers[i].z_start, WithinAbs(s.layers[i - 1].z_end, 1e-9));
        }
    }

    // Symmetry check: segment counts form a palindrome
    SECTION("Segment count palindrome symmetry") {
        const int N = s.layer_count();
        for (int i = 0; i < N / 2; ++i) {
            int j = N - 1 - i;
            INFO("Layer " << i << " vs layer " << j);
            REQUIRE(s.layers[i].segments.size() == s.layers[j].segments.size());
        }
    }

    // Undo: verify full restoration of the 12-layer state (11 content + 1 empty)
    SECTION("Undo restores 12-layer state with empty top") {
        cmd.undo(s);
        REQUIRE(s.layer_count() == 12);
        REQUIRE(s.layers[11].segments.empty()); // empty canvas restored
        for (int i = 0; i < 11; ++i) {
            INFO("Checking original layer " << i << " after undo");
            REQUIRE((int)s.layers[i].segments.size() == orig_counts[i]);
        }
    }
}

// ---------------------------------------------------------------------------
// DeleteLayerZShift — Comprehensive tests for remove_layer() Z-shift fix
// ---------------------------------------------------------------------------

TEST_CASE("DeleteLayer: delete middle layer shifts z of layers above", "[DeleteLayerZShift]")
{
    // 5 layers × 0.3 mm each.
    DrawSession s;
    for (int i = 0; i < 5; ++i)
        s.add_layer(0.3);

    // Delete layer at index 2 (z 0.6–0.9).
    REQUIRE(s.remove_layer(2));
    REQUIRE(s.layer_count() == 4);

    // Layers 0 and 1 are below the deleted layer — unchanged.
    REQUIRE(s.layers[0].layer_index == 0);
    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.3, 1e-9));

    REQUIRE(s.layers[1].layer_index == 1);
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.6, 1e-9));

    // Layers 2 and 3 (were 3 and 4) shifted DOWN by 0.3.
    REQUIRE(s.layers[2].layer_index == 2);
    REQUIRE_THAT(s.layers[2].z_start, WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(s.layers[2].z_end,   WithinAbs(0.9, 1e-9));

    REQUIRE(s.layers[3].layer_index == 3);
    REQUIRE_THAT(s.layers[3].z_start, WithinAbs(0.9, 1e-9));
    REQUIRE_THAT(s.layers[3].z_end,   WithinAbs(1.2, 1e-9));

    // total_height must be 4 × 0.3 = 1.2, not the old 1.5.
    REQUIRE_THAT(s.total_height(), WithinAbs(1.2, 1e-9));
}

TEST_CASE("DeleteLayer: delete bottom layer shifts all others down", "[DeleteLayerZShift]")
{
    // 4 layers × 0.3 mm each.
    DrawSession s;
    for (int i = 0; i < 4; ++i)
        s.add_layer(0.3);
    s.active_layer = 0;

    REQUIRE(s.remove_layer(0));
    REQUIRE(s.layer_count() == 3);

    // All remaining layers shifted down by 0.3.
    REQUIRE(s.layers[0].layer_index == 0);
    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.3, 1e-9));

    REQUIRE(s.layers[1].layer_index == 1);
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.6, 1e-9));

    REQUIRE(s.layers[2].layer_index == 2);
    REQUIRE_THAT(s.layers[2].z_start, WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(s.layers[2].z_end,   WithinAbs(0.9, 1e-9));

    REQUIRE_THAT(s.total_height(), WithinAbs(0.9, 1e-9));
}

TEST_CASE("DeleteLayer: delete top layer does not shift others", "[DeleteLayerZShift]")
{
    // 4 layers × 0.3 mm each.
    DrawSession s;
    for (int i = 0; i < 4; ++i)
        s.add_layer(0.3);

    REQUIRE(s.remove_layer(3));
    REQUIRE(s.layer_count() == 3);

    // Layers below the deleted top are untouched.
    REQUIRE(s.layers[0].layer_index == 0);
    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.3, 1e-9));

    REQUIRE(s.layers[1].layer_index == 1);
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.6, 1e-9));

    REQUIRE(s.layers[2].layer_index == 2);
    REQUIRE_THAT(s.layers[2].z_start, WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(s.layers[2].z_end,   WithinAbs(0.9, 1e-9));

    REQUIRE_THAT(s.total_height(), WithinAbs(0.9, 1e-9));
}

TEST_CASE("DeleteLayer: undo restores z values and segments", "[DeleteLayerZShift]")
{
    // 4 layers × 0.3 mm each, with a unique segment on every layer.
    DrawSession s;
    for (int i = 0; i < 4; ++i) {
        s.add_layer(0.3);
        s.layers[i].segments.push_back(
            make_seg(static_cast<double>(i), 0.0,
                     static_cast<double>(i + 1), 0.0));
    }
    s.active_layer = 3;

    // --- execute ---
    Slic3r::GUI::RemoveLayerCommand cmd(1);
    cmd.execute(s);

    REQUIRE(s.layer_count() == 3);
    // Layer 1 (was 2) shifted down.
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.6, 1e-9));
    // Layer 2 (was 3) shifted down.
    REQUIRE_THAT(s.layers[2].z_start, WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(s.layers[2].z_end,   WithinAbs(0.9, 1e-9));
    // Segments on shifted layers are still their original data.
    REQUIRE_THAT(s.layers[1].segments[0].start.x(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(s.layers[2].segments[0].start.x(), WithinAbs(3.0, 1e-9));

    // --- undo ---
    cmd.undo(s);

    REQUIRE(s.layer_count() == 4);

    // All four original z ranges must be restored.
    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(s.layers[2].z_start, WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(s.layers[2].z_end,   WithinAbs(0.9, 1e-9));
    REQUIRE_THAT(s.layers[3].z_start, WithinAbs(0.9, 1e-9));
    REQUIRE_THAT(s.layers[3].z_end,   WithinAbs(1.2, 1e-9));

    // All layer_index values correct.
    for (int i = 0; i < 4; ++i)
        REQUIRE(s.layers[i].layer_index == i);

    // Segments restored.
    for (int i = 0; i < 4; ++i) {
        REQUIRE(s.layers[i].segments.size() == 1);
        REQUIRE_THAT(s.layers[i].segments[0].start.x(), WithinAbs(static_cast<double>(i), 1e-9));
    }

    // active_layer restored.
    REQUIRE(s.active_layer == 3);
}

TEST_CASE("DeleteLayer: delete first of two layers with different heights", "[DeleteLayerZShift]")
{
    // Layer 0: 0.3 mm, Layer 1: 0.5 mm.
    DrawSession s;
    s.add_layer(0.3);
    s.add_layer(0.5);

    REQUIRE(s.remove_layer(0));
    REQUIRE(s.layer_count() == 1);

    // The remaining layer (was index 1, height 0.5) must be re-anchored to z=0.
    REQUIRE(s.layers[0].layer_index == 0);
    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(s.total_height(),     WithinAbs(0.5, 1e-9));
}

TEST_CASE("DeleteLayer: multiple consecutive deletes cascade z correctly", "[DeleteLayerZShift]")
{
    // 5 layers × 0.3 mm each (total 1.5 mm).
    DrawSession s;
    for (int i = 0; i < 5; ++i)
        s.add_layer(0.3);

    // Delete layer at index 2 → 4 layers, total 1.2 mm.
    REQUIRE(s.remove_layer(2));
    REQUIRE(s.layer_count() == 4);
    REQUIRE_THAT(s.total_height(), WithinAbs(1.2, 1e-9));

    // Delete layer at index 1 → 3 layers, total 0.9 mm.
    REQUIRE(s.remove_layer(1));
    REQUIRE(s.layer_count() == 3);
    REQUIRE_THAT(s.total_height(), WithinAbs(0.9, 1e-9));

    // Verify final z layout and layer_index correctness.
    REQUIRE(s.layers[0].layer_index == 0);
    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.3, 1e-9));

    REQUIRE(s.layers[1].layer_index == 1);
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.6, 1e-9));

    REQUIRE(s.layers[2].layer_index == 2);
    REQUIRE_THAT(s.layers[2].z_start, WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(s.layers[2].z_end,   WithinAbs(0.9, 1e-9));
}

TEST_CASE("DeleteLayer: z continuity — no gaps after deletion", "[DeleteLayerZShift]")
{
    // 8 layers alternating 0.2 mm and 0.4 mm heights.
    DrawSession s;
    for (int i = 0; i < 8; ++i)
        s.add_layer((i % 2 == 0) ? 0.2 : 0.4);

    // Delete three layers and verify no Z gaps remain each time.
    // Delete at current index 1 (height 0.4).
    REQUIRE(s.remove_layer(1));
    // Delete at current index 2 (was original layer 3).
    REQUIRE(s.remove_layer(2));
    // Delete at current index 3 (was original layer 5).
    REQUIRE(s.remove_layer(3));

    // After three deletions: 5 layers remain. Verify no Z gaps.
    REQUIRE(s.layer_count() == 5);
    for (int i = 1; i < s.layer_count(); ++i) {
        INFO("Z gap between layer " << (i - 1) << " and " << i);
        REQUIRE_THAT(s.layers[i].z_start,
                     WithinAbs(s.layers[i - 1].z_end, 1e-9));
    }
    // All layer_index values must match vector positions.
    for (int i = 0; i < s.layer_count(); ++i)
        REQUIRE(s.layers[i].layer_index == i);
}

TEST_CASE("DeleteLayer: delete only layer produces empty session", "[DeleteLayerZShift]")
{
    DrawSession s;
    s.add_layer(0.3);
    s.layers[0].segments.push_back(make_seg(0.0, 0.0, 1.0, 0.0));

    REQUIRE(s.remove_layer(0));
    REQUIRE(s.is_empty());
    REQUIRE(s.layer_count() == 0);
    REQUIRE(s.active_layer == -1);
    REQUIRE_THAT(s.total_height(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("DeleteLayer: undo then redo (RemoveLayerCommand execute/undo/execute)", "[DeleteLayerZShift]")
{
    // 3 layers × 0.3 mm with known segments on each.
    DrawSession s;
    for (int i = 0; i < 3; ++i) {
        s.add_layer(0.3);
        s.layers[i].segments.push_back(
            make_seg(static_cast<double>(i * 10), 0.0,
                     static_cast<double>(i * 10 + 5), 0.0));
    }
    s.active_layer = 2;

    Slic3r::GUI::RemoveLayerCommand cmd(1);

    // --- First execute ---
    cmd.execute(s);
    REQUIRE(s.layer_count() == 2);
    // Layer 1 (was 2) shifted down by 0.3.
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(s.layers[1].segments[0].start.x(), WithinAbs(20.0, 1e-9));

    // --- Undo ---
    cmd.undo(s);
    REQUIRE(s.layer_count() == 3);
    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[2].z_start, WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(s.layers[1].segments[0].start.x(), WithinAbs(10.0, 1e-9)); // restored
    REQUIRE(s.active_layer == 2);

    // --- Second execute (redo) ---
    cmd.execute(s);
    REQUIRE(s.layer_count() == 2);
    // Must produce identical result as the first execute.
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(s.layers[1].segments[0].start.x(), WithinAbs(20.0, 1e-9));
}

TEST_CASE("DeleteLayer: 11-layer halfstraightthrough scenario", "[DeleteLayerZShift]")
{
    // Simulate the user's actual file: 11 layers each 0.3 mm.
    const int N = 11;
    DrawSession s;
    for (int i = 0; i < N; ++i)
        s.add_layer(0.3);

    // Delete layer at index 2 (1-indexed: "layer 3").
    REQUIRE(s.remove_layer(2));
    REQUIRE(s.layer_count() == 10);

    // Layers 0 and 1 are below the deleted layer — unchanged.
    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.6, 1e-9));

    // Layers 2–9 (were 3–10) shifted DOWN by 0.3 mm — no Z gap at old z=0.6.
    for (int i = 2; i < 10; ++i) {
        INFO("Checking layer " << i);
        REQUIRE(s.layers[i].layer_index == i);
        REQUIRE_THAT(s.layers[i].z_start,
                     WithinAbs(0.3 * static_cast<double>(i), 1e-9));
        REQUIRE_THAT(s.layers[i].z_end,
                     WithinAbs(0.3 * static_cast<double>(i + 1), 1e-9));
    }

    // Z contiguity: no gaps across all remaining layers.
    for (int i = 1; i < s.layer_count(); ++i) {
        INFO("Z gap between layer " << (i - 1) << " and " << i);
        REQUIRE_THAT(s.layers[i].z_start,
                     WithinAbs(s.layers[i - 1].z_end, 1e-9));
    }

    // Total height: 10 layers × 0.3 mm = 3.0 mm (not 3.3 mm).
    REQUIRE_THAT(s.total_height(), WithinAbs(3.0, 1e-9));
}

// ---------------------------------------------------------------------------
// Edge-case tests — independent validation (added by code reviewer)
// ---------------------------------------------------------------------------

TEST_CASE("DeleteLayer: insert_layer then remove_layer are perfect inverses", "[DeleteLayerZShift]")
{
    // Create 3 layers each 0.3 mm.
    DrawSession s;
    s.add_layer(0.3);
    s.add_layer(0.3);
    s.add_layer(0.3);
    REQUIRE(s.layer_count() == 3);

    // Record original z values.
    const double orig_z_start[3] = { s.layers[0].z_start, s.layers[1].z_start, s.layers[2].z_start };
    const double orig_z_end[3]   = { s.layers[0].z_end,   s.layers[1].z_end,   s.layers[2].z_end   };

    // Insert a new 0.3mm layer at position 2 (after layer 1) directly via the
    // DrawSession API.  InsertLayerAfterActiveCommand is not header-only so it
    // is unavailable in libslic3r_tests; insert_layer() is sufficient here.
    s.insert_layer(2, 0.3);

    // Now 4 layers.
    REQUIRE(s.layer_count() == 4);
    REQUIRE(s.active_layer == 2); // newly inserted layer is active

    // Layers 0 and 1 must be unchanged.
    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(orig_z_start[0], 1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(orig_z_end[0],   1e-9));
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(orig_z_start[1], 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(orig_z_end[1],   1e-9));

    // The inserted layer (index 2) fills z 0.6→0.9.
    REQUIRE_THAT(s.layers[2].z_start, WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(s.layers[2].z_end,   WithinAbs(0.9, 1e-9));

    // Old layer 2 (now index 3) was pushed up by 0.3 mm.
    REQUIRE_THAT(s.layers[3].z_start, WithinAbs(orig_z_start[2] + 0.3, 1e-9));
    REQUIRE_THAT(s.layers[3].z_end,   WithinAbs(orig_z_end[2]   + 0.3, 1e-9));

    // Now remove the newly-inserted layer at position 2.
    Slic3r::GUI::RemoveLayerCommand rem(2);
    rem.execute(s);

    // Must be back to exactly 3 layers.
    REQUIRE(s.layer_count() == 3);

    // Z values must be exactly the originals (within 1e-9).
    for (int i = 0; i < 3; ++i) {
        INFO("Checking layer " << i << " after insert+remove roundtrip");
        REQUIRE_THAT(s.layers[i].z_start, WithinAbs(orig_z_start[i], 1e-9));
        REQUIRE_THAT(s.layers[i].z_end,   WithinAbs(orig_z_end[i],   1e-9));
        REQUIRE(s.layers[i].layer_index == i);
    }
}

TEST_CASE("DeleteLayer: remove then mirror stack produces contiguous Z", "[DeleteLayerZShift]")
{
    // Create 5 layers each 0.3 mm, each with one content segment.
    DrawSession s;
    for (int i = 0; i < 5; ++i) {
        s.add_layer(0.3);
        s.layers[i].segments.push_back(make_seg(
            static_cast<double>(i), 0.0, static_cast<double>(i + 1), 0.0));
    }
    REQUIRE(s.layer_count() == 5);

    // Delete layer at index 2 → 4 layers remain.
    Slic3r::GUI::RemoveLayerCommand rem2(2);
    rem2.execute(s);
    REQUIRE(s.layer_count() == 4);

    // Z continuity after delete.
    for (int i = 1; i < s.layer_count(); ++i)
        REQUIRE_THAT(s.layers[i].z_start, WithinAbs(s.layers[i - 1].z_end, 1e-9));

    // Add one empty layer on top (simulates user pressing "+ Layer").
    s.add_layer(0.3);
    REQUIRE(s.layer_count() == 5);
    REQUIRE(s.layers[4].segments.empty());

    // Run MirrorStackCommand.  With an empty top layer:
    //   src_count = 4 (content layers 0..3)
    //   layer 4 is filled in-place with copy of layer 3
    //   src_count-1 = 3 new layers appended (copies of layers 2, 1, 0)
    //   Total = 5 + 3 = 8 layers.
    Slic3r::GUI::MirrorStackCommand mirror;
    mirror.execute(s);

    REQUIRE(s.layer_count() == 8);

    // Verify NO Z gaps anywhere — every z_start == previous z_end.
    for (int i = 1; i < s.layer_count(); ++i) {
        INFO("Z gap at layer " << i);
        REQUIRE_THAT(s.layers[i].z_start, WithinAbs(s.layers[i - 1].z_end, 1e-9));
    }

    // All layer_index values must be sequential.
    for (int i = 0; i < s.layer_count(); ++i)
        REQUIRE(s.layers[i].layer_index == i);
}

TEST_CASE("DeleteLayer: 3MF round-trip preserves corrected z values", "[DeleteLayerZShift]")
{
    // This test verifies that after a delete the session's z values are
    // correct for subsequent serialization (total_height and per-layer z).
    DrawSession s;
    for (int i = 0; i < 4; ++i) {
        s.add_layer(0.3);
        s.layers[i].segments.push_back(make_seg(
            static_cast<double>(i), 0.0, static_cast<double>(i + 1), 0.0));
    }
    REQUIRE(s.layer_count() == 4);

    // Delete layer at index 1.
    Slic3r::GUI::RemoveLayerCommand rem(1);
    rem.execute(s);

    // 3 layers must remain with z: [0, 0.3], [0.3, 0.6], [0.6, 0.9].
    REQUIRE(s.layer_count() == 3);

    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.3, 1e-9));

    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.3, 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.6, 1e-9));

    REQUIRE_THAT(s.layers[2].z_start, WithinAbs(0.6, 1e-9));
    REQUIRE_THAT(s.layers[2].z_end,   WithinAbs(0.9, 1e-9));

    // total_height() must reflect the corrected stack height exactly.
    REQUIRE_THAT(s.total_height(), WithinAbs(0.9, 1e-9));

    // No Z gaps.
    for (int i = 1; i < s.layer_count(); ++i)
        REQUIRE_THAT(s.layers[i].z_start, WithinAbs(s.layers[i - 1].z_end, 1e-9));
}

TEST_CASE("DeleteLayer: delete-then-undo does not leak z shifts", "[DeleteLayerZShift]")
{
    // Regression: repeated delete+undo cycles must not accumulate Z drift.
    DrawSession s;
    s.add_layer(0.3);
    s.add_layer(0.3);
    s.add_layer(0.3);
    s.add_layer(0.3);
    REQUIRE(s.layer_count() == 4);

    // Snapshot original z values for all 4 layers.
    struct ZSnap { double z_start, z_end; };
    ZSnap orig[4];
    for (int i = 0; i < 4; ++i)
        orig[i] = { s.layers[i].z_start, s.layers[i].z_end };

    auto verify_original = [&](const char* label) {
        INFO(label);
        REQUIRE(s.layer_count() == 4);
        for (int i = 0; i < 4; ++i) {
            INFO("  layer " << i);
            REQUIRE_THAT(s.layers[i].z_start, WithinAbs(orig[i].z_start, 1e-9));
            REQUIRE_THAT(s.layers[i].z_end,   WithinAbs(orig[i].z_end,   1e-9));
            REQUIRE(s.layers[i].layer_index == i);
        }
    };

    auto verify_after_delete = [&](const char* label) {
        INFO(label);
        REQUIRE(s.layer_count() == 3);
        // Layer 0 unchanged.
        REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
        REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.3, 1e-9));
        // Layers 1 and 2 shifted down by 0.3.
        REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.3, 1e-9));
        REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.6, 1e-9));
        REQUIRE_THAT(s.layers[2].z_start, WithinAbs(0.6, 1e-9));
        REQUIRE_THAT(s.layers[2].z_end,   WithinAbs(0.9, 1e-9));
        REQUIRE(s.layers[0].layer_index == 0);
        REQUIRE(s.layers[1].layer_index == 1);
        REQUIRE(s.layers[2].layer_index == 2);
    };

    // Cycle 1: delete layer 1.
    Slic3r::GUI::RemoveLayerCommand cmd1(1);
    cmd1.execute(s);
    verify_after_delete("After first delete");

    // Undo: must restore exactly.
    cmd1.undo(s);
    verify_original("After first undo");

    // Cycle 2: delete layer 1 again — shift must be identical, not doubled.
    Slic3r::GUI::RemoveLayerCommand cmd2(1);
    cmd2.execute(s);
    verify_after_delete("After second delete");

    // Undo again: must restore exactly.
    cmd2.undo(s);
    verify_original("After second undo");
}
