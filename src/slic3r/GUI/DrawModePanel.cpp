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
    m_draw_toggle->SetValue(true);

    // Action buttons
    m_clear_btn    = new wxButton(this, wxID_ANY, "Clear Layer");
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
    top_sizer->AddStretchSpacer();
    top_sizer->Add(m_clear_btn,    0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
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
    m_prev_layer_btn->Bind(wxEVT_BUTTON, &DrawModePanel::on_prev_layer, this);
    m_next_layer_btn->Bind(wxEVT_BUTTON, &DrawModePanel::on_next_layer, this);
    m_add_layer_btn->Bind(wxEVT_BUTTON,  &DrawModePanel::on_add_layer,  this);
    m_clear_btn->Bind(wxEVT_BUTTON,      &DrawModePanel::on_clear_layer, this);
    m_finalize_btn->Bind(wxEVT_BUTTON,   &DrawModePanel::on_finalize,    this);
    Bind(wxEVT_CHAR_HOOK,                &DrawModePanel::on_char_hook,   this);

    // Canvas events
    m_canvas->Bind(wxEVT_PAINT,       &DrawModePanel::on_canvas_paint,      this);
    m_canvas->Bind(wxEVT_ERASE_BACKGROUND, &DrawModePanel::on_canvas_erase_bg, this);
    m_canvas->Bind(wxEVT_LEFT_DOWN,   &DrawModePanel::on_canvas_left_down,  this);
    m_canvas->Bind(wxEVT_RIGHT_DOWN,  &DrawModePanel::on_canvas_right_down, this);
    m_canvas->Bind(wxEVT_MOTION,      &DrawModePanel::on_canvas_motion,     this);
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

    // Segments (inactive layers dim, active layer bright)
    int active = m_session.active_layer;
    for (int li = 0; li < m_session.layer_count(); ++li) {
        bool is_active = (li == active);
        wxColour col = is_active ? wxColour(80, 220, 100) : wxColour(50, 90, 55);
        int width = is_active ? 2 : 1;
        dc.SetPen(wxPen(col, width));
        dc.SetBrush(wxBrush(col));
        for (const DrawSegment& seg : m_session.layers[li].segments) {
            wxPoint p1 = plate_to_screen(seg.start);
            wxPoint p2 = plate_to_screen(seg.end);
            dc.DrawLine(p1, p2);
            if (is_active) {
                dc.DrawCircle(p1, 3);
                dc.DrawCircle(p2, 3);
            }
        }
    }

    // Pending start point + preview line
    if (m_pending_start) {
        wxPoint ps = plate_to_screen(*m_pending_start);
        wxPoint pm = plate_to_screen(m_mouse_plate);

        // Preview line (dashed yellow)
        dc.SetPen(wxPen(wxColour(255, 200, 50), 1, wxPENSTYLE_SHORT_DASH));
        dc.DrawLine(ps, pm);

        // Hollow circle at pending start
        dc.SetPen(wxPen(wxColour(255, 200, 50), 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawCircle(ps, 5);
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
    if (m_input_mode != DrawInputMode::Drawing) { evt.Skip(); return; }

    int active = m_session.active_layer;
    if (active < 0 || active >= m_session.layer_count()) {
        wxMessageBox("Add a layer first — click '+ Layer'.",
                     "Draw Mode", wxOK | wxICON_INFORMATION, this);
        return;
    }

    Vec2d pos = screen_to_plate(evt.GetPosition());

    if (!m_pending_start) {
        // First click: record start point
        m_pending_start = pos;
    } else {
        // Second click: commit segment, chain next start
        DrawSegment seg;
        seg.start     = *m_pending_start;
        seg.end       = pos;
        seg.is_travel = false;
        dispatch_command(std::make_unique<AddSegmentCommand>(active, std::move(seg)));

        // Chain: end of previous segment is start of next
        m_pending_start = pos;
    }

    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_canvas_right_down(wxMouseEvent&)
{
    // Cancel pending start
    m_pending_start.reset();
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_canvas_motion(wxMouseEvent& evt)
{
    m_mouse_plate = screen_to_plate(evt.GetPosition());
    if (m_canvas) m_canvas->Refresh(false);
    evt.Skip();
}

void DrawModePanel::on_draw_toggle(wxCommandEvent&)
{
    m_input_mode = DrawInputMode::Drawing;
    m_draw_toggle->SetValue(true);
    m_edit_toggle->SetValue(false);
}

void DrawModePanel::on_edit_toggle(wxCommandEvent&)
{
    m_input_mode = DrawInputMode::Editing;
    m_edit_toggle->SetValue(true);
    m_draw_toggle->SetValue(false);
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

void DrawModePanel::on_finalize(wxCommandEvent&)
{
    if (m_session.is_empty()) {
        wxMessageBox("Nothing to finalize — draw some segments first.", "Draw Mode",
            wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (!m_plater) return;

    // Read nozzle diameter from active printer preset
    double nozzle_d = 0.4;
    if (wxGetApp().preset_bundle) {
        const auto& pcfg = wxGetApp().preset_bundle->printers.get_edited_preset().config;
        if (auto* nd = pcfg.option<ConfigOptionFloats>("nozzle_diameter"))
            if (!nd->values.empty()) nozzle_d = nd->values.front();
    }

    // Build a mesh that exactly represents the drawn paths (one box per segment)
    TriangleMesh path_mesh = make_draw_path_mesh(m_session, nozzle_d);
    if (path_mesh.empty()) {
        wxMessageBox("No drawable segments found.", "Draw Mode",
            wxOK | wxICON_INFORMATION, this);
        return;
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
                return;
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

    // Switch back to 3D Editor tab
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
    evt.Skip();
}

} // namespace GUI
} // namespace Slic3r
