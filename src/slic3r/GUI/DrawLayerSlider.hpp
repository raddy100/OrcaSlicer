#pragma once

#include <wx/panel.h>
#include <functional>
#include <vector>

namespace Slic3r { namespace GUI {

// ---------------------------------------------------------------------------
// Pure, GUI-free pixel<->layer mapping helpers.
//
// These free functions contain NO wx dependency (plain int/double arithmetic)
// so they can be unit-tested from libslic3r_tests, which does not link the GUI
// library. The DrawLayerSlider widget's y_to_layer()/layer_to_y() delegate to
// them. Their names, parameter order and edge-case behavior are a CONTRACT —
// do not change them.
//
// Track geometry: the usable track spans pixel-Y in [top, height_px - bottom].
// Layer 0 maps to the BOTTOM of the track (largest Y); layer n-1 maps to the
// TOP (smallest Y), matching physical print orientation.
// ---------------------------------------------------------------------------

// Map a pixel-Y to the nearest layer index. Bottom = 0, top = n-1. Result is
// clamped to [0, n-1]. Returns -1 when n == 0 (no active layer). For n == 1 it
// always returns 0.
int layer_slider_y_to_index(int y, int height_px, int top, int bottom, int n);

// Centre pixel-Y of layer `index`'s tick. For n <= 1 it returns a y within the
// track (no division by zero). Round-trips with layer_slider_y_to_index:
// y_to_index(index_to_y(i)) == i for all valid i.
int layer_slider_index_to_y(int index, int height_px, int top, int bottom, int n);

// Vertical, single-thumb layer scrubber for Draw Mode.
// Pure view widget: it owns NO layer data. The panel pushes state in via
// set_layers()/set_active() and receives change requests via the callback.
class DrawLayerSlider : public wxPanel
{
public:
    // on_layer_change(new_active_index): invoked when the user scrubs or clicks
    // to a different layer. The host applies the change to DrawSession and calls
    // set_active() back (the widget does NOT mutate state itself).
    DrawLayerSlider(wxWindow* parent, std::function<void(int)> on_layer_change);

    // Push the current layer set. z_heights[i] is the z_end (mm) of layer i,
    // used only for the floating label. Pass the layer count via the vector size.
    void set_layers(const std::vector<double>& z_heights);

    // Highlight/move the thumb to layer `active` (-1 = none). Does NOT fire the callback.
    void set_active(int active);

    int  active() const { return m_active; }
    int  layer_count() const { return (int) m_z_heights.size(); }

private:
    void on_paint(wxPaintEvent&);
    void on_left_down(wxMouseEvent&);
    void on_motion(wxMouseEvent&);
    void on_left_up(wxMouseEvent&);
    void on_wheel(wxMouseEvent&);
    void on_key_down(wxKeyEvent&);
    void on_size(wxSizeEvent&);

    // Apply a relative step (clamped) and fire m_on_change if the active index changed.
    void step_active(int delta);
    // Set active to an absolute clamped index and fire m_on_change if it changed.
    void request_active(int index);

    // Map a pixel-Y on the track to the nearest layer index (clamped).
    int  y_to_layer(int y) const;
    // Map a layer index to the centre pixel-Y of its tick.
    int  layer_to_y(int index) const;

    std::function<void(int)> m_on_change;
    std::vector<double>      m_z_heights;   // size == layer count
    int                      m_active   { -1 };
    bool                     m_dragging { false };
};

}} // namespace Slic3r::GUI
