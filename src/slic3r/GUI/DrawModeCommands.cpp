#include "DrawModeCommands.hpp"
#include "libslic3r/DrawModeFeedback.hpp"
#include <cassert>
#include <stdexcept>

namespace Slic3r {
namespace GUI {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static DrawLayer& get_layer(DrawSession& s, int idx)
{
    if (idx < 0 || idx >= (int)s.layers.size())
        throw std::out_of_range("DrawCommand: layer_index out of range");
    return s.layers[idx];
}

static DrawSegment& get_segment(DrawSession& s, int layer_idx, int seg_idx)
{
    DrawLayer& l = get_layer(s, layer_idx);
    if (seg_idx < 0 || seg_idx >= (int)l.segments.size())
        throw std::out_of_range("DrawCommand: segment_index out of range");
    return l.segments[seg_idx];
}

// ---------------------------------------------------------------------------
// AddSegmentCommand
// ---------------------------------------------------------------------------

void AddSegmentCommand::execute(DrawSession& session)
{
    get_layer(session, layer_index).segments.push_back(segment);
}

void AddSegmentCommand::undo(DrawSession& session)
{
    DrawLayer& layer = get_layer(session, layer_index);
    assert(!layer.segments.empty());
    layer.segments.pop_back();
}

// ---------------------------------------------------------------------------
// DeleteSegmentCommand
// ---------------------------------------------------------------------------

void DeleteSegmentCommand::execute(DrawSession& session)
{
    DrawLayer& layer = get_layer(session, layer_index);
    assert(segment_index >= 0 && segment_index < (int)layer.segments.size());
    saved = layer.segments[segment_index];
    layer.segments.erase(layer.segments.begin() + segment_index);
}

void DeleteSegmentCommand::undo(DrawSession& session)
{
    DrawLayer& layer = get_layer(session, layer_index);
    layer.segments.insert(layer.segments.begin() + segment_index, saved);
}

// ---------------------------------------------------------------------------
// MoveEndpointCommand
// ---------------------------------------------------------------------------

void MoveEndpointCommand::execute(DrawSession& session)
{
    DrawSegment& seg = get_segment(session, layer_index, segment_index);
    if (is_start)
        seg.start = new_pos;
    else
        seg.end = new_pos;
}

void MoveEndpointCommand::undo(DrawSession& session)
{
    DrawSegment& seg = get_segment(session, layer_index, segment_index);
    if (is_start)
        seg.start = old_pos;
    else
        seg.end = old_pos;
}

// ---------------------------------------------------------------------------
// TranslateSegmentCommand
// ---------------------------------------------------------------------------

void TranslateSegmentCommand::execute(DrawSession& session)
{
    DrawSegment& seg = get_segment(session, layer_index, segment_index);
    seg = draw_translate_segment(seg, delta);
}

void TranslateSegmentCommand::undo(DrawSession& session)
{
    DrawSegment& seg = get_segment(session, layer_index, segment_index);
    seg = draw_translate_segment(seg, -delta);
}

// ---------------------------------------------------------------------------
// MoveControlHandleCommand
// ---------------------------------------------------------------------------

void MoveControlHandleCommand::execute(DrawSession& session)
{
    DrawSegment& seg = get_segment(session, layer_index, segment_index);
    if (ctrl_idx == 0) seg.ctrl1 = new_pos;
    else               seg.ctrl2 = new_pos;
}

void MoveControlHandleCommand::undo(DrawSession& session)
{
    DrawSegment& seg = get_segment(session, layer_index, segment_index);
    if (ctrl_idx == 0) seg.ctrl1 = old_pos;
    else               seg.ctrl2 = old_pos;
}

// ---------------------------------------------------------------------------
// AddLayerCommand
// ---------------------------------------------------------------------------

void AddLayerCommand::execute(DrawSession& session)
{
    session.add_layer(layer_height);
}

void AddLayerCommand::undo(DrawSession& session)
{
    assert(!session.layers.empty());
    session.layers.pop_back();
    session.active_layer = session.layers.empty() ? -1 : (int)session.layers.size() - 1;
}

// ---------------------------------------------------------------------------
// InsertLayerAfterActiveCommand
// ---------------------------------------------------------------------------

void InsertLayerAfterActiveCommand::execute(DrawSession& session)
{
    saved_active    = session.active_layer;
    insert_position = saved_active + 1; // one above the current active layer
    session.insert_layer(insert_position, layer_height);
}

void InsertLayerAfterActiveCommand::undo(DrawSession& session)
{
    assert(insert_position >= 0 && insert_position < session.layer_count());
    const double lh = session.layers[insert_position].layer_height();

    session.layers.erase(session.layers.begin() + insert_position);

    // Undo z-shift and layer_index increment for all layers that were shifted up.
    for (int i = insert_position; i < (int)session.layers.size(); ++i) {
        session.layers[i].z_start     -= lh;
        session.layers[i].z_end       -= lh;
        session.layers[i].layer_index  = i;
    }

    session.active_layer = saved_active;
}

// ---------------------------------------------------------------------------
// ClearLayerCommand
// ---------------------------------------------------------------------------

void ClearLayerCommand::execute(DrawSession& session)
{
    DrawLayer& layer = get_layer(session, layer_index);
    saved_segments = layer.segments;
    layer.segments.clear();
}

void ClearLayerCommand::undo(DrawSession& session)
{
    get_layer(session, layer_index).segments = saved_segments;
}

// ---------------------------------------------------------------------------
// RemoveLayerCommand
// ---------------------------------------------------------------------------

void RemoveLayerCommand::execute(DrawSession& session)
{
    assert(layer_index >= 0 && layer_index < session.layer_count());
    saved_layer  = session.layers[layer_index];
    saved_active = session.active_layer;
    session.remove_layer(layer_index);
}

void RemoveLayerCommand::undo(DrawSession& session)
{
    // Re-insert the saved layer at its original position.
    assert(layer_index >= 0 && layer_index <= session.layer_count());
    session.layers.insert(session.layers.begin() + layer_index, saved_layer);
    session.active_layer = saved_active;
}

} // namespace GUI
} // namespace Slic3r
