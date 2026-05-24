#include "DrawModePanel.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "PartPlate.hpp"
#include "MainFrame.hpp"
#include "GUI_ObjectList.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/DrawModeFeedback.hpp"
#include "libslic3r/DrawPathMesh.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Config.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/tglbtn.h>
#include <wx/textctrl.h>
#include <wx/msgdlg.h>
#include <wx/dcbuffer.h>
#include <wx/choice.h>

#include <algorithm>
#include <cmath>
#include <iterator>

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
    m_remove_layer_btn = new wxButton(this, wxID_ANY, "- Layer");
    m_add_layer_btn  = new wxButton(this, wxID_ANY, "+ Layer");
    m_delete_layer_btn = new wxButton(this, wxID_ANY, "Delete Layer");
    m_remove_layer_btn->SetToolTip("Go down a layer.\nIf the current layer is empty it will be deleted.");
    m_delete_layer_btn->SetToolTip("Delete the current layer and all its segments.");

    m_copy_layer_btn  = new wxButton(this, wxID_ANY, "Copy Layer");
    m_paste_layer_btn = new wxButton(this, wxID_ANY, "Paste Layer");
    m_copy_prev_btn   = new wxButton(this, wxID_ANY, "Copy From Prev");
    m_copy_layer_btn->SetToolTip("Copy all segments from the current layer to the clipboard.");
    m_paste_layer_btn->SetToolTip("Paste the clipboard segments into the current layer (additive — does not remove existing segments).");
    m_copy_prev_btn->SetToolTip("Add all segments from the previous layer to the current layer (additive, single undo step).");
    m_paste_layer_btn->Disable(); // no clipboard yet
    m_copy_prev_btn->Disable();   // no previous layer yet

    // Mode toggles
    m_draw_toggle = new wxToggleButton(this, wxID_ANY, "Draw");
    m_edit_toggle = new wxToggleButton(this, wxID_ANY, "Edit");
    m_line_tool_btn  = new wxToggleButton(this, wxID_ANY, "Line");
    m_arc_tool_btn   = new wxToggleButton(this, wxID_ANY, "Arc");
    m_curve_tool_btn = new wxToggleButton(this, wxID_ANY, "Curve");
    m_line_tool_btn->SetToolTip("Draw straight lines (2 clicks)");
    m_arc_tool_btn->SetToolTip("Draw circular arcs (3 clicks: start, through-point, end)");
    m_curve_tool_btn->SetToolTip("Draw cubic bezier curves (4 clicks: start, ctrl1, ctrl2, end)");
    m_line_tool_btn->SetValue(true); // Line is the default
    m_fill_toggle = new wxToggleButton(this, wxID_ANY, "Fill Width");
    m_snap_toggle = new wxToggleButton(this, wxID_ANY, "Snap");
    {
        const wxString grid_choices[] = {
            "0.05 mm",
            "0.1 mm", "0.2 mm", "0.3 mm", "0.4 mm", "0.5 mm",
            "0.6 mm", "0.7 mm", "0.8 mm", "0.9 mm", "1.0 mm"
        };
        m_grid_res_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(72, -1),
            11, grid_choices);
        m_grid_res_choice->SetSelection(4); // default: 0.4 mm
        m_grid_res_choice->SetToolTip("Snap-to-grid resolution");
    }
    m_measure_toggle = new wxToggleButton(this, wxID_ANY, "Show Measurements");
    m_coord_toggle = new wxToggleButton(this, wxID_ANY, "Show Coordinates");
    m_length_input = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1), wxTE_PROCESS_ENTER);
    m_draw_toggle->SetValue(true);
    m_snap_toggle->SetValue(true); // snap on by default
    m_measure_toggle->SetValue(true);
    m_coord_toggle->SetValue(true);
    m_length_input->SetHint("Length mm");

    // Action buttons
    m_snip_btn     = new wxButton(this, wxID_ANY, "Snip");
    m_clear_btn    = new wxButton(this, wxID_ANY, "Clear Layer");
    m_simulate_btn = new wxButton(this, wxID_ANY, "Simulate");
    m_finalize_btn = new wxButton(this, wxID_ANY, "Finalize");
    m_snip_btn->SetToolTip("Break the continuous drawing chain here.\n"
                           "Next click starts a new disconnected path (printer travels).\n"
                           "Shortcut: Right-click or Escape.");

    // Interactive 2-D drawing canvas
    m_canvas = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS);
    m_canvas->SetBackgroundStyle(wxBG_STYLE_PAINT); // suppress default erase
    m_canvas->SetBackgroundColour(wxColour(55, 55, 55));
    m_canvas->SetCursor(wxCursor(wxCURSOR_CROSS));

    // Layout
    auto* top_sizer = new wxBoxSizer(wxHORIZONTAL);
    top_sizer->Add(m_banner_text, 1, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_draw_toggle, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_edit_toggle, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_line_tool_btn,  0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_arc_tool_btn,   0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_curve_tool_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_fill_toggle, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_snap_toggle, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(new wxStaticText(this, wxID_ANY, "Grid:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_grid_res_choice, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_measure_toggle, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_coord_toggle, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(new wxStaticText(this, wxID_ANY, "Length:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_length_input, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_snip_btn,   0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->AddStretchSpacer();
    top_sizer->Add(m_clear_btn,    0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_simulate_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    top_sizer->Add(m_finalize_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);

    auto* nav_sizer = new wxBoxSizer(wxHORIZONTAL);
    nav_sizer->Add(m_prev_layer_btn,   0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    nav_sizer->Add(m_layer_label,      1, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    nav_sizer->Add(m_next_layer_btn,   0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    nav_sizer->Add(m_remove_layer_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    nav_sizer->Add(m_add_layer_btn,    0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    nav_sizer->Add(m_delete_layer_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    nav_sizer->Add(m_copy_layer_btn,  0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    nav_sizer->Add(m_paste_layer_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    nav_sizer->Add(m_copy_prev_btn,   0, wxALL | wxALIGN_CENTER_VERTICAL, 4);

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(top_sizer, 0, wxEXPAND);
    main_sizer->Add(nav_sizer, 0, wxEXPAND);
    main_sizer->Add(m_canvas, 1, wxEXPAND | wxALL, 8);

    SetSizer(main_sizer);

    // Bind events
    m_draw_toggle->Bind(wxEVT_TOGGLEBUTTON, &DrawModePanel::on_draw_toggle, this);
    m_edit_toggle->Bind(wxEVT_TOGGLEBUTTON, &DrawModePanel::on_edit_toggle, this);
    m_line_tool_btn->Bind(wxEVT_TOGGLEBUTTON,  &DrawModePanel::on_line_tool,  this);
    m_arc_tool_btn->Bind(wxEVT_TOGGLEBUTTON,   &DrawModePanel::on_arc_tool,   this);
    m_curve_tool_btn->Bind(wxEVT_TOGGLEBUTTON, &DrawModePanel::on_curve_tool, this);
    m_fill_toggle->Bind(wxEVT_TOGGLEBUTTON, &DrawModePanel::on_fill_toggle, this);
    m_snap_toggle->Bind(wxEVT_TOGGLEBUTTON, &DrawModePanel::on_snap_toggle, this);
    m_grid_res_choice->Bind(wxEVT_CHOICE, &DrawModePanel::on_grid_res_change, this);
    m_measure_toggle->Bind(wxEVT_TOGGLEBUTTON, &DrawModePanel::on_measure_toggle, this);
    m_coord_toggle->Bind(wxEVT_TOGGLEBUTTON, &DrawModePanel::on_coord_toggle, this);
    m_length_input->Bind(wxEVT_TEXT, &DrawModePanel::on_length_text, this);
    m_length_input->Bind(wxEVT_TEXT_ENTER, &DrawModePanel::on_length_enter, this);
    m_snip_btn->Bind(wxEVT_BUTTON, &DrawModePanel::on_snip, this);
    m_prev_layer_btn->Bind(wxEVT_BUTTON, &DrawModePanel::on_prev_layer, this);
    m_next_layer_btn->Bind(wxEVT_BUTTON, &DrawModePanel::on_next_layer, this);
    m_add_layer_btn->Bind(wxEVT_BUTTON,  &DrawModePanel::on_add_layer,  this);
    m_remove_layer_btn->Bind(wxEVT_BUTTON, &DrawModePanel::on_remove_layer, this);
    m_delete_layer_btn->Bind(wxEVT_BUTTON, &DrawModePanel::on_delete_layer, this);
    m_clear_btn->Bind(wxEVT_BUTTON,      &DrawModePanel::on_clear_layer, this);
    m_copy_layer_btn->Bind(wxEVT_BUTTON,  &DrawModePanel::on_copy_layer,     this);
    m_paste_layer_btn->Bind(wxEVT_BUTTON, &DrawModePanel::on_paste_layer,    this);
    m_copy_prev_btn->Bind(wxEVT_BUTTON,   &DrawModePanel::on_copy_from_prev, this);
    m_simulate_btn->Bind(wxEVT_BUTTON,   &DrawModePanel::on_simulate,    this);
    m_finalize_btn->Bind(wxEVT_BUTTON,   &DrawModePanel::on_finalize,    this);
    Bind(wxEVT_CHAR_HOOK,                &DrawModePanel::on_char_hook,   this);
    Bind(wxEVT_KEY_UP,                   &DrawModePanel::on_key_up,      this);

    // Canvas events
    m_canvas->Bind(wxEVT_PAINT,       &DrawModePanel::on_canvas_paint,      this);
    m_canvas->Bind(wxEVT_ERASE_BACKGROUND, &DrawModePanel::on_canvas_erase_bg, this);
    m_canvas->Bind(wxEVT_LEFT_DOWN,   &DrawModePanel::on_canvas_left_down,  this);
    m_canvas->Bind(wxEVT_LEFT_UP,     &DrawModePanel::on_canvas_left_up,   this);
    m_canvas->Bind(wxEVT_RIGHT_DOWN,  &DrawModePanel::on_canvas_right_down, this);
    m_canvas->Bind(wxEVT_MOTION,      &DrawModePanel::on_canvas_motion,     this);
    m_canvas->Bind(wxEVT_MOUSEWHEEL,  &DrawModePanel::on_canvas_mouse_wheel, this);
    m_canvas->Bind(wxEVT_MIDDLE_DOWN, &DrawModePanel::on_canvas_middle_down, this);
    m_canvas->Bind(wxEVT_MIDDLE_UP,   &DrawModePanel::on_canvas_middle_up,   this);
    m_canvas->Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
        // Capture lost externally  -  clean up drag state without calling ReleaseMouse
        m_is_dragging = false;
        m_dragging_ep.reset();
        m_dragging_ctrl.reset();
        m_pan_start.reset();
        m_canvas->SetCursor(wxCursor(wxCURSOR_CROSS));
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
    reset_draft(false);
    m_zoom_factor = DRAW_MODE_DEFAULT_ZOOM_FACTOR;
    m_pan_offset = Vec2d(0.0, 0.0);
    m_pan_start.reset();

    if (m_plater) {
        const DrawModeDisplayPreferences& prefs = m_plater->model().draw_mode_display_preferences;
        m_show_measurements = prefs.show_measurements;
        m_show_coordinates  = prefs.show_coordinates;
        if (m_measure_toggle) m_measure_toggle->SetValue(m_show_measurements);
        if (m_coord_toggle) m_coord_toggle->SetValue(m_show_coordinates);
    }

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
    m_dragging_ctrl.reset();
    m_is_dragging   = false;

    // Reset tool selection
    m_draw_tool = DrawTool::Line;
    if (m_line_tool_btn)  m_line_tool_btn->SetValue(true);
    if (m_arc_tool_btn)   m_arc_tool_btn->SetValue(false);
    if (m_curve_tool_btn) m_curve_tool_btn->SetValue(false);

    if (restore_existing_draw_object(plate))
        return;

    m_layer_clipboard.reset();
    update_copy_paste_buttons();
    update_banner();
    update_layer_label();
    if (m_canvas) m_canvas->Refresh();
}

void DrawModePanel::reactivate(PartPlate* plate)
{
    // Cancel any in-progress rubber-band draw, but keep committed segments.
    reset_draft(false);
    m_pending_start.reset();

    if (plate) {
        Vec3d origin = plate->get_origin();
        m_plate_x = origin.x();
        m_plate_y = origin.y();
    }

    // Refresh nozzle diameter from current preset in case it changed.
    m_nozzle_d = 0.4;
    if (wxGetApp().preset_bundle) {
        const auto& pcfg = wxGetApp().preset_bundle->printers.get_edited_preset().config;
        if (auto* nd = pcfg.option<ConfigOptionFloats>("nozzle_diameter"))
            if (!nd->values.empty()) m_nozzle_d = nd->values.front();
    }

    if (m_plater) {
        const DrawModeDisplayPreferences& prefs = m_plater->model().draw_mode_display_preferences;
        m_show_measurements = prefs.show_measurements;
        m_show_coordinates  = prefs.show_coordinates;
        if (m_measure_toggle) m_measure_toggle->SetValue(m_show_measurements);
        if (m_coord_toggle) m_coord_toggle->SetValue(m_show_coordinates);
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
    m_pending_start.reset();
    reset_draft(false);
    m_zoom_factor = DRAW_MODE_DEFAULT_ZOOM_FACTOR;
    m_pan_offset = Vec2d(0.0, 0.0);
    m_pan_start.reset();

    // Reset tool selection
    m_draw_tool = DrawTool::Line;
    if (m_line_tool_btn)  m_line_tool_btn->SetValue(true);
    if (m_arc_tool_btn)   m_arc_tool_btn->SetValue(false);
    if (m_curve_tool_btn) m_curve_tool_btn->SetValue(false);

    if (m_plater) {
        const DrawModeDisplayPreferences& prefs = m_plater->model().draw_mode_display_preferences;
        m_show_measurements = prefs.show_measurements;
        m_show_coordinates  = prefs.show_coordinates;
        if (m_measure_toggle) m_measure_toggle->SetValue(m_show_measurements);
        if (m_coord_toggle) m_coord_toggle->SetValue(m_show_coordinates);
    }

    update_banner();
    update_layer_label();
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::refresh()
{
    update_layer_label();
    update_copy_paste_buttons();
    if (m_canvas) m_canvas->Refresh(false);
}

bool DrawModePanel::restore_existing_draw_object(PartPlate* plate)
{
    if (m_plater == nullptr)
        return false;

    Model& model = m_plater->model();
    std::vector<ModelObject*> candidates;
    if (plate != nullptr)
        candidates = plate->get_objects_on_this_plate();
    else
        candidates = model.objects;

    ModelObject* object = draw_find_first_restorable_draw_object(candidates);
    if (object == nullptr)
        return false;

    auto model_it = std::find(model.objects.begin(), model.objects.end(), object);
    if (model_it == model.objects.end())
        return false;

    load_for_edit(object, static_cast<int>(std::distance(model.objects.begin(), model_it)));
    return true;
}

void DrawModePanel::update_banner()
{
    wxString info = "Draw Mode";
    if (m_editing_obj_idx >= 0)
        info += wxString::Format(" - Editing object %d", m_editing_obj_idx + 1);
    if (m_banner_text)
        m_banner_text->SetLabel(info);
}

void DrawModePanel::update_layer_label()
{
    if (!m_layer_label) return;

    const int count  = m_session.layer_count();
    const int active = (count > 0) ? m_session.active_layer : -1;

    // Display counter: 0 before any layer is added, then 1, 2, 3, ...
    const int display_layer = (active < 0) ? 0 : active + 1;
    m_layer_label->SetLabel(wxString::Format("Layer: %d", display_layer));

    // Enable / disable navigation and layer action buttons.
    const bool has_layers    = count > 0;
    const bool active_valid  = has_layers && active >= 0 && active < count;
    const bool layer_empty   = active_valid && m_session.layers[active].segments.empty();
    // "- Layer": enabled when there is somewhere meaningful to go:
    //   • active layer is empty  (can delete it even on layer 0)
    //   • OR there is a layer below the current one
    const bool can_remove = has_layers && (layer_empty || active > 0);

    if (m_prev_layer_btn)    m_prev_layer_btn->Enable(active > 0);
    if (m_next_layer_btn)    m_next_layer_btn->Enable(has_layers && active < count - 1);
    if (m_remove_layer_btn)  m_remove_layer_btn->Enable(can_remove);
    if (m_delete_layer_btn)  m_delete_layer_btn->Enable(has_layers);
    // "+ Layer" navigates to the next existing layer when one is above, or
    // creates a new layer when already at the top. The button is disabled only
    // when the user is at the top AND the current layer is empty (no point
    // creating another empty layer on top of an empty one).
    // When not at the top, the button always navigates up — even from an empty
    // layer — so the user can reach higher layers they navigated away from.
    // When there are no layers yet the button stays enabled (creates the first).
    const bool at_top  = !has_layers || (active_valid && active == count - 1);
    const bool can_add = !at_top || !layer_empty;
    if (m_add_layer_btn)     m_add_layer_btn->Enable(can_add);
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

    void draw_scale_indicator(wxDC& dc, const wxSize& canvas_size, double scale_px_per_mm)
    {
        const double inner_w = std::max(1.0, static_cast<double>(canvas_size.x - 2 * CANVAS_PAD));
        const int bar_length_px = (scale_px_per_mm > 0)
            ? std::max(1, static_cast<int>(std::round(DRAW_MODE_SCALE_GRID_MM * scale_px_per_mm)))
            : 0;
        if (bar_length_px <= 0)
            return;

        const wxString scale_label = wxString::Format("%.0f mm", DRAW_MODE_SCALE_GRID_MM);
        dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        const wxSize label_sz = dc.GetTextExtent(scale_label);

        const int pad = 5;
        const int box_w = std::max(bar_length_px, label_sz.x) + 2 * pad;
        const int box_h = label_sz.y + 18 + 2 * pad;
        const int box_x = std::max(CANVAS_PAD, canvas_size.x - CANVAS_PAD - box_w);
        const int box_y = std::max(CANVAS_PAD, canvas_size.y - CANVAS_PAD - box_h);
        const int bar_x = box_x + (box_w - bar_length_px) / 2;
        const int bar_y = box_y + box_h - pad - 8;

        dc.SetPen(wxPen(wxColour(20, 20, 20), 1));
        dc.SetBrush(wxBrush(wxColour(45, 45, 45, 220)));
        dc.DrawRoundedRectangle(box_x, box_y, box_w, box_h, 3);

        dc.SetPen(wxPen(wxColour(220, 220, 220), 2));
        dc.DrawLine(bar_x, bar_y, bar_x + bar_length_px, bar_y);
        dc.DrawLine(bar_x, bar_y - 4, bar_x, bar_y + 4);
        dc.DrawLine(bar_x + bar_length_px, bar_y - 4, bar_x + bar_length_px, bar_y + 4);

        dc.SetTextForeground(wxColour(220, 220, 220));
        dc.DrawText(scale_label, box_x + (box_w - label_sz.x) / 2, box_y + pad);
    }

    void draw_feedback_label(wxDC& dc, const wxString& text, wxPoint pos, const wxSize& canvas_size)
    {
        constexpr int pad = 4;
        wxSize text_size = dc.GetTextExtent(text);
        pos.x = std::clamp(pos.x, CANVAS_PAD, std::max(CANVAS_PAD, canvas_size.x - text_size.x - 2 * pad - CANVAS_PAD));
        pos.y = std::clamp(pos.y, CANVAS_PAD, std::max(CANVAS_PAD, canvas_size.y - text_size.y - 2 * pad - CANVAS_PAD));

        dc.SetPen(wxPen(wxColour(20, 20, 20), 1));
        dc.SetBrush(wxBrush(wxColour(245, 245, 220)));
        dc.DrawRoundedRectangle(pos.x, pos.y, text_size.x + 2 * pad, text_size.y + 2 * pad, 3);
        dc.SetTextForeground(wxColour(20, 20, 20));
        dc.DrawText(text, pos.x + pad, pos.y + pad);
    }
}

DrawModePanel::DrawTransform DrawModePanel::get_draw_transform() const
{
    DrawTransform t{};
    if (!m_canvas) return t;
    wxSize sz = m_canvas->GetClientSize();
    double inner_w = sz.x - 2 * CANVAS_PAD;
    double inner_h = sz.y - 2 * CANVAS_PAD;
    if (inner_w <= 0 || inner_h <= 0) return t;

    t.visible_w = m_plate_w_mm / m_zoom_factor;
    t.visible_h = m_plate_h_mm / m_zoom_factor;

    // Uniform scale: fit visible plate area in canvas while preserving 1:1 aspect ratio
    // so that 10 mm looks visually the same in both X and Y (square grid cells).
    t.scale = std::min(inner_w / t.visible_w, inner_h / t.visible_h);

    t.draw_w = t.scale * t.visible_w;
    t.draw_h = t.scale * t.visible_h;

    // Center the drawing area within the inner canvas area.
    t.draw_x0 = CANVAS_PAD + (inner_w - t.draw_w) / 2.0;
    t.draw_y0 = CANVAS_PAD + (inner_h - t.draw_h) / 2.0;
    return t;
}

Vec2d DrawModePanel::screen_to_plate(wxPoint pt) const
{
    if (!m_canvas) return Vec2d(0.0, 0.0);
    const DrawTransform t = get_draw_transform();
    if (t.scale <= 0) return Vec2d(0.0, 0.0);

    // Plate origin is at the canvas centre; coords run -half_w..+half_w and -half_h..+half_h.
    const double half_w = m_plate_w_mm * 0.5;
    const double half_h = m_plate_h_mm * 0.5;
    double px = -half_w + m_pan_offset.x() + (pt.x - t.draw_x0) / t.scale;
    // Screen Y grows downward; plate Y grows upward.
    double py = -half_h + m_pan_offset.y() + (t.draw_h - (pt.y - t.draw_y0)) / t.scale;
    return Vec2d(px, py);
}

wxPoint DrawModePanel::plate_to_screen(Vec2d pt) const
{
    if (!m_canvas) return wxPoint(0, 0);
    const DrawTransform t = get_draw_transform();

    // Inverse of screen_to_plate: plate centre (0,0) maps to the canvas centre.
    const double half_w = m_plate_w_mm * 0.5;
    const double half_h = m_plate_h_mm * 0.5;
    int sx = static_cast<int>(t.draw_x0 + (pt.x() + half_w - m_pan_offset.x()) * t.scale);
    int sy = static_cast<int>(t.draw_y0 + (t.visible_h - (pt.y() + half_h - m_pan_offset.y())) * t.scale);
    return wxPoint(sx, sy);
}

Vec2d DrawModePanel::snap_pos(Vec2d pt) const
{
    if (!m_snap_to_grid || m_grid_spacing < 1e-6) return pt;
    const double g = m_grid_spacing;
    return Vec2d(std::round(pt.x() / g) * g,
                 std::round(pt.y() / g) * g);
}

DrawDirectionSnapMode DrawModePanel::current_snap_mode(bool ctrl_down, bool alt_down) const
{
    return draw_resolve_snap_mode(ctrl_down, alt_down);
}

double DrawModePanel::draft_relative_angle_degrees(const Vec2d& start, const Vec2d& end) const
{
    const int active = m_session.active_layer;
    if (active < 0 || active >= m_session.layer_count() || m_session.layers[active].segments.empty())
        return 0.0;

    const DrawSegment& previous = m_session.layers[active].segments.back();
    return draw_relative_angle_degrees(previous.start, previous.end, start, end);
}

void DrawModePanel::sync_length_input()
{
    if (!m_length_input)
        return;

    m_updating_length_input = true;
    m_length_input->ChangeValue(m_draft.has_typed_length ? m_draft.typed_length_text : wxString());
    m_updating_length_input = false;
}

void DrawModePanel::set_typed_length_text(const wxString& text)
{
    if (!m_pending_start)
        return;

    m_draft.has_typed_length = !text.empty();
    m_draft.typed_length_text = text;
    sync_length_input();
    const bool diagonal_snap = m_draft.snap_mode == DrawDirectionSnapMode::Diagonal45;
    const bool free_snap = m_draft.snap_mode == DrawDirectionSnapMode::Free;
    update_draft(m_draft.raw_mouse, diagonal_snap, free_snap);
}

void DrawModePanel::update_draft(Vec2d raw_mouse, bool ctrl_down, bool alt_down)
{
    if (!m_pending_start) {
        m_draft.active = false;
        sync_length_input();
        return;
    }

    m_draft.active = true;
    m_draft.start = *m_pending_start;
    m_draft.raw_mouse = raw_mouse;
    m_draft.grid_snapped_mouse = snap_pos(raw_mouse);
    m_draft.snap_mode = current_snap_mode(ctrl_down, alt_down);

    Vec2d constrained = draw_apply_direction_snap(m_draft.start, m_draft.grid_snapped_mouse, m_draft.snap_mode);
    if (m_draft.has_typed_length) {
        const std::optional<double> typed_length = draw_parse_length_mm(m_draft.typed_length_text.ToStdString());
        if (typed_length)
            constrained = draw_project_typed_length(m_draft.start, constrained, *typed_length);
    }

    m_draft.constrained_end = constrained;
    m_draft.length_mm = draw_segment_length_mm(m_draft.start, m_draft.constrained_end);
    m_draft.angle_degrees = draft_relative_angle_degrees(m_draft.start, m_draft.constrained_end);
}

void DrawModePanel::reset_draft(bool keep_chain_anchor)
{
    if (!keep_chain_anchor)
        m_pending_start.reset();
    m_draft = DrawDraftSegment();
    sync_length_input();
}

bool DrawModePanel::commit_draft_segment()
{
    const int active = m_session.active_layer;
    if (!m_draft.active || active < 0 || active >= m_session.layer_count())
        return false;
    if (m_draft.length_mm <= DRAW_MODE_MIN_SEGMENT_LENGTH_MM)
        return false;

    Vec2d start = m_draft.start;
    Vec2d end   = m_draft.constrained_end;
    if ((end - start).squaredNorm() < 1e-12)
        return false;

    DrawSegment new_seg;
    if (m_draw_tool == DrawTool::Arc && m_draft.intermediate_clicks.size() >= 1) {
        new_seg = DrawSegment::make_arc(start, m_draft.intermediate_clicks[0], end);
    } else if (m_draw_tool == DrawTool::Bezier && m_draft.intermediate_clicks.size() >= 2) {
        new_seg = DrawSegment::make_bezier(start, m_draft.intermediate_clicks[0],
                                            m_draft.intermediate_clicks[1], end);
    } else {
        new_seg = DrawSegment::make_line(start, end);
    }

    dispatch_command(std::make_unique<AddSegmentCommand>(active, std::move(new_seg)));

    // Chain anchor: next segment starts at the end of this one
    m_pending_start = end;
    m_draft.intermediate_clicks.clear();
    const Vec2d next_raw = end;
    m_draft = DrawDraftSegment();
    m_draft.raw_mouse = next_raw;
    update_draft(next_raw);
    sync_length_input();
    return true;
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
    dc.SetBackground(wxBrush(wxColour(55, 55, 55)));
    dc.Clear();

    // Plate rectangle — use the centered uniform-scale drawing area
    const DrawTransform dt = get_draw_transform();
    const int drx = static_cast<int>(dt.draw_x0);
    const int dry = static_cast<int>(dt.draw_y0);
    const int drw = static_cast<int>(dt.draw_w);
    const int drh = static_cast<int>(dt.draw_h);

    dc.SetPen(wxPen(wxColour(105, 105, 105), 1));
    dc.SetBrush(wxBrush(wxColour(68, 68, 68)));
    dc.DrawRectangle(drx, dry, drw, drh);

    // Multi-tier reference grid — distances measured from origin (0,0).
    // Tiers (most prominent last so they paint on top):
    //   0.5mm lines: shown when scale >= 10 px/mm
    //   1mm lines:   shown when scale >= 4 px/mm (visible at all normal zoom levels)
    //   5mm lines:   always shown (prominent quarter-plate divisions)
    //   Centre axes (X=0, Y=0): always shown, most prominent
    {
        const double half_w  = m_plate_w_mm * 0.5;
        const double half_h  = m_plate_h_mm * 0.5;
        const bool   show_1mm  = (dt.scale >= 4.0);
        const bool   show_05mm = (dt.scale >= 10.0);

        // 0.5mm non-integer positions (±0.5, ±1.5, ±2.5 … ±9.5)
        if (show_05mm) {
            dc.SetPen(wxPen(wxColour(78, 78, 78), 1));
            for (int i = -20; i <= 20; ++i) {
                if (i == 0 || i % 2 == 0) continue; // skip origin and 1mm positions
                const double pos = i * 0.5;
                const wxPoint pv = plate_to_screen(Vec2d(pos, -half_h));
                if (pv.x >= drx && pv.x <= drx + drw)
                    dc.DrawLine(pv.x, dry, pv.x, dry + drh);
                const wxPoint ph = plate_to_screen(Vec2d(-half_w, pos));
                if (ph.y >= dry && ph.y <= dry + drh)
                    dc.DrawLine(drx, ph.y, drx + drw, ph.y);
            }
        }

        // 1mm positions, skipping 5mm multiples and origin (±1, ±2, ±3, ±4, ±6…)
        if (show_1mm) {
            dc.SetPen(wxPen(wxColour(84, 84, 84), 2));
            for (int i = -10; i <= 10; ++i) {
                if (i == 0 || i % 5 == 0) continue;
                const double pos = static_cast<double>(i);
                const wxPoint pv = plate_to_screen(Vec2d(pos, -half_h));
                if (pv.x >= drx && pv.x <= drx + drw)
                    dc.DrawLine(pv.x, dry, pv.x, dry + drh);
                const wxPoint ph = plate_to_screen(Vec2d(-half_w, pos));
                if (ph.y >= dry && ph.y <= dry + drh)
                    dc.DrawLine(drx, ph.y, drx + drw, ph.y);
            }
        }

        // 5mm lines (±5 from origin; ±10 = plate edge, already shown by border)
        dc.SetPen(wxPen(wxColour(90, 90, 90), 3));
        for (int sign = -1; sign <= 1; sign += 2) {
            const double pos = sign * 5.0;
            const wxPoint pv = plate_to_screen(Vec2d(pos, -half_h));
            if (pv.x >= drx && pv.x <= drx + drw)
                dc.DrawLine(pv.x, dry, pv.x, dry + drh);
            const wxPoint ph = plate_to_screen(Vec2d(-half_w, pos));
            if (ph.y >= dry && ph.y <= dry + drh)
                dc.DrawLine(drx, ph.y, drx + drw, ph.y);
        }

        // Centre axes (X=0 / Y=0): always drawn, most prominent
        dc.SetPen(wxPen(wxColour(100, 100, 100), 1));
        const wxPoint py_axis = plate_to_screen(Vec2d(0.0, -half_h));
        dc.DrawLine(py_axis.x, dry, py_axis.x, dry + drh);
        const wxPoint px_axis = plate_to_screen(Vec2d(-half_w, 0.0));
        dc.DrawLine(drx, px_axis.y, drx + drw, px_axis.y);
    }

    // Minor snap-grid dots at the selected grid spacing.
    // Only rendered when snap is on and cells are large enough to be legible (>=6 px).
    if (m_snap_to_grid && m_grid_spacing > 0.0) {
        const double dot_spacing_px = dt.scale * m_grid_spacing;
        if (dot_spacing_px >= 6.0) {
            dc.SetPen(wxPen(wxColour(86, 86, 86), 1));
            const double g         = m_grid_spacing;
            const double half_w    = m_plate_w_mm * 0.5;
            const double half_h    = m_plate_h_mm * 0.5;
            // Compute visible plate region in centred coords.
            const double x_vis_min = -half_w + m_pan_offset.x();
            const double x_vis_max = -half_w + m_pan_offset.x() + dt.visible_w;
            const double y_vis_min = -half_h + m_pan_offset.y();
            const double y_vis_max = -half_h + m_pan_offset.y() + dt.visible_h;
            const double x_start   = std::ceil(std::max(-half_w, x_vis_min) / g) * g;
            const double y_start   = std::ceil(std::max(-half_h, y_vis_min) / g) * g;
            for (double gx = x_start; gx <= std::min(half_w, x_vis_max); gx += g) {
                for (double gy = y_start; gy <= std::min(half_h, y_vis_max); gy += g) {
                    wxPoint p = plate_to_screen(Vec2d(gx, gy));
                    if (p.x >= drx && p.x <= drx + drw && p.y >= dry && p.y <= dry + drh)
                        dc.DrawPoint(p.x, p.y);
                }
            }
        }
    }

    // Ruler tick marks along all four plate edges.
    // Plate coords are centred: X and Y each run from -half to +half.
    // Integer-indexed loop (step 0.1 mm) avoids floating-point drift.
    // Tick heights (pointing inward):  0.1mm→3px  0.5mm→5px  1mm→8px  5mm→12px+label
    {
        const double px_per_mm = dt.scale;
        const bool show_01 = (px_per_mm * 0.1 >= 2.0);
        const bool show_05 = (px_per_mm * 0.5 >= 2.0);
        const double half_w = m_plate_w_mm * 0.5;
        const double half_h = m_plate_h_mm * 0.5;

        dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dc.SetTextForeground(wxColour(160, 160, 160));
        dc.SetPen(wxPen(wxColour(130, 130, 130), 1));

        // Returns tick height in pixels for the i-th 0.1mm step; 0 = skip.
        // i ranges from -n_half to +n_half; multiples-of-50 → 5mm marks.
        auto tick_h = [&](int i) -> int {
            if (i % 50 == 0)            return 12; // 5 mm (includes -50, 0, 50, etc.)
            if (i % 10 == 0)            return  8; // 1 mm
            if (i % 5  == 0 && show_05) return  5; // 0.5 mm
            if (               show_01) return  3; // 0.1 mm
            return 0;
        };

        // n_half steps cover one half of the axis (0.1mm resolution, so 100 for 10mm).
        const int n_x_half = static_cast<int>(std::round(half_w / 0.1));
        const int n_y_half = static_cast<int>(std::round(half_h / 0.1));

        // ── Bottom edge (plate Y = -half_h) — ticks point upward ─────────────
        const int bot = dry + drh;
        for (int i = -n_x_half; i <= n_x_half; ++i) {
            const int th = tick_h(i);
            if (th == 0) continue;
            const double rx = i * 0.1;
            const wxPoint p = plate_to_screen(Vec2d(rx, -half_h));
            if (p.x < drx || p.x > drx + drw) continue;
            dc.DrawLine(p.x, bot, p.x, bot - th);
            if (i % 50 == 0) { // 5mm label, inside plate just above tick
                wxString lbl = wxString::Format("%.0f", rx);
                wxSize ts = dc.GetTextExtent(lbl);
                dc.DrawText(lbl, p.x - ts.x / 2, bot - th - ts.y - 1);
            }
        }

        // ── Top edge (plate Y = +half_h) — ticks point downward ──────────────
        const int top_y = dry;
        for (int i = -n_x_half; i <= n_x_half; ++i) {
            const int th = tick_h(i);
            if (th == 0) continue;
            const double rx = i * 0.1;
            const wxPoint p = plate_to_screen(Vec2d(rx, half_h));
            if (p.x < drx || p.x > drx + drw) continue;
            dc.DrawLine(p.x, top_y, p.x, top_y + th);
        }

        // ── Left edge (plate X = -half_w) — ticks point rightward ────────────
        const int lft = drx;
        for (int i = -n_y_half; i <= n_y_half; ++i) {
            const int th = tick_h(i);
            if (th == 0) continue;
            const double ry = i * 0.1;
            const wxPoint p = plate_to_screen(Vec2d(-half_w, ry));
            if (p.y < dry || p.y > dry + drh) continue;
            dc.DrawLine(lft, p.y, lft + th, p.y);
            if (i % 50 == 0) { // 5mm label, inside plate just right of tick
                wxString lbl = wxString::Format("%.0f", ry);
                wxSize ts = dc.GetTextExtent(lbl);
                dc.DrawText(lbl, lft + th + 2, p.y - ts.y / 2);
            }
        }

        // ── Right edge (plate X = +half_w) — ticks point leftward ───────────
        const int rgt = drx + drw;
        for (int i = -n_y_half; i <= n_y_half; ++i) {
            const int th = tick_h(i);
            if (th == 0) continue;
            const double ry = i * 0.1;
            const wxPoint p = plate_to_screen(Vec2d(half_w, ry));
            if (p.y < dry || p.y > dry + drh) continue;
            dc.DrawLine(rgt, p.y, rgt - th, p.y);
        }
    }

    // No-layers message
    if (m_session.layer_count() == 0) {
        dc.SetTextForeground(wxColour(160, 160, 160));
        dc.SetFont(GetFont().Larger());
        wxString msg = "No layers - click '+ Layer' to start";
        wxSize ts = dc.GetTextExtent(msg);
        dc.DrawText(msg, (sz.x - ts.x) / 2, (sz.y - ts.y) / 2);
        draw_scale_indicator(dc, sz, dt.scale);
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
                // Selected segment  -  bright yellow (sampled for curves)
                dc.SetPen(wxPen(wxColour(255, 255, 0), 3));
                dc.SetBrush(wxBrush(wxColour(255, 255, 0)));
                if (seg.type == DrawSegmentType::Line) {
                    dc.DrawLine(p1, p2);
                } else {
                    auto pts = draw_sample_segment(seg, DRAW_MODE_SAMPLE_TOLERANCE_MM);
                    for (size_t k = 1; k < pts.size(); ++k)
                        dc.DrawLine(plate_to_screen(pts[k-1]), plate_to_screen(pts[k]));
                }
                dc.DrawCircle(p1, 4);
                dc.DrawCircle(p2, 4);
            } else if (seg.is_travel) {
                // Travel move  -  blue dashed
                wxColour c = is_active ? wxColour(80, 140, 255) : wxColour(30, 55, 100);
                dc.SetPen(wxPen(c, 1, wxPENSTYLE_SHORT_DASH));
                dc.DrawLine(p1, p2);
            } else if (m_show_filled && is_active) {
                // Filled nozzle-width strip along the sampled polyline
                double hw = m_nozzle_d * 0.5;
                dc.SetPen(wxPen(wxColour(255, 100, 0), 1));
                dc.SetBrush(wxBrush(wxColour(200, 75, 0)));
                auto pts = draw_sample_segment(seg, DRAW_MODE_SAMPLE_TOLERANCE_MM);
                for (size_t k = 1; k < pts.size(); ++k) {
                    Vec2d dv = pts[k] - pts[k-1];
                    double dlen = dv.norm();
                    if (dlen > 1e-6) {
                        Vec2d dir = dv / dlen;
                        Vec2d perp(-dir.y(), dir.x());
                        wxPoint quad[4] = {
                            plate_to_screen(pts[k-1] - perp * hw),
                            plate_to_screen(pts[k-1] + perp * hw),
                            plate_to_screen(pts[k]   + perp * hw),
                            plate_to_screen(pts[k]   - perp * hw),
                        };
                        dc.DrawPolygon(4, quad);
                    }
                }
            } else {
                // Wire extrusion line
                wxColour c = is_active ? wxColour(255, 140, 0) : wxColour(80, 50, 15);
                int lw = is_active ? 2 : 1;
                dc.SetPen(wxPen(c, lw));
                dc.SetBrush(wxBrush(c));
                if (seg.type == DrawSegmentType::Line) {
                    dc.DrawLine(p1, p2);
                    if (is_active) {
                        dc.DrawCircle(p1, 3);
                        dc.DrawCircle(p2, 3);
                    }
                } else {
                    auto pts = draw_sample_segment(seg, DRAW_MODE_SAMPLE_TOLERANCE_MM);
                    for (size_t k = 1; k < pts.size(); ++k)
                        dc.DrawLine(plate_to_screen(pts[k-1]), plate_to_screen(pts[k]));
                    if (is_active) {
                        dc.DrawCircle(p1, 3);
                        dc.DrawCircle(p2, 3);
                    }
                }
            }
        }
    }

    // Drawing mode: pending start + preview (tool-aware)
    if (m_input_mode == DrawInputMode::Drawing && m_draft.active) {
        wxPoint ps = plate_to_screen(m_draft.start);
        wxPoint pm_draw = plate_to_screen(m_draft.constrained_end);

        if (m_draw_tool == DrawTool::Line) {
            // Rubber-band preview line
            dc.SetPen(wxPen(wxColour(255, 200, 50), 1, wxPENSTYLE_SHORT_DASH));
            dc.DrawLine(ps, pm_draw);
            dc.SetPen(wxPen(wxColour(255, 200, 50), 2));
            dc.SetBrush(wxBrush(wxColour(255, 200, 50)));
            dc.DrawCircle(ps, 5);
            dc.DrawCircle(pm_draw, 4);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);

            if (m_show_measurements && m_draft.length_mm > DRAW_MODE_MIN_SEGMENT_LENGTH_MM) {
                const wxString label = wxString::FromUTF8(draw_format_length_mm(m_draft.length_mm))
                    + "\n" + wxString::FromUTF8(draw_format_angle_degrees(m_draft.angle_degrees)) + " from previous";
                draw_feedback_label(dc, label, wxPoint((ps.x + pm_draw.x) / 2 + 8, (ps.y + pm_draw.y) / 2 - 28), sz);
            }
        } else if (m_draw_tool == DrawTool::Arc) {
            dc.SetBrush(wxBrush(wxColour(255, 200, 50)));
            dc.SetPen(wxPen(wxColour(255, 200, 50), 2));
            dc.DrawCircle(ps, 5); // start anchor

            Vec2d start_pos  = m_draft.start;
            Vec2d mouse_pos  = m_draft.constrained_end;

            if (m_draft.intermediate_clicks.empty()) {
                // Only start: rubber-band line to mouse
                dc.SetPen(wxPen(wxColour(255, 200, 50), 1, wxPENSTYLE_SHORT_DASH));
                dc.DrawLine(ps, pm_draw);
                dc.DrawCircle(pm_draw, 4);
            } else {
                // Through-point committed: show live arc preview
                Vec2d through = m_draft.intermediate_clicks[0];
                wxPoint pt = plate_to_screen(through);
                dc.DrawCircle(pt, 4); // through-point dot

                DrawSegment preview_seg = DrawSegment::make_arc(start_pos, through, mouse_pos);
                auto pts = draw_sample_segment(preview_seg, DRAW_MODE_SAMPLE_TOLERANCE_MM);
                dc.SetPen(wxPen(wxColour(255, 200, 50), 1, wxPENSTYLE_SHORT_DASH));
                for (size_t k = 1; k < pts.size(); ++k)
                    dc.DrawLine(plate_to_screen(pts[k-1]), plate_to_screen(pts[k]));
                dc.DrawCircle(pm_draw, 4);
            }
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
        } else { // DrawTool::Bezier
            dc.SetBrush(wxBrush(wxColour(255, 200, 50)));
            dc.SetPen(wxPen(wxColour(255, 200, 50), 2));
            dc.DrawCircle(ps, 5); // start anchor

            Vec2d start_pos = m_draft.start;
            Vec2d mouse_pos = m_draft.constrained_end;

            if (m_draft.intermediate_clicks.empty()) {
                // Only start: rubber-band line to mouse
                dc.SetPen(wxPen(wxColour(255, 200, 50), 1, wxPENSTYLE_SHORT_DASH));
                dc.DrawLine(ps, pm_draw);
                dc.DrawCircle(pm_draw, 4);
            } else if (m_draft.intermediate_clicks.size() == 1) {
                // ctrl1 committed; mouse is the tentative ctrl2
                Vec2d ctrl1 = m_draft.intermediate_clicks[0];
                wxPoint pc1 = plate_to_screen(ctrl1);
                dc.DrawCircle(pc1, 4);
                dc.SetPen(wxPen(wxColour(200, 150, 255), 1, wxPENSTYLE_SHORT_DASH));
                dc.DrawLine(ps, pc1); // handle line start → ctrl1
                dc.DrawLine(pm_draw, pc1); // handle line mouse → ctrl1 (approximate)
                dc.SetPen(wxPen(wxColour(255, 200, 50), 1, wxPENSTYLE_SHORT_DASH));
                dc.DrawCircle(pm_draw, 4);
            } else {
                // ctrl1 and ctrl2 committed; end is mouse
                Vec2d ctrl1 = m_draft.intermediate_clicks[0];
                Vec2d ctrl2 = m_draft.intermediate_clicks[1];
                wxPoint pc1 = plate_to_screen(ctrl1);
                wxPoint pc2 = plate_to_screen(ctrl2);
                dc.DrawCircle(pc1, 4);
                dc.DrawCircle(pc2, 4);
                dc.SetPen(wxPen(wxColour(200, 150, 255), 1, wxPENSTYLE_SHORT_DASH));
                dc.DrawLine(ps, pc1);
                dc.DrawLine(pm_draw, pc2);

                DrawSegment preview_seg = DrawSegment::make_bezier(start_pos, ctrl1, ctrl2, mouse_pos);
                auto pts = draw_sample_segment(preview_seg, DRAW_MODE_SAMPLE_TOLERANCE_MM);
                dc.SetPen(wxPen(wxColour(255, 200, 50), 1, wxPENSTYLE_SHORT_DASH));
                for (size_t k = 1; k < pts.size(); ++k)
                    dc.DrawLine(plate_to_screen(pts[k-1]), plate_to_screen(pts[k]));
                dc.DrawCircle(pm_draw, 4);
            }
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
        }
    }

    // Status hint text at the bottom of the canvas
    {
        wxString hint;
        if (m_input_mode == DrawInputMode::Drawing) {
            if (m_draw_tool == DrawTool::Line) {
                hint = m_pending_start
                    ? wxString("Axis snap \u2022 Ctrl: 45\u00b0 \u2022 Alt: free \u2022 Type length + Enter \u2022 Right-click/Snip/Esc breaks")
                    : wxString("Line: Click to start \u2022 Default horizontal/vertical snap");
            } else if (m_draw_tool == DrawTool::Arc) {
                size_t n = m_draft.intermediate_clicks.size();
                if (!m_pending_start)     hint = "Arc: Click start point";
                else if (n == 0)          hint = "Arc: Click through-point (point on arc)";
                else                      hint = "Arc: Click end point to commit \u2022 Right-click/Esc cancels";
            } else {
                size_t n = m_draft.intermediate_clicks.size();
                if (!m_pending_start)     hint = "Curve: Click start point";
                else if (n == 0)          hint = "Curve: Click first control point";
                else if (n == 1)          hint = "Curve: Click second control point";
                else                      hint = "Curve: Click end point to commit \u2022 Right-click/Esc cancels";
            }
        } else {
            hint = "Click segment to select \u2022 Drag endpoints or handles \u2022 Arrow keys nudge (Shift = 1 mm) \u2022 Del removes";
        }
        dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dc.SetTextForeground(wxColour(160, 160, 160));
        dc.DrawText(hint, drx + 4, dry + drh - 20);
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

        // Control handle indicators for arc/bezier segments
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        for (const DrawSegment& seg : m_session.layers[active].segments) {
            if (seg.type == DrawSegmentType::CircularArc) {
                wxPoint pc = plate_to_screen(seg.ctrl1);
                dc.SetPen(wxPen(wxColour(200, 150, 255), 1, wxPENSTYLE_SHORT_DASH));
                dc.DrawLine(plate_to_screen(seg.start), pc);
                dc.DrawLine(plate_to_screen(seg.end), pc);
                dc.SetPen(wxPen(wxColour(200, 150, 255), 2));
                dc.DrawCircle(pc, 4);
            } else if (seg.type == DrawSegmentType::CubicBezier) {
                wxPoint pc1 = plate_to_screen(seg.ctrl1);
                wxPoint pc2 = plate_to_screen(seg.ctrl2);
                dc.SetPen(wxPen(wxColour(200, 150, 255), 1, wxPENSTYLE_SHORT_DASH));
                dc.DrawLine(plate_to_screen(seg.start), pc1);
                dc.DrawLine(plate_to_screen(seg.end), pc2);
                dc.SetPen(wxPen(wxColour(200, 150, 255), 2));
                dc.DrawCircle(pc1, 4);
                dc.DrawCircle(pc2, 4);
            }
        }
    }

    // Edit mode: drag-in-progress preview (control handle)
    if (m_is_dragging && m_dragging_ctrl.has_value()) {
        const ControlHandleRef& ch = *m_dragging_ctrl;
        if (ch.layer_index >= 0 && ch.layer_index < m_session.layer_count()
                && ch.segment_index >= 0
                && ch.segment_index < (int)m_session.layers[ch.layer_index].segments.size()) {
            const DrawSegment& dseg = m_session.layers[ch.layer_index].segments[ch.segment_index];
            // Build a preview segment with the dragged control point
            DrawSegment preview = dseg;
            if (ch.ctrl_idx == 0) preview.ctrl1 = m_drag_preview;
            else                  preview.ctrl2 = m_drag_preview;

            // Draw the updated arc/bezier preview
            auto pts = draw_sample_segment(preview, DRAW_MODE_SAMPLE_TOLERANCE_MM);
            dc.SetPen(wxPen(wxColour(255, 255, 0), 2, wxPENSTYLE_SHORT_DASH));
            for (size_t k = 1; k < pts.size(); ++k)
                dc.DrawLine(plate_to_screen(pts[k-1]), plate_to_screen(pts[k]));

            // Control handle indicator
            dc.SetPen(wxPen(wxColour(255, 255, 0), 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawCircle(plate_to_screen(m_drag_preview), 5);
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

    if (m_show_measurements && m_input_mode == DrawInputMode::Editing
            && m_sel_layer_idx >= 0 && m_sel_layer_idx < m_session.layer_count()
            && m_sel_seg_idx >= 0 && m_sel_seg_idx < (int)m_session.layers[m_sel_layer_idx].segments.size()) {
        const DrawLayer& layer = m_session.layers[m_sel_layer_idx];
        const DrawSegment& seg = layer.segments[m_sel_seg_idx];
        wxPoint p1 = plate_to_screen(seg.start);
        wxPoint p2 = plate_to_screen(seg.end);
        const wxString label = wxString::FromUTF8(draw_format_length_mm(draw_display_length_mm(seg)))
            + "\n" + wxString::FromUTF8(draw_format_angle_degrees(draw_segment_relative_angle_degrees(layer, m_sel_seg_idx)))
            + " from previous";
        draw_feedback_label(dc, label, wxPoint((p1.x + p2.x) / 2 + 8, (p1.y + p2.y) / 2 - 28), sz);
    }

    // Crosshair at mouse — clipped to the drawing area
    wxPoint pm = plate_to_screen(m_mouse_plate);
    dc.SetPen(wxPen(wxColour(140, 140, 140), 1, wxPENSTYLE_DOT));
    dc.DrawLine(pm.x, dry, pm.x, dry + drh);
    dc.DrawLine(drx, pm.y, drx + drw, pm.y);

    if (m_show_coordinates)
        draw_feedback_label(dc, wxString::FromUTF8(draw_format_coordinate_mm(m_mouse_plate)), wxPoint(pm.x + 10, pm.y + 10), sz);

    draw_scale_indicator(dc, sz, dt.scale);
}

// ---------------------------------------------------------------------------
// Canvas mouse handlers
// ---------------------------------------------------------------------------

void DrawModePanel::on_canvas_left_down(wxMouseEvent& evt)
{
    const Vec2d raw_pos = screen_to_plate(evt.GetPosition());
    Vec2d pos = snap_pos(raw_pos);

    if (m_input_mode == DrawInputMode::Editing) {
        int active = m_session.active_layer;
        if (active < 0 || active >= m_session.layer_count()) return;
        const DrawLayer& layer = m_session.layers[active];

        // Threshold: ~8 screen pixels converted to plate-space mm using the uniform scale.
        const DrawTransform hit_t = get_draw_transform();
        double thr = hit_t.scale > 0 ? 8.0 / hit_t.scale : 1.0;

        // Priority 0: control handles (for arc/bezier)
        int ctrl_seg = -1; int ctrl_ctrl_idx = 0; double best_ctrl = thr;
        for (int si = 0; si < (int)layer.segments.size(); ++si) {
            const DrawSegment& seg = layer.segments[si];
            if (seg.type == DrawSegmentType::CircularArc) {
                double d = (pos - seg.ctrl1).norm();
                if (d < best_ctrl) { best_ctrl = d; ctrl_seg = si; ctrl_ctrl_idx = 0; }
            } else if (seg.type == DrawSegmentType::CubicBezier) {
                double d1 = (pos - seg.ctrl1).norm();
                if (d1 < best_ctrl) { best_ctrl = d1; ctrl_seg = si; ctrl_ctrl_idx = 0; }
                double d2 = (pos - seg.ctrl2).norm();
                if (d2 < best_ctrl) { best_ctrl = d2; ctrl_seg = si; ctrl_ctrl_idx = 1; }
            }
        }
        if (ctrl_seg >= 0) {
            const DrawSegment& cseg = layer.segments[ctrl_seg];
            Vec2d ctrl_pos = (ctrl_ctrl_idx == 0) ? cseg.ctrl1 : cseg.ctrl2;
            m_dragging_ctrl = ControlHandleRef{active, ctrl_seg, ctrl_ctrl_idx};
            m_drag_preview  = ctrl_pos;
            m_is_dragging   = true;
            m_dragging_ep.reset();
            m_pending_start.reset();
            reset_draft(true);
            m_sel_layer_idx = active;
            m_sel_seg_idx   = ctrl_seg;
            if (!m_canvas->HasCapture()) m_canvas->CaptureMouse();
            m_canvas->Refresh(false);
            return;
        }

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
            m_dragging_ctrl.reset();
            m_pending_start.reset();
            reset_draft(true);
            m_sel_layer_idx = -1;
            m_sel_seg_idx   = -1;
            if (!m_canvas->HasCapture()) m_canvas->CaptureMouse();
            m_canvas->Refresh(false);
            return;
        }

        // Priority 2: segment bodies (use sampled polylines for curves)
        int body_seg = -1; double best_body = thr;
        for (int si = 0; si < (int)layer.segments.size(); ++si) {
            const DrawSegment& seg = layer.segments[si];
            if (seg.type == DrawSegmentType::Line) {
                double t;
                double d = DrawModeInputHandler::point_to_segment_distance(
                    pos, seg.start, seg.end, t);
                if (d < best_body) { best_body = d; body_seg = si; }
            } else {
                auto pts = draw_sample_segment(seg, DRAW_MODE_SAMPLE_TOLERANCE_MM);
                for (size_t k = 1; k < pts.size(); ++k) {
                    double t;
                    double d = DrawModeInputHandler::point_to_segment_distance(
                        pos, pts[k-1], pts[k], t);
                    if (d < best_body) { best_body = d; body_seg = si; }
                }
            }
        }
        m_sel_layer_idx = (body_seg >= 0) ? active : -1;
        m_sel_seg_idx   = body_seg;
        m_dragging_ep.reset();
        m_dragging_ctrl.reset();
        m_is_dragging = false;
        m_canvas->Refresh(false);
        return;
    }

    // Drawing mode
    int active = m_session.active_layer;
    if (active < 0 || active >= m_session.layer_count()) {
            wxMessageBox("Add a layer first - click '+ Layer'.",
                     "Draw Mode", wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (m_draw_tool == DrawTool::Line) {
        // Existing 2-click line behavior
        if (!m_pending_start) {
            m_pending_start = pos;
            update_draft(raw_pos, evt.ControlDown(), evt.AltDown());
        } else {
            update_draft(raw_pos, evt.ControlDown(), evt.AltDown());
            commit_draft_segment();
        }
    } else if (m_draw_tool == DrawTool::Arc) {
        // 3-click arc: start → through-point → end
        if (!m_pending_start) {
            // First click: set start
            m_pending_start = pos;
            m_draft.intermediate_clicks.clear();
            m_draft.tool = DrawTool::Arc;
            update_draft(raw_pos, evt.ControlDown(), evt.AltDown());
        } else if (m_draft.intermediate_clicks.empty()) {
            // Second click: set through-point
            m_draft.intermediate_clicks.push_back(snap_pos(raw_pos));
            update_draft(raw_pos, evt.ControlDown(), evt.AltDown());
        } else {
            // Third click: commit the arc
            update_draft(raw_pos, evt.ControlDown(), evt.AltDown());
            commit_draft_segment();
        }
    } else { // DrawTool::Bezier
        // 4-click bezier: start → ctrl1 → ctrl2 → end
        if (!m_pending_start) {
            m_pending_start = pos;
            m_draft.intermediate_clicks.clear();
            m_draft.tool = DrawTool::Bezier;
            update_draft(raw_pos, evt.ControlDown(), evt.AltDown());
        } else if (m_draft.intermediate_clicks.size() < 2) {
            // Second or third click: add ctrl1 or ctrl2
            m_draft.intermediate_clicks.push_back(snap_pos(raw_pos));
            update_draft(raw_pos, evt.ControlDown(), evt.AltDown());
        } else {
            // Fourth click: commit the bezier
            update_draft(raw_pos, evt.ControlDown(), evt.AltDown());
            commit_draft_segment();
        }
    }
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_canvas_right_down(wxMouseEvent&)
{
    // Cancel pending draw or active drag
    reset_draft(false);
    if (m_is_dragging) {
        if (m_canvas->HasCapture()) m_canvas->ReleaseMouse();
        m_is_dragging = false;
        m_dragging_ep.reset();
        m_dragging_ctrl.reset();
    }
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_canvas_left_up(wxMouseEvent& evt)
{
    // Check if we were dragging a control handle
    if (m_dragging_ctrl.has_value()) {
        if (m_canvas->HasCapture()) m_canvas->ReleaseMouse();
        const ControlHandleRef& ch = *m_dragging_ctrl;
        if (ch.layer_index >= 0 && ch.layer_index < m_session.layer_count()
                && ch.segment_index >= 0
                && ch.segment_index < (int)m_session.layers[ch.layer_index].segments.size()) {
            const DrawSegment& seg = m_session.layers[ch.layer_index].segments[ch.segment_index];
            Vec2d old_pos = (ch.ctrl_idx == 0) ? seg.ctrl1 : seg.ctrl2;
            Vec2d new_pos = m_drag_preview;
            if ((new_pos - old_pos).squaredNorm() > 1e-12) {
                dispatch_command(std::make_unique<MoveControlHandleCommand>(
                    ch.layer_index, ch.segment_index, ch.ctrl_idx, old_pos, new_pos));
            }
        }
        m_dragging_ctrl.reset();
        m_is_dragging = false;
        if (m_canvas) m_canvas->Refresh(false);
        return;
    }

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
    const Vec2d raw_mouse = screen_to_plate(evt.GetPosition());
    m_mouse_plate = snap_pos(raw_mouse);
    
    // Handle middle-mouse pan dragging
    if (m_pan_start.has_value()) {
        wxPoint current_pos = evt.GetPosition();
        const DrawTransform pan_t = get_draw_transform();
        int dx = current_pos.x - m_pan_start->x;
        int dy = current_pos.y - m_pan_start->y;

        if (pan_t.scale > 0) {
            m_pan_offset.x() = m_pan_start_offset.x() - dx / pan_t.scale;
            m_pan_offset.y() = m_pan_start_offset.y() + dy / pan_t.scale;
        }
        
        m_pan_offset = draw_clamp_pan_offset(m_pan_offset, m_plate_w_mm, m_plate_h_mm, m_zoom_factor);
        
        if (m_canvas) m_canvas->Refresh(false);
        return;
    }
    
    if (m_is_dragging && m_dragging_ctrl.has_value()) {
        // Control handle drag: update drag preview to snapped mouse
        m_drag_preview = m_mouse_plate;
    } else if (m_is_dragging && m_dragging_ep.has_value()) {
        const EndpointRef& ep = *m_dragging_ep;
        if (ep.layer_index >= 0 && ep.layer_index < m_session.layer_count()
                && ep.segment_index >= 0 && ep.segment_index < (int)m_session.layers[ep.layer_index].segments.size()) {
            const DrawSegment& seg = m_session.layers[ep.layer_index].segments[ep.segment_index];
            const Vec2d fixed = ep.is_start ? seg.end : seg.start;
            m_drag_preview = draw_apply_direction_snap(fixed, m_mouse_plate, current_snap_mode(evt.ControlDown(), evt.AltDown()));
        }
    } else if (m_input_mode == DrawInputMode::Drawing && m_pending_start) {
        update_draft(raw_mouse, evt.ControlDown(), evt.AltDown());
    }
    if (m_canvas) m_canvas->Refresh(false);
    evt.Skip();
}

void DrawModePanel::on_canvas_mouse_wheel(wxMouseEvent& evt)
{
    // Zoom centered on mouse position
    const int rotation = evt.GetWheelRotation();
    const double zoom_step = (rotation > 0) ? 1.2 : (1.0 / 1.2);
    
    // Get mouse position in plate coordinates before zoom
    const Vec2d mouse_plate_before = screen_to_plate(evt.GetPosition());
    
    // Update zoom with sensible bounds for 2-20 mm work.
    const double old_zoom = m_zoom_factor;
    m_zoom_factor = draw_clamp_zoom_factor(m_zoom_factor * zoom_step);
    
    // Adjust pan to keep mouse position stable
    if (std::abs(m_zoom_factor - old_zoom) > 1e-6) {
        const Vec2d mouse_plate_after = screen_to_plate(evt.GetPosition());
        m_pan_offset += (mouse_plate_before - mouse_plate_after);
        
        m_pan_offset = draw_clamp_pan_offset(m_pan_offset, m_plate_w_mm, m_plate_h_mm, m_zoom_factor);
    }
    
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_canvas_middle_down(wxMouseEvent& evt)
{
    m_pan_start = evt.GetPosition();
    m_pan_start_offset = m_pan_offset;
    if (!m_canvas->HasCapture())
        m_canvas->CaptureMouse();
    m_canvas->SetCursor(wxCursor(wxCURSOR_HAND));
}

void DrawModePanel::on_canvas_middle_up(wxMouseEvent& evt)
{
    m_pan_start.reset();
    if (m_canvas->HasCapture())
        m_canvas->ReleaseMouse();
    m_canvas->SetCursor(wxCursor(wxCURSOR_CROSS));
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
    m_dragging_ctrl.reset();
    m_is_dragging   = false;
    reset_draft(false);
    // Re-assert the current tool button state
    if (m_line_tool_btn)  m_line_tool_btn->SetValue(m_draw_tool == DrawTool::Line);
    if (m_arc_tool_btn)   m_arc_tool_btn->SetValue(m_draw_tool == DrawTool::Arc);
    if (m_curve_tool_btn) m_curve_tool_btn->SetValue(m_draw_tool == DrawTool::Bezier);
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_edit_toggle(wxCommandEvent&)
{
    m_input_mode = DrawInputMode::Editing;
    m_edit_toggle->SetValue(true);
    m_draw_toggle->SetValue(false);
    // Cancel any in-progress draw
    reset_draft(false);
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_prev_layer(wxCommandEvent&)
{
    if (m_session.active_layer > 0) {
        m_session.active_layer--;
        reset_draft(false);
        refresh();
    }
}

void DrawModePanel::on_next_layer(wxCommandEvent&)
{
    int last = m_session.layer_count() - 1;
    if (m_session.active_layer < last) {
        m_session.active_layer++;
        reset_draft(false);
        refresh();
    }
}

void DrawModePanel::on_add_layer(wxCommandEvent&)
{
    const int count  = m_session.layer_count();
    const int active = m_session.active_layer;

    // If there are already layers above the current one, navigate up by one
    // without creating anything new.  This lets the user use + Layer to
    // travel back up through layers they navigated away from with - Layer.
    if (count > 0 && active < count - 1) {
        m_session.active_layer++;
        reset_draft(false);
        refresh();
        return;
    }

    // At the top (or no layers at all): create a new layer above the current one.
    double lh = 0.2;
    if (wxGetApp().preset_bundle) {
        lh = wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_float("layer_height");
        if (lh <= 0.0) lh = 0.2;
    }
    dispatch_command(std::make_unique<AddLayerCommand>(lh));
}

void DrawModePanel::on_remove_layer(wxCommandEvent&)
{
    const int active = m_session.active_layer;
    if (active < 0 || active >= m_session.layer_count()) return;

    const bool layer_empty = m_session.layers[active].segments.empty();

    if (layer_empty) {
        // Delete the empty layer and step back (or to no-layers if it was the first)
        dispatch_command(std::make_unique<RemoveLayerCommand>(active));
        reset_draft(false);
    } else if (active > 0) {
        // Layer has content: just navigate down without deleting
        m_session.active_layer--;
        reset_draft(false);
        refresh();
    }
    // Layer 0 with segments: no-op — can't go below the first layer with content
}

void DrawModePanel::on_delete_layer(wxCommandEvent&)
{
    const int active = m_session.active_layer;
    if (active < 0 || active >= m_session.layer_count()) return;

    // Always remove the entire layer (segments and all), then step back.
    dispatch_command(std::make_unique<RemoveLayerCommand>(active));
    reset_draft(false);
}

void DrawModePanel::on_clear_layer(wxCommandEvent&)
{
    int active = m_session.active_layer;
    if (active < 0 || active >= m_session.layer_count()) return;
    dispatch_command(std::make_unique<ClearLayerCommand>(active));
    reset_draft(false);
}

// ---------------------------------------------------------------------------
// Copy / Paste layer handlers
// ---------------------------------------------------------------------------

void DrawModePanel::update_copy_paste_buttons()
{
    const int active = m_session.active_layer;

    // Paste: enabled when clipboard has segments
    const bool has_clipboard = m_layer_clipboard.has_value() && !m_layer_clipboard->empty();
    if (m_paste_layer_btn) m_paste_layer_btn->Enable(has_clipboard);

    // Copy From Prev: enabled when there is a previous layer with segments
    bool has_prev = false;
    if (active > 0 && active < m_session.layer_count())
        has_prev = !m_session.layers[active - 1].segments.empty();
    if (m_copy_prev_btn) m_copy_prev_btn->Enable(has_prev);
}

void DrawModePanel::on_copy_layer(wxCommandEvent&)
{
    const int active = m_session.active_layer;
    if (active < 0 || active >= m_session.layer_count()) return;
    const std::vector<DrawSegment>& segs = m_session.layers[active].segments;
    if (segs.empty()) {
        wxMessageBox("The current layer has no segments to copy.", "Draw Mode",
            wxOK | wxICON_INFORMATION, this);
        return;
    }
    m_layer_clipboard = segs; // copy
    update_copy_paste_buttons();
}

void DrawModePanel::on_paste_layer(wxCommandEvent&)
{
    if (!m_layer_clipboard.has_value() || m_layer_clipboard->empty()) return;
    const int active = m_session.active_layer;
    if (active < 0 || active >= m_session.layer_count()) return;
    dispatch_command(std::make_unique<PasteSegmentsCommand>(active, *m_layer_clipboard));
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_copy_from_prev(wxCommandEvent&)
{
    const int active = m_session.active_layer;
    if (active <= 0 || active >= m_session.layer_count()) return;
    const std::vector<DrawSegment>& prev_segs = m_session.layers[active - 1].segments;
    if (prev_segs.empty()) {
        wxMessageBox("The previous layer has no segments to copy.", "Draw Mode",
            wxOK | wxICON_INFORMATION, this);
        return;
    }
    dispatch_command(std::make_unique<PasteSegmentsCommand>(active, prev_segs));
    if (m_canvas) m_canvas->Refresh(false);
}

bool DrawModePanel::apply_session_to_model(bool reset_after)
{
    if (m_session.is_empty()) {
        wxMessageBox("Nothing to finalize  -  draw some segments first.", "Draw Mode",
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
        // Pass false so the mesh is NOT auto-centered  -  it is already in
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

        // Draw canvas uses centered coordinates: the draw origin (0,0) sits at
        // the centre of the 20×20 mm work area, not at the plate's bottom-left
        // corner.  The GCode generator formula is:
        //   abs_XY = plate_origin + instance_offset + segment_coord
        // so instance_offset must shift the draw-canvas centre to the work-area
        // centre, i.e. (half_w, half_h) relative to the plate origin.
        ModelInstance* inst = obj->add_instance();
        inst->set_offset(Vec3d(m_plate_x + m_plate_w_mm * 0.5,
                               m_plate_y + m_plate_h_mm * 0.5,
                               0.0));

        // Track the new object so a subsequent simulate/finalize updates it in-place.
        if (!reset_after)
            m_editing_obj_idx = static_cast<int>(model.objects.size()) - 1;
    }

    m_plater->update();
    wxGetApp().obj_list()->update_after_undo_redo();

    if (reset_after) {
        // Finalize path: clear the panel so the user starts a fresh session.
        m_editing_obj_idx = -1;
        m_session         = DrawSession();
        m_undo_stack.clear();
        m_redo_stack.clear();
        m_pending_start.reset();
        update_banner();
        update_layer_label();
    }

    return true;
}

void DrawModePanel::on_simulate(wxCommandEvent&)
{
    // Write session to model without resetting the panel  -  user can keep editing.
    if (!apply_session_to_model(/*reset_after=*/false)) return;

    // Trigger the draw-path G-code path explicitly. This prevents the
    // background process from reusing a stale finished normal slice and from
    // falling through to normal slicing/Arachne.
    m_plater->reslice_draw_path_simulation();

    // Switch to Preview tab so the G-code viewer shows the result.
    wxGetApp().mainframe->select_tab((size_t)MainFrame::tpPreview);
}

void DrawModePanel::on_finalize(wxCommandEvent&)
{
    if (!apply_session_to_model(/*reset_after=*/true)) return;

    // Switch back to 3D Editor tab.
    wxGetApp().mainframe->select_tab((size_t)MainFrame::tp3DEditor);
}

void DrawModePanel::on_char_hook(wxKeyEvent& evt)
{
    if (!IsShownOnScreen()) {
        evt.Skip();
        return;
    }

    // Escape in Drawing mode: snip the continuous chain and cancel any typed length.
    if (evt.GetKeyCode() == WXK_ESCAPE && m_input_mode == DrawInputMode::Drawing) {
        reset_draft(false);
        if (m_canvas) m_canvas->Refresh(false);
        return;
    }

    if (evt.ControlDown()) {
        if (evt.GetKeyCode() == 'Z' && !m_undo_stack.empty()) {
            auto cmd = std::move(m_undo_stack.back());
            m_undo_stack.pop_back();
            cmd->undo(m_session);
            m_redo_stack.push_back(std::move(cmd));
            sync_chain_anchor();
            refresh();
            return; // swallowed
        }
        if (evt.GetKeyCode() == 'Y' && !m_redo_stack.empty()) {
            auto cmd = std::move(m_redo_stack.back());
            m_redo_stack.pop_back();
            cmd->execute(m_session);
            m_undo_stack.push_back(std::move(cmd));
            sync_chain_anchor();
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

        const Vec2d delta = draw_nudge_delta_from_key(kc, evt.ShiftDown());
        if (delta.squaredNorm() > 0.0) {
            dispatch_command(std::make_unique<TranslateSegmentCommand>(m_sel_layer_idx, m_sel_seg_idx, delta));
            return;
        }
    }

    if (m_input_mode == DrawInputMode::Drawing && m_pending_start && !evt.ControlDown() && !evt.AltDown()) {
        const int kc = evt.GetKeyCode();
        if ((kc >= '0' && kc <= '9') || kc == '.' || kc == ',' || (kc >= WXK_NUMPAD0 && kc <= WXK_NUMPAD9) || kc == WXK_DECIMAL) {
            wxString text = m_draft.typed_length_text;
            if (kc >= WXK_NUMPAD0 && kc <= WXK_NUMPAD9)
                text += wxChar('0' + (kc - WXK_NUMPAD0));
            else if (kc == WXK_DECIMAL) {
                if (text.Contains('.') || text.Contains(','))
                    return;
                text += '.';
            } else if (kc == '.' || kc == ',') {
                if (text.Contains('.') || text.Contains(','))
                    return;
                text += wxChar(kc);
            } else {
                text += wxChar(kc);
            }
            set_typed_length_text(text);
            if (m_canvas) m_canvas->Refresh(false);
            return;
        }
        if (kc == WXK_BACK) {
            wxString text = m_draft.typed_length_text;
            if (!text.empty())
                text.RemoveLast();
            set_typed_length_text(text);
            if (m_canvas) m_canvas->Refresh(false);
            return;
        }
        if (kc == WXK_RETURN || kc == WXK_NUMPAD_ENTER) {
            if (m_draft.has_typed_length && draw_parse_length_mm(m_draft.typed_length_text.ToStdString())) {
                commit_draft_segment();
                if (m_canvas) m_canvas->Refresh(false);
                return;
            }
        }
    }
    evt.Skip();
}

void DrawModePanel::on_key_up(wxKeyEvent& evt)
{
    evt.Skip();
    if (!IsShownOnScreen()) return;

    // When a modifier key (Ctrl/Alt) is released, re-evaluate the snap mode so
    // the rubber-band line immediately returns to Cardinal instead of staying
    // locked in Diagonal45 until the mouse moves again.
    const int kc = evt.GetKeyCode();
    if ((kc == WXK_CONTROL || kc == WXK_ALT) &&
        m_input_mode == DrawInputMode::Drawing && m_pending_start && m_draft.active) {
        update_draft(m_draft.raw_mouse, evt.ControlDown(), evt.AltDown());
        if (m_canvas) m_canvas->Refresh(false);
    }
}

void DrawModePanel::on_fill_toggle(wxCommandEvent&)
{
    m_show_filled = m_fill_toggle->GetValue();
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_snap_toggle(wxCommandEvent&)
{
    m_snap_to_grid = m_snap_toggle->GetValue();
    // Immediately snap the current mouse position so crosshair updates
    m_mouse_plate = snap_pos(m_mouse_plate);
    const bool diagonal_snap = m_draft.snap_mode == DrawDirectionSnapMode::Diagonal45;
    const bool free_snap = m_draft.snap_mode == DrawDirectionSnapMode::Free;
    update_draft(m_draft.raw_mouse, diagonal_snap, free_snap);
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_grid_res_change(wxCommandEvent&)
{
    static const double k_spacings[] = {
        0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0
    };
    const int sel = m_grid_res_choice->GetSelection();
    if (sel >= 0 && sel < static_cast<int>(sizeof(k_spacings) / sizeof(k_spacings[0])))
        m_grid_spacing = k_spacings[sel];
    m_mouse_plate = snap_pos(m_mouse_plate);
    const bool diagonal_snap = m_draft.snap_mode == DrawDirectionSnapMode::Diagonal45;
    const bool free_snap     = m_draft.snap_mode == DrawDirectionSnapMode::Free;
    update_draft(m_draft.raw_mouse, diagonal_snap, free_snap);
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_measure_toggle(wxCommandEvent&)
{
    m_show_measurements = m_measure_toggle->GetValue();
    if (m_plater)
        m_plater->model().draw_mode_display_preferences.show_measurements = m_show_measurements;
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_coord_toggle(wxCommandEvent&)
{
    m_show_coordinates = m_coord_toggle->GetValue();
    if (m_plater)
        m_plater->model().draw_mode_display_preferences.show_coordinates = m_show_coordinates;
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_length_text(wxCommandEvent&)
{
    if (m_updating_length_input || !m_length_input || !m_pending_start)
        return;

    m_draft.has_typed_length = !m_length_input->GetValue().empty();
    m_draft.typed_length_text = m_length_input->GetValue();
    const bool diagonal_snap = m_draft.snap_mode == DrawDirectionSnapMode::Diagonal45;
    const bool free_snap = m_draft.snap_mode == DrawDirectionSnapMode::Free;
    update_draft(m_draft.raw_mouse, diagonal_snap, free_snap);
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_length_enter(wxCommandEvent&)
{
    if (m_draft.has_typed_length && draw_parse_length_mm(m_draft.typed_length_text.ToStdString())) {
        commit_draft_segment();
        if (m_canvas) m_canvas->Refresh(false);
    }
}

void DrawModePanel::on_snip(wxCommandEvent&)
{
    // Break the continuous chain. Next left-click starts a new extrusion path.
    // The G-code generator will insert a retract + travel between the two paths.
    m_draft.intermediate_clicks.clear();
    reset_draft(false);
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_line_tool(wxCommandEvent&)
{
    m_draw_tool = DrawTool::Line;
    m_line_tool_btn->SetValue(true);
    m_arc_tool_btn->SetValue(false);
    m_curve_tool_btn->SetValue(false);
    reset_draft(false);
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_arc_tool(wxCommandEvent&)
{
    m_draw_tool = DrawTool::Arc;
    m_line_tool_btn->SetValue(false);
    m_arc_tool_btn->SetValue(true);
    m_curve_tool_btn->SetValue(false);
    reset_draft(false);
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::on_curve_tool(wxCommandEvent&)
{
    m_draw_tool = DrawTool::Bezier;
    m_line_tool_btn->SetValue(false);
    m_arc_tool_btn->SetValue(false);
    m_curve_tool_btn->SetValue(true);
    reset_draft(false);
    if (m_canvas) m_canvas->Refresh(false);
}

void DrawModePanel::sync_chain_anchor()
{
    if (m_input_mode != DrawInputMode::Drawing) return;
    const int active = m_session.active_layer;
    if (active < 0 || active >= m_session.layer_count()
            || m_session.layers[active].segments.empty()) {
        reset_draft(false);
        return;
    }
    // Continue chain from the last segment endpoint on the active layer.
    m_pending_start = m_session.layers[active].segments.back().end;
    update_draft(m_pending_start.value());
}

} // namespace GUI
} // namespace Slic3r
