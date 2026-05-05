#pragma once

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/tglbtn.h>
#include <wx/dcclient.h>

#include "libslic3r/DrawSession.hpp"
#include "DrawModeCommands.hpp"
#include "DrawModeInputHandler.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace Slic3r {

class DynamicPrintConfig;
class ModelObject;

namespace GUI {

class Plater;
struct PartPlate;

// Draw Mode panel — lets users draw toolpaths directly on a 2D plate view
// without needing a 3D model. Shown as a separate top-level tab.
class DrawModePanel : public wxPanel
{
public:
    DrawModePanel(wxWindow* parent, Plater* plater);
    ~DrawModePanel();

    // Called when user switches to the Draw tab.
    // Snapshots the active plate and resets state for a new drawing session.
    void activate(PartPlate* plate);

    // Called when the user wants to edit an already-finalized draw object.
    // Loads the existing DrawSession into the panel for modification.
    void load_for_edit(ModelObject* obj, int obj_idx);

    // Refresh UI labels.
    void refresh();

    // Undo/redo stacks — exposed so keyboard handler can check emptiness.
    bool can_undo() const { return !m_undo_stack.empty(); }
    bool can_redo() const { return !m_redo_stack.empty(); }

private:
    // UI widgets
    wxStaticText*      m_banner_text    { nullptr };
    wxStaticText*      m_layer_label    { nullptr };
    wxPanel*           m_canvas         { nullptr }; // 2-D drawing surface
    wxToggleButton*    m_draw_toggle    { nullptr };
    wxToggleButton*    m_edit_toggle    { nullptr };
    wxButton*          m_prev_layer_btn { nullptr };
    wxButton*          m_next_layer_btn { nullptr };
    wxButton*          m_add_layer_btn  { nullptr };
    wxButton*          m_clear_btn      { nullptr };
    wxButton*          m_simulate_btn   { nullptr };
    wxButton*          m_finalize_btn   { nullptr };

    // State
    Plater*      m_plater        { nullptr };
    DrawSession  m_session;
    DrawInputMode m_input_mode   { DrawInputMode::Drawing };
    int          m_editing_obj_idx{ -1 }; // >=0 when editing existing object

    // Plate origin snapshot (plate-relative, set during activate())
    double       m_plate_x { 0.0 };
    double       m_plate_y { 0.0 };

    // 2-D canvas state
    std::optional<Vec2d> m_pending_start;       // first click waiting for second
    Vec2d                m_mouse_plate { 0.0, 0.0 }; // current mouse in plate-mm
    double               m_plate_w_mm  { 256.0 };
    double               m_plate_h_mm  { 256.0 };

    // Command stacks for undo/redo
    std::vector<std::unique_ptr<DrawCommand>> m_undo_stack;
    std::vector<std::unique_ptr<DrawCommand>> m_redo_stack;

    // Helpers
    void update_banner();
    void update_layer_label();
    void dispatch_command(std::unique_ptr<DrawCommand> cmd);

    // Coordinate conversion between canvas pixels and plate-space mm
    Vec2d   screen_to_plate(wxPoint pt) const;
    wxPoint plate_to_screen(Vec2d   pt) const;

    // Canvas event handlers
    void on_canvas_paint(wxPaintEvent&);
    void on_canvas_erase_bg(wxEraseEvent&);
    void on_canvas_left_down(wxMouseEvent&);
    void on_canvas_right_down(wxMouseEvent&);
    void on_canvas_motion(wxMouseEvent&);

    // Button handlers
    void on_draw_toggle(wxCommandEvent& evt);
    void on_edit_toggle(wxCommandEvent& evt);
    void on_prev_layer(wxCommandEvent& evt);
    void on_next_layer(wxCommandEvent& evt);
    void on_add_layer(wxCommandEvent& evt);
    void on_clear_layer(wxCommandEvent& evt);
    void on_simulate(wxCommandEvent& evt);
    void on_finalize(wxCommandEvent& evt);

    // Keyboard hook (Ctrl+Z / Ctrl+Y)
    void on_char_hook(wxKeyEvent& evt);

    // Shared finalize logic: create/update the ModelObject on the plate.
    // Returns true on success. Does NOT switch tabs.
    bool apply_session_to_model();
};

} // namespace GUI
} // namespace Slic3r
