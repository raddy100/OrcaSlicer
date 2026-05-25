#pragma once

#include "libslic3r/DrawSession.hpp"
#include <memory>
#include <vector>

namespace Slic3r {
namespace GUI {

// Abstract base for all Draw-Mode edit commands.
// Commands are executed once (forward direction), then stored on the undo
// stack so their inverse can be replayed via undo().  No wx dependencies.
struct DrawCommand {
    virtual ~DrawCommand() = default;
    virtual void execute(DrawSession& session) = 0;
    virtual void undo(DrawSession& session) = 0;
};

// Add a new segment to a specific layer.
struct AddSegmentCommand : DrawCommand {
    int         layer_index;
    DrawSegment segment;

    AddSegmentCommand(int layer_idx, DrawSegment seg)
        : layer_index(layer_idx), segment(std::move(seg)) {}

    void execute(DrawSession& session) override;
    void undo(DrawSession& session) override;
};

// Remove the segment at the given index from a layer.
struct DeleteSegmentCommand : DrawCommand {
    int         layer_index;
    int         segment_index;
    DrawSegment saved; // populated during execute() for undo

    DeleteSegmentCommand(int layer_idx, int seg_idx)
        : layer_index(layer_idx), segment_index(seg_idx) {}

    void execute(DrawSession& session) override;
    void undo(DrawSession& session) override;
};

// Move one endpoint of a segment (either start or end) to a new position.
struct MoveEndpointCommand : DrawCommand {
    int    layer_index;
    int    segment_index;
    bool   is_start;    // true = move start, false = move end
    Vec2d  old_pos;
    Vec2d  new_pos;

    MoveEndpointCommand(int layer_idx, int seg_idx, bool start, Vec2d old_p, Vec2d new_p)
        : layer_index(layer_idx), segment_index(seg_idx), is_start(start),
          old_pos(old_p), new_pos(new_p) {}

    void execute(DrawSession& session) override;
    void undo(DrawSession& session) override;
};

// Translate a whole committed segment by a plate-space delta. Used by Edit Mode
// arrow-key nudging so both endpoints move through one undoable command entry.
// Uses draw_translate_segment() so ctrl1/ctrl2 are also moved for arc/bezier.
struct TranslateSegmentCommand : DrawCommand {
    int   layer_index;
    int   segment_index;
    Vec2d delta;

    TranslateSegmentCommand(int layer_idx, int seg_idx, Vec2d delta_)
        : layer_index(layer_idx), segment_index(seg_idx), delta(delta_) {}

    void execute(DrawSession& session) override;
    void undo(DrawSession& session) override;
};

// Move one control handle of an arc or bezier segment.
// ctrl_idx: 0 = ctrl1 (arc through-point or bezier ctrl1), 1 = ctrl2 (bezier only)
struct MoveControlHandleCommand : DrawCommand {
    int   layer_index;
    int   segment_index;
    int   ctrl_idx;  // 0 = ctrl1, 1 = ctrl2
    Vec2d old_pos;
    Vec2d new_pos;

    MoveControlHandleCommand(int layer_idx, int seg_idx, int ctrl, Vec2d old_p, Vec2d new_p)
        : layer_index(layer_idx), segment_index(seg_idx), ctrl_idx(ctrl),
          old_pos(old_p), new_pos(new_p) {}

    void execute(DrawSession& session) override;
    void undo(DrawSession& session) override;
};

// Append a new empty layer with the given layer height (mm).
struct AddLayerCommand : DrawCommand {
    double layer_height;

    explicit AddLayerCommand(double lh) : layer_height(lh) {}

    void execute(DrawSession& session) override;
    void undo(DrawSession& session) override;
};

// Insert a new empty layer immediately after the current active layer,
// making it the new active layer. Used by the "+ Layer" button.
// Layers above the insertion point have their z and layer_index shifted up.
struct InsertLayerAfterActiveCommand : DrawCommand {
    double layer_height;
    int    insert_position; // populated during execute() for undo
    int    saved_active;    // active_layer before execute(), restored on undo

    explicit InsertLayerAfterActiveCommand(double lh)
        : layer_height(lh), insert_position(-1), saved_active(-1) {}

    void execute(DrawSession& session) override;
    void undo(DrawSession& session) override;
};

// Clear all segments from a layer (preserves the layer itself).
struct ClearLayerCommand : DrawCommand {
    int                      layer_index;
    std::vector<DrawSegment> saved_segments; // populated during execute()

    explicit ClearLayerCommand(int layer_idx) : layer_index(layer_idx) {}

    void execute(DrawSession& session) override;
    void undo(DrawSession& session) override;
};

// Reference to one endpoint in a layer (no layer_index — all in same layer).
struct ConnectedEndpointRef {
    int  segment_index;
    bool is_start; // true = start endpoint, false = end endpoint
};

// Move multiple endpoints that share the same position to a new position.
// Used when dragging a shared node in Edit Mode: all connected segment endpoints
// move atomically (single undo step).
struct MoveConnectedEndpointsCommand : DrawCommand {
    int layer_index;
    std::vector<ConnectedEndpointRef> endpoints; // includes the primary + all connected
    Vec2d old_pos;
    Vec2d new_pos;

    MoveConnectedEndpointsCommand(int layer_idx,
                                  std::vector<ConnectedEndpointRef> eps,
                                  Vec2d old_p, Vec2d new_p)
        : layer_index(layer_idx), endpoints(std::move(eps)),
          old_pos(old_p), new_pos(new_p) {}

    void execute(DrawSession& session) override {
        assert(layer_index >= 0 && layer_index < session.layer_count());
        DrawLayer& layer = session.layers[layer_index];
        for (const auto& ep : endpoints) {
            assert(ep.segment_index >= 0 && ep.segment_index < (int)layer.segments.size());
            DrawSegment& seg = layer.segments[ep.segment_index];
            if (ep.is_start) seg.start = new_pos;
            else             seg.end   = new_pos;
        }
    }

    void undo(DrawSession& session) override {
        assert(layer_index >= 0 && layer_index < session.layer_count());
        DrawLayer& layer = session.layers[layer_index];
        for (const auto& ep : endpoints) {
            assert(ep.segment_index >= 0 && ep.segment_index < (int)layer.segments.size());
            DrawSegment& seg = layer.segments[ep.segment_index];
            if (ep.is_start) seg.start = old_pos;
            else             seg.end   = old_pos;
        }
    }
};

// Append a set of copied segments to a layer.
// Paste is always additive: existing segments on the target layer are preserved.
// Undo removes exactly the appended segments (the last segments.size() entries).
// This command is header-only so the libslic3r_tests target can link it without
// pulling in the GUI library.
struct PasteSegmentsCommand : DrawCommand {
    int                      layer_index;
    std::vector<DrawSegment> segments; // segments to append

    PasteSegmentsCommand(int layer_idx, std::vector<DrawSegment> segs)
        : layer_index(layer_idx), segments(std::move(segs)) {}

    void execute(DrawSession& session) override {
        assert(layer_index >= 0 && layer_index < session.layer_count());
        DrawLayer& layer = session.layers[layer_index];
        layer.segments.insert(layer.segments.end(), segments.begin(), segments.end());
    }

    void undo(DrawSession& session) override {
        assert(layer_index >= 0 && layer_index < session.layer_count());
        DrawLayer& layer = session.layers[layer_index];
        assert(layer.segments.size() >= segments.size());
        layer.segments.resize(layer.segments.size() - segments.size());
    }
};

// Append a reversed copy of all layers to the session (Z-mirror of the stack).
// New layer N = copy of layer N-1, new layer N+1 = copy of layer N-2, etc.
// Undo removes exactly the appended layers and restores active_layer.
// Header-only so it can be unit-tested from test_draw_session.cpp without
// pulling in the GUI library.
struct MirrorStackCommand : DrawCommand {
    int saved_layer_count { -1 }; // populated during execute() for undo
    int saved_active      { -1 };

    void execute(DrawSession& session) override {
        saved_layer_count = session.layer_count();
        saved_active      = session.active_layer;

        if (saved_layer_count == 0) return; // no-op

        double top_z = session.layers.back().z_end;

        // Append layers in reverse order: N-1, N-2, ..., 0
        for (int i = saved_layer_count - 1; i >= 0; --i) {
            const DrawLayer& src = session.layers[i];
            DrawLayer nlay;
            nlay.layer_index = session.layer_count(); // current count before push_back
            nlay.z_start     = top_z;
            nlay.z_end       = top_z + src.layer_height();
            nlay.segments    = src.segments; // deep copy
            top_z            = nlay.z_end;
            session.layers.push_back(std::move(nlay));
        }

        session.active_layer = session.layer_count() - 1;
    }

    void undo(DrawSession& session) override {
        assert(saved_layer_count >= 0);
        session.layers.resize(saved_layer_count);
        // We only appended to the tail, so existing layer_index values are
        // still correct after truncation.
        session.active_layer = saved_active;
    }
};

// Remove an entire layer (including all its segments) from the session.
// Undoable: re-inserts the layer and restores active_layer.
struct RemoveLayerCommand : DrawCommand {
    int       layer_index;
    DrawLayer saved_layer;  // populated during execute() for undo
    int       saved_active; // active_layer before execute(), restored on undo

    explicit RemoveLayerCommand(int layer_idx)
        : layer_index(layer_idx), saved_active(-1) {}

    void execute(DrawSession& session) override;
    void undo(DrawSession& session) override;
};

} // namespace GUI
} // namespace Slic3r
