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

TEST_CASE("DrawSession::remove_layer - Z values of remaining layers are preserved", "[DrawSession]")
{
    DrawSession s;
    s.add_layer(0.2); // layer 0: z 0.0–0.2
    s.add_layer(0.3); // layer 1: z 0.2–0.5
    s.add_layer(0.1); // layer 2: z 0.5–0.6

    s.remove_layer(1); // remove middle layer

    REQUIRE(s.layer_count() == 2);
    REQUIRE_THAT(s.layers[0].z_start, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(s.layers[0].z_end,   WithinAbs(0.2, 1e-9));
    // What was layer 2 is now layer 1
    REQUIRE_THAT(s.layers[1].z_start, WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(s.layers[1].z_end,   WithinAbs(0.6, 1e-9));
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
