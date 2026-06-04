#include "DrawLayerSlider.hpp"

#include <wx/dcbuffer.h>
#include <wx/settings.h>

#include <algorithm>
#include <cmath>

namespace Slic3r { namespace GUI {

// ---------------------------------------------------------------------------
// Layout constants (kept in the .cpp, off the public API).
// ---------------------------------------------------------------------------
namespace {
    constexpr int kTrackMargin   = 24; // top & bottom margin reserved for thumb/labels
    constexpr int kRailWidth     = 5;  // vertical rail thickness (px)
    constexpr int kThumbHalfH    = 8;  // half-height of the active thumb marker
    constexpr int kMinWidth      = 64; // minimum widget width so labels fit

    // Palette (LOCKED — see 01_IMPLEMENTATION.md §3.3).
    const wxColour kBgColour      (55, 55, 55);
    const wxColour kRailColour    (105, 105, 105);
    const wxColour kRailFillColour(78, 78, 78);
    const wxColour kTickColour    (100, 100, 100);
    const wxColour kThumbFill     (245, 245, 220);
    const wxColour kThumbBorder   (255, 255, 0);
    const wxColour kThumbText     (20, 20, 20);
} // namespace

// ---------------------------------------------------------------------------
// Pure GUI-free mapping helpers (CONTRACT — see header).
// ---------------------------------------------------------------------------

int layer_slider_y_to_index(int y, int height_px, int top, int bottom, int n)
{
    if (n <= 0)
        return -1;
    if (n == 1)
        return 0;

    const double track_bottom = static_cast<double>(height_px - bottom); // y of layer 0
    const double usable       = static_cast<double>(height_px - top - bottom);
    if (usable <= 0.0)
        return 0;

    // frac: 0 at the bottom (layer 0), 1 at the top (layer n-1).
    const double frac  = (track_bottom - static_cast<double>(y)) / usable;
    int          index = static_cast<int>(std::lround(frac * static_cast<double>(n - 1)));
    index = std::clamp(index, 0, n - 1);
    return index;
}

int layer_slider_index_to_y(int index, int height_px, int top, int bottom, int n)
{
    const double track_bottom = static_cast<double>(height_px - bottom);
    const double usable       = static_cast<double>(height_px - top - bottom);

    if (n <= 1)
        return static_cast<int>(std::lround(static_cast<double>(top) + usable / 2.0));

    const int    clamped = std::clamp(index, 0, n - 1);
    const double frac    = static_cast<double>(clamped) / static_cast<double>(n - 1);
    const double y       = track_bottom - frac * usable;
    return static_cast<int>(std::lround(y));
}

// ---------------------------------------------------------------------------
// DrawLayerSlider widget
// ---------------------------------------------------------------------------

DrawLayerSlider::DrawLayerSlider(wxWindow* parent, std::function<void(int)> on_layer_change)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS)
    , m_on_change(std::move(on_layer_change))
{
    SetMinSize(wxSize(kMinWidth, -1));
    SetBackgroundStyle(wxBG_STYLE_PAINT); // flicker-free custom paint
    SetBackgroundColour(kBgColour);

    Bind(wxEVT_PAINT,      &DrawLayerSlider::on_paint,     this);
    Bind(wxEVT_LEFT_DOWN,  &DrawLayerSlider::on_left_down, this);
    Bind(wxEVT_MOTION,     &DrawLayerSlider::on_motion,    this);
    Bind(wxEVT_LEFT_UP,    &DrawLayerSlider::on_left_up,   this);
    Bind(wxEVT_MOUSEWHEEL, &DrawLayerSlider::on_wheel,     this);
    Bind(wxEVT_KEY_DOWN,   &DrawLayerSlider::on_key_down,  this);
    Bind(wxEVT_SIZE,       &DrawLayerSlider::on_size,      this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { m_dragging = false; });
}

void DrawLayerSlider::set_layers(const std::vector<double>& z_heights)
{
    m_z_heights = z_heights;
    if (m_active >= layer_count())
        m_active = layer_count() - 1; // clamp (also handles empty -> -1)
    Refresh(false);
}

void DrawLayerSlider::set_active(int active)
{
    const int n = layer_count();
    m_active = (n == 0) ? -1 : std::clamp(active, 0, n - 1);
    Refresh(false);
}

int DrawLayerSlider::y_to_layer(int y) const
{
    const wxSize sz = GetClientSize();
    return layer_slider_y_to_index(y, sz.GetHeight(), kTrackMargin, kTrackMargin, layer_count());
}

int DrawLayerSlider::layer_to_y(int index) const
{
    const wxSize sz = GetClientSize();
    return layer_slider_index_to_y(index, sz.GetHeight(), kTrackMargin, kTrackMargin, layer_count());
}

void DrawLayerSlider::request_active(int index)
{
    const int n = layer_count();
    if (n == 0)
        return;
    const int clamped = std::clamp(index, 0, n - 1);
    if (clamped == m_active)
        return;
    m_active = clamped;
    Refresh(false);
    if (m_on_change)
        m_on_change(clamped); // host applies to DrawSession and calls set_active() back
}

void DrawLayerSlider::step_active(int delta)
{
    if (layer_count() == 0)
        return;
    const int base = (m_active < 0) ? 0 : m_active;
    request_active(base + delta);
}

void DrawLayerSlider::on_left_down(wxMouseEvent& evt)
{
    SetFocus(); // so arrow/page/home/end keys reach this widget after a click
    if (layer_count() == 0)
        return;
    if (!HasCapture())
        CaptureMouse();
    m_dragging = true;
    request_active(y_to_layer(evt.GetY())); // single click teleports to nearest layer
}

void DrawLayerSlider::on_motion(wxMouseEvent& evt)
{
    if (!m_dragging || !evt.LeftIsDown() || layer_count() == 0)
        return;
    request_active(y_to_layer(evt.GetY())); // drag-to-scrub: fire on each layer change
}

void DrawLayerSlider::on_left_up(wxMouseEvent&)
{
    m_dragging = false;
    if (HasCapture())
        ReleaseMouse();
}

void DrawLayerSlider::on_wheel(wxMouseEvent& evt)
{
    if (layer_count() == 0)
        return;
    step_active(evt.GetWheelRotation() > 0 ? +1 : -1); // wheel up = +1, down = -1
}

void DrawLayerSlider::on_key_down(wxKeyEvent& evt)
{
    if (layer_count() == 0) {
        evt.Skip();
        return;
    }
    switch (evt.GetKeyCode()) {
    case WXK_UP:       step_active(+1); break;
    case WXK_DOWN:     step_active(-1); break;
    case WXK_PAGEUP:   step_active(+5); break;
    case WXK_PAGEDOWN: step_active(-5); break;
    case WXK_HOME:     request_active(layer_count() - 1); break; // top = highest layer
    case WXK_END:      request_active(0); break;                 // bottom = layer 0
    default:           evt.Skip(); break;
    }
}

void DrawLayerSlider::on_size(wxSizeEvent& evt)
{
    Refresh(false);
    evt.Skip();
}

void DrawLayerSlider::on_paint(wxPaintEvent&)
{
    wxBufferedPaintDC dc(this);
    const wxSize sz = GetClientSize();

    // Background.
    dc.SetBackground(wxBrush(kBgColour));
    dc.Clear();

    const int n = layer_count();
    const int cx = sz.GetWidth() / 2;
    const int rail_top    = kTrackMargin;
    const int rail_bottom = std::max(rail_top, sz.GetHeight() - kTrackMargin);

    // Track rail (rounded vertical bar).
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(kRailColour));
    dc.DrawRoundedRectangle(cx - kRailWidth / 2, rail_top, kRailWidth, rail_bottom - rail_top, kRailWidth / 2);

    if (n == 0)
        return; // empty/disabled state: just the empty rail

    // Filled portion from the bottom up to the active thumb.
    if (m_active >= 0) {
        const int thumb_y = layer_to_y(m_active);
        if (rail_bottom > thumb_y) {
            dc.SetBrush(wxBrush(kRailFillColour));
            dc.DrawRoundedRectangle(cx - kRailWidth / 2, thumb_y, kRailWidth, rail_bottom - thumb_y, kRailWidth / 2);
        }
    }

    // One tick per layer.
    dc.SetPen(wxPen(kTickColour, 1));
    for (int i = 0; i < n; ++i) {
        const int ty = layer_to_y(i);
        dc.DrawLine(cx - 9, ty, cx + 9, ty);
    }

    // Active thumb + floating label.
    if (m_active >= 0 && m_active < n) {
        const int thumb_y = layer_to_y(m_active);

        // Thumb marker (filled rounded rect straddling the rail).
        dc.SetPen(wxPen(kThumbBorder, 2));
        dc.SetBrush(wxBrush(kThumbFill));
        const int thumb_w = std::max(16, sz.GetWidth() - 8);
        dc.DrawRoundedRectangle(cx - thumb_w / 2, thumb_y - kThumbHalfH, thumb_w, kThumbHalfH * 2, 3);

        // Floating label: "Layer: N  z.zzmm" (N is 1-based).
        const wxString label = wxString::Format("Layer: %d  %.2fmm", m_active + 1, m_z_heights[m_active]);
        dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        const wxSize tsz = dc.GetTextExtent(label);
        const int    pad = 3;
        int box_x = cx - (tsz.x + 2 * pad) / 2;
        int box_y = thumb_y - kThumbHalfH - tsz.y - 2 * pad - 2;
        box_x = std::clamp(box_x, 1, std::max(1, sz.GetWidth() - tsz.x - 2 * pad - 1));
        if (box_y < 1)
            box_y = thumb_y + kThumbHalfH + 2; // flip below the thumb near the top edge

        // Readout box matches the canvas overlay style (border 20,20,20 / fill 245,245,220).
        dc.SetPen(wxPen(wxColour(20, 20, 20), 1));
        dc.SetBrush(wxBrush(kThumbFill));
        dc.DrawRoundedRectangle(box_x, box_y, tsz.x + 2 * pad, tsz.y + 2 * pad, 3);
        dc.SetTextForeground(kThumbText);
        dc.DrawText(label, box_x + pad, box_y + pad);
    }
}

}} // namespace Slic3r::GUI
