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
struct TranslateSegmentCommand : DrawCommand {
    int   layer_index;
    int   segment_index;
    Vec2d delta;

    TranslateSegmentCommand(int layer_idx, int seg_idx, Vec2d delta_)
        : layer_index(layer_idx), segment_index(seg_idx), delta(delta_) {}

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
