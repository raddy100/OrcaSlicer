#include "DrawModePanel.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "PartPlate.hpp"
#include "MainFrame.hpp"
#include "GUI_ObjectList.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Config.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/tglbtn.h>
#include <wx/msgdlg.h>
#include <wx/dcbuffer.h>

#include <algorithm>

namespace Slic3r {
namespace GUI {

DrawModePanel::DrawModePanel(wxWindow* parent, Plater* plater)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL)
    , m_plater(plater)
{
    // Top banner (profile info)
    m_banner_text = new wxStaticText(this, wxID_ANY, "Draw Mode",
        wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);

    // Layer navigation row
    m_prev_layer_btn = new wxButton(this, wxID_ANY, "< Prev");
    m_layer_label    = new wxStaticText(this, wxID_ANY, "No layers",
        wxDefaultPosition, wxSize(240, -1), wxALIGN_CENTRE_HORIZONTAL);
    m_next_layer_btn = new wxButton(this, wxID_ANY, "Next >");
    m_add_layer_btn  = new wxButton(this, wxID_ANY, "+ Layer");

    // Mode toggles
    m_draw_toggle = new wxToggleButton(this, wxID_ANY, "Draw");
    m_edit_toggle = new wxToggleButton(this, wxID_ANY, "Edit");
    m_fill_toggle = new wxToggleButton(this, wxID_ANY, "Fill Width");
    m_draw_toggle->SetValue(true);

    // Action buttons
    m_clear_btn    = new wxButton(this, wxID_ANY, "Clear Layer");
    m_simulate_btn = new wxButton(this, wxID_ANY, "Simulate");
    m_finalize_btn = new wxButton(this, wxID_ANY, "Finalize");

    // Interactive 2-D drawing canvas
    m_canvas = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS);
    m_canvas->SetBackgroundStyle(wxBG_STYLE_PAINT); // suppress default erase
    m_canvas->SetBackgroundColour(wxColour(30, 30, 30));
    m_canvas->SetCursor(wxCursor(wxCURSOR_CROSS));

    // Layout
    auto* top_sizer = new wxBoxSizer(wxHORIZONTAL);
    top_sizer->Add(m_banner_text, 1, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_draw_toggle, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_edit_toggle, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_fill_toggle, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->AddStretchSpacer();
    top_sizer->Add(m_clear_btn,    0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_simulate_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_finalize_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);

    auto* nav_sizer = new wxBoxSizer(wxHORIZONTAL);
    nav_sizer->Add(m_prev_layer_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    nav_sizer->Add(m_layer_label,    1, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    nav_sizer->Add(m_next_layer_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    nav_sizer->Add(m_add_layer_btn,  0, wxALL | wxALIGN_CENTER_VERTICAL, 4);

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(top_sizer, 0, wxEXPAND);
    main_sizer->Add(nav_sizer, 0, wxEXPAND);
    main_sizer->Add(m_canvas, 1, wxEXPAND | wxALL, 8);

    SetSizer(main_sizer);

    // Bind events
    m_draw_toggle->Bind(wxEVT_TOGGLEBUTTON, &DrawModePanel::on_draw_toggle, this);
    m_edit_toggle->Bind(wxEVT_TOGGLEBUTTON, &DrawModePanel::on_edit_toggle, this);
    m_fill_toggle->Bind(wxEVT_TOGGLEBUTTON, &DrawModePanel::on_fill_toggle, this);
    m_prev_layer_btn->Bind(wxEVT_BUTTON, &DrawModePanel::on_prev_layer, this);
    m_next_layer_btn->Bind(wxEVT_BUTTON, &DrawModePanel::on_next_layer, this);
    m_add_layer_btn->Bind(wxEVT_BUTTON,  &DrawModePanel::on_add_layer,  this);
    m_clear_btn->Bind(wxEVT_BUTTON,      &DrawModePanel::on_clear_layer, this);
    m_simulate_btn->Bind(wxEVT_BUTTON,   &DrawModePanel::on_simulate,    this);
    m_finalize_btn->Bind(wxEVT_BUTTON,   &DrawModePanel::on_finalize,    this);
    Bind(wxEVT_CHAR_HOOK,                &DrawModePanel::on_char_hook,   this);

    // Canvas events
    m_canvas->Bind(wxEVT_PAINT,       &DrawModePanel::on_canvas_paint,      this);
    m_canvas->Bind(wxEVT_ERASE_BACKGROUND, &DrawModePanel::on_canvas_erase_bg, this);
    m_canvas->Bind(wxEVT_LEFT_DOWN,   &DrawModePanel::on_canvas_left_down,  this);
    m_canvas->Bind(wxEVT_LEFT_UP,     &DrawModePanel::on_canvas_left_up,   this);
    m_canvas->Bind(wxEVT_RIGHT_DOWN,  &DrawModePanel::on_canvas_right_down, this);
    m_canvas->Bind(wxEVT_MOTION,      &DrawModePanel::on_canvas_motion,     this);
    m_canvas->Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
        // Capture lost externally — clean up drag state without calling ReleaseMouse
        m_is_dragging = false;
        m_dragging_ep.reset();
        if (m_canvas) m_canvas->Refresh(false);
    });
}

DrawModePanel::~DrawModePanel() = default;

void DrawModePanel::activate(PartPlate* plate)
{
    m_session.clear();
    m_editing_obj_idx = -1;
    m_undo_stack.clear();
    m_redo_stack.clear();
    m_pending_start.reset();

    if (plate) {
        Vec3d origin = plate->get_origin();
        m_plate_x = origin.x();
        m_plate_y = origin.y();
    } else {
        m_plate_x = 0.0;
        m_plate_y = 0.0;
    }

    // Cache nozzle diameter from active printer preset
    m_nozzle_d = 0.4;
    if (wxGetApp().preset_bundle) {
        const auto& pcfg = wxGetApp().preset_bundle->printers.get_edited_preset().config;
        if (auto* nd = pcfg.option<ConfigOptionFloats>("nozzle_diameter"))
            if (!nd->values.empty()) m_nozzle_d = nd->values.front();
    }

    // Reset edit state
    m_sel_layer_idx = -1;
    m_sel_seg_idx   = -1;
    m_dragging_ep.reset();
    m_is_dragging   = false;

    update_banner();
    update_layer_label();
    if (m_canvas) m_canvas->Refresh();
}

void DrawModePanel::load_for_edit(ModelObject* obj, int obj_idx)
{
    if (!obj || !obj->draw_session) return;

    m_session = *obj->draw_session; // deep copy
    m_editing_obj_idx = obj_idx;
    m_undo_stack.clear();
    m_redo_stack.clear();

    update_banner();
    update_layer_label();
}

void DrawModePanel::refresh()
{
    update_layer_label();
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::update_banner()
{
    wxString info = "Draw Mode";
    if (m_editing_obj_idx >= 0)
        info += wxString::Format(" — Editing object %d", m_editing_obj_idx + 1);
    if (m_banner_text)
        m_banner_text->SetLabel(info);
}

void DrawModePanel::update_layer_label()
{
    if (!m_layer_label) return;

    int count = m_session.layer_count();
    if (count == 0) {
        m_layer_label->SetLabel("No layers — click '+ Layer' to start");
        return;
    }

    int active = m_session.active_layer;
    if (active < 0 || active >= count) active = count - 1;

    const DrawLayer& l = m_session.layers[active];
    m_layer_label->SetLabel(wxString::Format(
        "Layer %d of %d  (Z: %.3f -> %.3f mm)",
        active + 1, count, l.z_start, l.z_end));
}

void DrawModePanel::dispatch_command(std::unique_ptr<DrawCommand> cmd)
{
    cmd->execute(m_session);
    m_undo_stack.push_back(std::move(cmd));
    m_redo_stack.clear();
    refresh();
}

// ---------------------------------------------------------------------------
// Coordinate conversion
// ---------------------------------------------------------------------------

namespace {
    constexpr int CANVAS_PAD = 14; // pixel margin inside canvas widget
}

Vec2d DrawModePanel::screen_to_plate(wxPoint pt) const
{
    if (!m_canvas) return Vec2d(0.0, 0.0);
    wxSize sz = m_canvas->GetClientSize();
    double inner_w = sz.x - 2 * CANVAS_PAD;
    double inner_h = sz.y - 2 * CANVAS_PAD;
    if (inner_w <= 0 || inner_h <= 0) return Vec2d(0.0, 0.0);
    double px = (pt.x - CANVAS_PAD) / inner_w * m_plate_w_mm;
    double py = (1.0 - (pt.y - CANVAS_PAD) / inner_h) * m_plate_h_mm;
    return Vec2d(px, py);
}

wxPoint DrawModePanel::plate_to_screen(Vec2d pt) const
{
    if (!m_canvas) return wxPoint(0, 0);
    wxSize sz = m_canvas->GetClientSize();
    double inner_w = sz.x - 2 * CANVAS_PAD;
    double inner_h = sz.y - 2 * CANVAS_PAD;
    int sx = CANVAS_PAD + static_cast<int>(pt.x() / m_plate_w_mm * inner_w);
    int sy = CANVAS_PAD + static_cast<int>((1.0 - pt.y() / m_plate_h_mm) * inner_h);
    return wxPoint(sx, sy);
}

// ---------------------------------------------------------------------------
// Canvas paint
// ---------------------------------------------------------------------------

void DrawModePanel::on_canvas_erase_bg(wxEraseEvent&) { /* suppress flicker */ }

void DrawModePanel::on_canvas_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(m_canvas);
    wxSize sz = m_canvas->GetClientSize();

    // Background
    dc.SetBackground(wxBrush(wxColour(30, 30, 30)));
    dc.Clear();

    // Plate rectangle
    dc.SetPen(wxPen(wxColour(90, 90, 90), 1));
    dc.SetBrush(wxBrush(wxColour(45, 45, 45)));
    dc.DrawRectangle(CANVAS_PAD, CANVAS_PAD,
                     sz.x - 2 * CANVAS_PAD, sz.y - 2 * CANVAS_PAD);

    // Subtle grid every 10 mm
    dc.SetPen(wxPen(wxColour(60, 60, 60), 1, wxPENSTYLE_DOT));
    for (double x = 10.0; x < m_plate_w_mm; x += 10.0) {
        wxPoint p1 = plate_to_screen(Vec2d(x, 0.0));
        wxPoint p2 = plate_to_screen(Vec2d(x, m_plate_h_mm));
        dc.DrawLine(p1.x, CANVAS_PAD, p1.x, sz.y - CANVAS_PAD);
    }
    for (double y = 10.0; y < m_plate_h_mm; y += 10.0) {
        wxPoint p1 = plate_to_screen(Vec2d(0.0, y));
        dc.DrawLine(CANVAS_PAD, p1.y, sz.x - CANVAS_PAD, p1.y);
    }

    // No-layers message
    if (m_session.layer_count() == 0) {
        dc.SetTextForeground(wxColour(160, 160, 160));
        dc.SetFont(GetFont().Larger());
        wxString msg = "No layers — click '+ Layer' to start";
        wxSize ts = dc.GetTextExtent(msg);
        dc.DrawText(msg, (sz.x - ts.x) / 2, (sz.y - ts.y) / 2);
        return;
    }

    // Segments: travel=blue dashed, extrusion=orange; active bright, inactive dim
    int active = m_session.active_layer;
    for (int li = 0; li < m_session.layer_count(); ++li) {
        const bool is_active = (li == active);
        for (int si = 0; si < (int)m_session.layers[li].segments.size(); ++si) {
            const DrawSegment& seg = m_session.layers[li].segments[si];
            wxPoint p1 = plate_to_screen(seg.start);
            wxPoint p2 = plate_to_screen(seg.end);
            bool is_sel = (m_sel_layer_idx == li && m_sel_seg_idx == si);

            if (is_sel) {
                // Selected segment — bright yellow
                dc.SetPen(wxPen(wxColour(255, 255, 0), 3));
                dc.SetBrush(wxBrush(wxColour(255, 255, 0)));
                dc.DrawLine(p1, p2);
                dc.DrawCircle(p1, 4);
                dc.DrawCircle(p2, 4);
            } else if (seg.is_travel) {
                // Travel move — blue dashed
                wxColour c = is_active ? wxColour(80, 140, 255) : wxColour(30, 55, 100);
                dc.SetPen(wxPen(c, 1, wxPENSTYLE_SHORT_DASH));
                dc.DrawLine(p1, p2);
            } else if (m_show_filled && is_active) {
                // Filled nozzle-width quad for extrusion on active layer
                Vec2d dv = seg.end - seg.start;
                double dlen = dv.norm();
                if (dlen > 1e-6) {
                    Vec2d dir = dv / dlen;
                    Vec2d perp(-dir.y(), dir.x());
                    double hw = m_nozzle_d * 0.5;
                    wxPoint pts[4] = {
                        plate_to_screen(seg.start - perp * hw),
                        plate_to_screen(seg.start + perp * hw),
                        plate_to_screen(seg.end   + perp * hw),
                        plate_to_screen(seg.end   - perp * hw),
                    };
                    dc.SetPen(wxPen(wxColour(255, 100, 0), 1));
                    dc.SetBrush(wxBrush(wxColour(200, 75, 0)));
                    dc.DrawPolygon(4, pts);
                }
            } else {
                // Wire extrusion line
                wxColour c = is_active ? wxColour(255, 140, 0) : wxColour(80, 50, 15);
                int lw = is_active ? 2 : 1;
                dc.SetPen(wxPen(c, lw));
                dc.SetBrush(wxBrush(c));
                dc.DrawLine(p1, p2);
                if (is_active) {
                    dc.DrawCircle(p1, 3);
                    dc.DrawCircle(p2, 3);
                }
            }
        }
    }

    // Drawing mode: pending start + preview line
    if (m_input_mode == DrawInputMode::Drawing && m_pending_start) {
        wxPoint ps = plate_to_screen(*m_pending_start);
        wxPoint pm_draw = plate_to_screen(m_mouse_plate);
        dc.SetPen(wxPen(wxColour(255, 200, 50), 1, wxPENSTYLE_SHORT_DASH));
        dc.DrawLine(ps, pm_draw);
        dc.SetPen(wxPen(wxColour(255, 200, 50), 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawCircle(ps, 5);
    }

    // Edit mode: endpoint handles (hollow circles on active layer)
    if (m_input_mode == DrawInputMode::Editing
            && active >= 0 && active < m_session.layer_count()) {
        dc.SetPen(wxPen(wxColour(255, 200, 50), 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        for (const DrawSegment& seg : m_session.layers[active].segments) {
            dc.DrawCircle(plate_to_screen(seg.start), 5);
            dc.DrawCircle(plate_to_screen(seg.end),   5);
        }
    }

    // Edit mode: drag-in-progress preview
    if (m_is_dragging && m_dragging_ep.has_value()
            && m_dragging_ep->layer_index >= 0
            && m_dragging_ep->layer_index < m_session.layer_count()
            && m_dragging_ep->segment_index >= 0
            && m_dragging_ep->segment_index <
               (int)m_session.layers[m_dragging_ep->layer_index].segments.size()) {
        const DrawSegment& dseg =
            m_session.layers[m_dragging_ep->layer_index].segments[m_dragging_ep->segment_index];
        Vec2d fixed   = m_dragging_ep->is_start ? dseg.end : dseg.start;
        wxPoint pf    = plate_to_screen(fixed);
        wxPoint pd_ep = plate_to_screen(m_drag_preview);
        dc.SetPen(wxPen(wxColour(255, 255, 0), 2, wxPENSTYLE_SHORT_DASH));
        dc.DrawLine(pd_ep, pf);
        dc.SetPen(wxPen(wxColour(255, 255, 0), 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawCircle(pd_ep, 5);
    }

    // Crosshair at mouse
    wxPoint pm = plate_to_screen(m_mouse_plate);
    dc.SetPen(wxPen(wxColour(140, 140, 140), 1, wxPENSTYLE_DOT));
    dc.DrawLine(pm.x, CANVAS_PAD, pm.x, sz.y - CANVAS_PAD);
    dc.DrawLine(CANVAS_PAD, pm.y, sz.x - CANVAS_PAD, pm.y);
}

// ---------------------------------------------------------------------------
// Canvas mouse handlers
// ---------------------------------------------------------------------------

void DrawModePanel::on_canvas_left_down(wxMouseEvent& evt)
{
    Vec2d pos = screen_to_plate(evt.GetPosition());

    if (m_input_mode == DrawInputMode::Editing) {
        int active = m_session.active_layer;
        if (active < 0 || active >= m_session.layer_count()) return;
        const DrawLayer& layer = m_session.layers[active];

        // Threshold: ~8 screen pixels converted to plate-space mm
        wxSize sz = m_canvas->GetClientSize();
        double inner_w = std::max(1.0, (double)(sz.x - 2 * CANVAS_PAD));
        double thr = 8.0 / (inner_w / m_plate_w_mm);

        // Priority 1: endpoints
        int ep_seg = -1; bool ep_is_start = false; double best_ep = thr;
        for (int si = 0; si < (int)layer.segments.size(); ++si) {
            double ds = (pos - layer.segments[si].start).norm();
            double de = (pos - layer.segments[si].end).norm();
            if (ds < best_ep) { best_ep = ds; ep_seg = si; ep_is_start = true; }
            if (de < best_ep) { best_ep = de; ep_seg = si; ep_is_start = false; }
        }
        if (ep_seg >= 0) {
            Vec2d ep = ep_is_start ? layer.segments[ep_seg].start
                                   : layer.segments[ep_seg].end;
            m_dragging_ep   = EndpointRef{active, ep_seg, ep_is_start};
            m_drag_preview  = ep;
            m_is_dragging   = true;
            m_sel_layer_idx = -1;
            m_sel_seg_idx   = -1;
            if (!m_canvas->HasCapture()) m_canvas->CaptureMouse();
            m_canvas->Refresh(false);
            return;
        }

        // Priority 2: segment bodies
        int body_seg = -1; double best_body = thr;
        for (int si = 0; si < (int)layer.segments.size(); ++si) {
            double t;
            double d = DrawModeInputHandler::point_to_segment_distance(
                pos, layer.segments[si].start, layer.segments[si].end, t);
            if (d < best_body) { best_body = d; body_seg = si; }
        }
        m_sel_layer_idx = (body_seg >= 0) ? active : -1;
        m_sel_seg_idx   = body_seg;
        m_dragging_ep.reset();
        m_is_dragging = false;
        m_canvas->Refresh(false);
        return;
    }

    // Drawing mode
    int active = m_session.active_layer;
    if (active < 0 || active >= m_session.layer_count()) {
        wxMessageBox("Add a layer first — click '+ Layer'.",
                     "Draw Mode", wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (!m_pending_start) {
        m_pending_start = pos;
    } else {
        DrawSegment seg;
        seg.start     = *m_pending_start;
        seg.end       = pos;
        seg.is_travel = false;
        dispatch_command(std::make_unique<AddSegmentCommand>(active, std::move(seg)));
        m_pending_start = pos;
    }
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_canvas_right_down(wxMouseEvent&)
{
    // Cancel pending draw or active drag
    m_pending_start.reset();
    if (m_is_dragging) {
        if (m_canvas->HasCapture()) m_canvas->ReleaseMouse();
        m_is_dragging = false;
        m_dragging_ep.reset();
    }
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_canvas_left_up(wxMouseEvent& evt)
{
    if (!m_is_dragging || !m_dragging_ep.has_value()) { evt.Skip(); return; }
    if (m_canvas->HasCapture()) m_canvas->ReleaseMouse();

    const EndpointRef& ep = *m_dragging_ep;
    if (ep.layer_index >= 0 && ep.layer_index < m_session.layer_count()
            && ep.segment_index >= 0
            && ep.segment_index < (int)m_session.layers[ep.layer_index].segments.size()) {
        const DrawSegment& seg = m_session.layers[ep.layer_index].segments[ep.segment_index];
        Vec2d old_pos = ep.is_start ? seg.start : seg.end;
        Vec2d new_pos = m_drag_preview;
        // Only commit if actually moved
        if ((new_pos - old_pos).squaredNorm() > 1e-12) {
            dispatch_command(std::make_unique<MoveEndpointCommand>(
                ep.layer_index, ep.segment_index, ep.is_start, old_pos, new_pos));
        }
    }
    m_dragging_ep.reset();
    m_is_dragging = false;
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_canvas_motion(wxMouseEvent& evt)
{
    m_mouse_plate = screen_to_plate(evt.GetPosition());
    if (m_is_dragging)
        m_drag_preview = m_mouse_plate;
    if (m_canvas) m_canvas->Refresh(false);
    evt.Skip();
}

void DrawModePanel::on_draw_toggle(wxCommandEvent&)
{
    m_input_mode = DrawInputMode::Drawing;
    m_draw_toggle->SetValue(true);
    m_edit_toggle->SetValue(false);
    // Clear any pending edit state
    m_sel_layer_idx = -1;
    m_sel_seg_idx   = -1;
    m_dragging_ep.reset();
    m_is_dragging   = false;
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_edit_toggle(wxCommandEvent&)
{
    m_input_mode = DrawInputMode::Editing;
    m_edit_toggle->SetValue(true);
    m_draw_toggle->SetValue(false);
    // Cancel any in-progress draw
    m_pending_start.reset();
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_prev_layer(wxCommandEvent&)
{
    if (m_session.active_layer > 0) {
        m_session.active_layer--;
        refresh();
    }
}

void DrawModePanel::on_next_layer(wxCommandEvent&)
{
    int last = m_session.layer_count() - 1;
    if (m_session.active_layer < last) {
        m_session.active_layer++;
        refresh();
    }
}

void DrawModePanel::on_add_layer(wxCommandEvent&)
{
    // Default layer height — read from active print preset
    double lh = 0.2;
    if (wxGetApp().preset_bundle) {
        lh = wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_float("layer_height");
        if (lh <= 0.0) lh = 0.2;
    }
    dispatch_command(std::make_unique<AddLayerCommand>(lh));
}

void DrawModePanel::on_clear_layer(wxCommandEvent&)
{
    int active = m_session.active_layer;
    if (active < 0 || active >= m_session.layer_count()) return;
    dispatch_command(std::make_unique<ClearLayerCommand>(active));
}

// Build a TriangleMesh that visually represents the drawn paths as flat
// rectangular prisms: width = nozzle_diameter, height = layer_height.
// Each DrawSegment becomes one box oriented along the segment direction.
static TriangleMesh make_draw_path_mesh(const DrawSession& session, double nozzle_d)
{
    std::vector<Vec3f>   verts;
    std::vector<Vec3i32> faces;

    for (const DrawLayer& layer : session.layers) {
        const float zb  = static_cast<float>(layer.z_start);
        const float zt  = static_cast<float>(layer.z_end);
        const float hw  = static_cast<float>(nozzle_d / 2.0);

        for (const DrawSegment& seg : layer.segments) {
            Vec2d d   = seg.end - seg.start;
            double len = d.norm();
            if (len < 1e-6) continue;

            Vec2d dir  = d / len;
            // 2-D perpendicular (rotated 90° CCW)
            Vec2d perp(-dir.y(), dir.x());

            float ax = static_cast<float>(seg.start.x());
            float ay = static_cast<float>(seg.start.y());
            float bx = static_cast<float>(seg.end.x());
            float by = static_cast<float>(seg.end.y());
            float px = static_cast<float>(perp.x() * nozzle_d / 2.0);
            float py = static_cast<float>(perp.y() * nozzle_d / 2.0);

            // 8 vertices of the rectangular prism
            // Bottom ring (z = zb): v0..v3, Top ring (z = zt): v4..v7
            const int b = static_cast<int>(verts.size());
            verts.emplace_back(ax - px, ay - py, zb); // b+0 start-left  bot
            verts.emplace_back(ax + px, ay + py, zb); // b+1 start-right bot
            verts.emplace_back(bx + px, by + py, zb); // b+2 end-right   bot
            verts.emplace_back(bx - px, by - py, zb); // b+3 end-left    bot
            verts.emplace_back(ax - px, ay - py, zt); // b+4 start-left  top
            verts.emplace_back(ax + px, ay + py, zt); // b+5 start-right top
            verts.emplace_back(bx + px, by + py, zt); // b+6 end-right   top
            verts.emplace_back(bx - px, by - py, zt); // b+7 end-left    top

            // 12 triangles — winding gives outward normals (right-hand rule)
            // Bottom face (-Z normal)
            faces.emplace_back(b+0, b+1, b+2);
            faces.emplace_back(b+0, b+2, b+3);
            // Top face (+Z normal)
            faces.emplace_back(b+4, b+6, b+5);
            faces.emplace_back(b+4, b+7, b+6);
            // Start cap (-dir normal)
            faces.emplace_back(b+0, b+4, b+5);
            faces.emplace_back(b+0, b+5, b+1);
            // End cap (+dir normal)
            faces.emplace_back(b+2, b+6, b+3);
            faces.emplace_back(b+3, b+6, b+7);
            // Right side (+perp normal)
            faces.emplace_back(b+1, b+5, b+2);
            faces.emplace_back(b+2, b+5, b+6);
            // Left side (-perp normal)
            faces.emplace_back(b+0, b+3, b+7);
            faces.emplace_back(b+0, b+7, b+4);
        }
    }

    if (verts.empty()) return TriangleMesh();
    return TriangleMesh(verts, faces);
}

bool DrawModePanel::apply_session_to_model()
{
    if (m_session.is_empty()) {
        wxMessageBox("Nothing to finalize — draw some segments first.", "Draw Mode",
            wxOK | wxICON_INFORMATION, this);
        return false;
    }

    if (!m_plater) return false;

    // Refresh cached nozzle diameter from active printer preset
    m_nozzle_d = 0.4;
    if (wxGetApp().preset_bundle) {
        const auto& pcfg = wxGetApp().preset_bundle->printers.get_edited_preset().config;
        if (auto* nd = pcfg.option<ConfigOptionFloats>("nozzle_diameter"))
            if (!nd->values.empty()) m_nozzle_d = nd->values.front();
    }

    // Build a mesh that exactly represents the drawn paths (one box per segment)
    TriangleMesh path_mesh = make_draw_path_mesh(m_session, m_nozzle_d);
    if (path_mesh.empty()) {
        wxMessageBox("No drawable segments found.", "Draw Mode",
            wxOK | wxICON_INFORMATION, this);
        return false;
    }

    Model& model = m_plater->model();

    if (m_editing_obj_idx >= 0 && m_editing_obj_idx < (int)model.objects.size()) {
        // Re-edit path: replace mesh and session in place, preserve instances.
        ModelObject* obj = model.objects[m_editing_obj_idx];

        // Remove all existing mesh volumes (the previous ribbon mesh).
        while (!obj->volumes.empty())
            obj->delete_volume(0);

        // Add the freshly-built mesh as the single MODEL_PART volume.
        // Pass false so the mesh is NOT auto-centered — it is already in
        // plate-relative coordinates.
        obj->add_volume(std::move(path_mesh), ModelVolumeType::MODEL_PART, false);

        // Update stored draw session and config flag.
        obj->draw_session = std::make_unique<DrawSession>(m_session);
        obj->config.set_key_value("draw_path_object", new ConfigOptionBool(true));

        // Invalidate cached bounding boxes so the 3D view re-computes them.
        obj->invalidate_bounding_box();
    } else {
        // TASK-019: guard against mixing draw-path objects with normal 3D models
        // on the same plate. Check the current plate before adding.
        PartPlate* curr_plate = m_plater->get_partplate_list().get_curr_plate();
        if (curr_plate) {
            auto existing = curr_plate->get_objects_on_this_plate();
            bool plate_has_normal = std::any_of(existing.begin(), existing.end(),
                [](const ModelObject* o){ return !o->is_draw_path_object(); });
            if (plate_has_normal) {
                wxMessageBox(
                    "A plate cannot mix drawn-path objects with normal 3D models.\n"
                    "Please move existing objects to a different plate first,\n"
                    "or use a fresh plate for your drawing.",
                    "Draw Mode", wxOK | wxICON_WARNING, this);
                return false;
            }
        }

        // New object
        ModelObject* obj = model.add_object("DrawPathObject", "", std::move(path_mesh));
        obj->config.set_key_value("draw_path_object", new ConfigOptionBool(true));
        obj->draw_session = std::make_unique<DrawSession>(m_session);

        // Instance offset: segments are in plate-relative coords,
        // so place the object at the plate's world origin.
        ModelInstance* inst = obj->add_instance();
        inst->set_offset(Vec3d(m_plate_x, m_plate_y, 0.0));
    }

    m_plater->update();
    wxGetApp().obj_list()->update_after_undo_redo();

    // Reset panel state so user can start a fresh drawing session.
    m_editing_obj_idx = -1;
    m_session         = DrawSession();
    m_undo_stack.clear();
    m_redo_stack.clear();
    m_pending_start.reset();
    update_banner();
    update_layer_label();

    return true;
}

void DrawModePanel::on_simulate(wxCommandEvent&)
{
    if (!apply_session_to_model()) return;

    // Trigger the slicing pipeline — BackgroundSlicingProcess will detect
    // the all-draw-path plate and call DrawPathGCodeGenerator directly.
    m_plater->reslice();

    // Switch to Preview tab so the G-code viewer shows the result.
    wxGetApp().mainframe->select_tab((size_t)MainFrame::tpPreview);
}

void DrawModePanel::on_finalize(wxCommandEvent&)
{
    if (!apply_session_to_model()) return;

    // Switch back to 3D Editor tab.
    wxGetApp().mainframe->select_tab((size_t)MainFrame::tp3DEditor);
}

void DrawModePanel::on_char_hook(wxKeyEvent& evt)
{
    if (!IsShownOnScreen()) {
        evt.Skip();
        return;
    }

    if (evt.ControlDown()) {
        if (evt.GetKeyCode() == 'Z' && !m_undo_stack.empty()) {
            auto cmd = std::move(m_undo_stack.back());
            m_undo_stack.pop_back();
            cmd->undo(m_session);
            m_redo_stack.push_back(std::move(cmd));
            refresh();
            return; // swallowed
        }
        if (evt.GetKeyCode() == 'Y' && !m_redo_stack.empty()) {
            auto cmd = std::move(m_redo_stack.back());
            m_redo_stack.pop_back();
            cmd->execute(m_session);
            m_undo_stack.push_back(std::move(cmd));
            refresh();
            return; // swallowed
        }
    }

    // Delete/Backspace in Editing mode removes the selected segment
    if (m_input_mode == DrawInputMode::Editing && m_sel_seg_idx >= 0) {
        int kc = evt.GetKeyCode();
        if (kc == WXK_DELETE || kc == WXK_BACK) {
            dispatch_command(std::make_unique<DeleteSegmentCommand>(m_sel_layer_idx, m_sel_seg_idx));
            m_sel_layer_idx = -1;
            m_sel_seg_idx   = -1;
            return;
        }
    }
    evt.Skip();
}

void DrawModePanel::on_fill_toggle(wxCommandEvent&)
{
    m_show_filled = m_fill_toggle->GetValue();
    if (m_canvas) m_canvas->Refresh(false);
}

} // namespace GUI
} // namespace Slic3r
