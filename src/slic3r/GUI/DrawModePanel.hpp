#pragma once

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/tglbtn.h>
#include <wx/textctrl.h>
#include <wx/dcclient.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>

#include "libslic3r/DrawSession.hpp"
#include "libslic3r/DrawModeFeedback.hpp"
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

// Drawing tool selection.
enum class DrawTool { Line, Arc, Bezier };

// Draw Mode panel — lets users draw toolpaths directly on a 2D plate view
// without needing a 3D model. Shown as a separate top-level tab.
class DrawModePanel : public wxPanel
{
public:
    DrawModePanel(wxWindow* parent, Plater* plater);
    ~DrawModePanel();

    // Called when user switches to the Draw tab for the first time (fresh session).
    // Snapshots the active plate and resets ALL state.
    void activate(PartPlate* plate);

    // Called when user returns to the Draw tab after Simulate.
    // Updates plate/nozzle metadata without clearing the existing session.
    void reactivate(PartPlate* plate);

    // Returns true when the panel holds uncommitted or committed drawing data.
    bool has_session() const { return !m_session.is_empty(); }

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
    wxToggleButton*    m_fill_toggle    { nullptr }; // filled-quad width preview
    wxToggleButton*    m_snap_toggle    { nullptr }; // snap-to-grid (on by default)
    wxChoice*          m_grid_res_choice{ nullptr }; // snap grid resolution selector
    wxChoice*          m_arc_res_choice { nullptr }; // arc/bezier segment resolution
    wxCheckBox*        m_native_arc_chk { nullptr }; // emit G2/G3 for circular arcs
    wxSpinCtrl*        m_first_layer_flow_spin { nullptr }; // layer-0 flow % (elephant's foot)
    wxCheckBox*        m_wipe_check     { nullptr }; // wipe backward while retracting (anti-blob)
    wxSpinCtrlDouble*  m_wipe_dist_spin { nullptr }; // wipe distance (mm)
    wxSpinCtrlDouble*  m_coast_spin     { nullptr }; // coast distance (mm); PA-incompatible
    wxToggleButton*    m_measure_toggle { nullptr };
    wxToggleButton*    m_coord_toggle   { nullptr };
    wxTextCtrl*        m_length_input   { nullptr };
    wxButton*          m_snip_btn       { nullptr }; // break continuous chain
    wxButton*          m_prev_layer_btn { nullptr };
    wxButton*          m_next_layer_btn { nullptr };
    wxButton*          m_remove_layer_btn { nullptr }; // go down / delete if empty
    wxButton*          m_add_layer_btn  { nullptr };
    wxButton*          m_insert_below_btn { nullptr }; // insert empty layer at current position
    wxButton*          m_delete_layer_btn { nullptr }; // always delete current layer
    wxButton*          m_copy_layer_btn   { nullptr }; // copy current layer to clipboard
    wxButton*          m_paste_layer_btn  { nullptr }; // paste clipboard into current layer
    wxButton*          m_copy_prev_btn    { nullptr }; // copy-from-previous-layer shortcut
    wxButton*          m_mirror_stack_btn { nullptr }; // mirror the full layer stack
    wxButton*          m_clear_btn      { nullptr };
    wxButton*          m_simulate_btn   { nullptr };
    wxButton*          m_finalize_btn   { nullptr };

    // Tool selection for drawing mode
    wxToggleButton*    m_line_tool_btn  { nullptr };
    wxToggleButton*    m_arc_tool_btn   { nullptr };
    wxToggleButton*    m_curve_tool_btn { nullptr };
    wxToggleButton*    m_splice_btn     { nullptr };

    // State
    Plater*      m_plater        { nullptr };
    DrawSession  m_session;
    DrawInputMode m_input_mode   { DrawInputMode::Drawing };
    int          m_editing_obj_idx{ -1 }; // >=0 when editing existing object
    DrawTool     m_draw_tool     { DrawTool::Line }; // active drawing tool
    bool         m_splice_active { false };
    DrawInputMode m_saved_input_mode { DrawInputMode::Drawing };
    DrawTool     m_saved_draw_tool { DrawTool::Line };

    // Layer clipboard: holds segments copied via "Copy Layer".
    // Cleared on activate(). Persists across layer navigation and undo/redo.
    std::optional<std::vector<DrawSegment>> m_layer_clipboard;

    // Plate origin snapshot (plate-relative, set during activate())
    double       m_plate_x { 0.0 };
    double       m_plate_y { 0.0 };

    // 2-D canvas state
    std::optional<Vec2d> m_pending_start;       // first click waiting for second
    Vec2d                m_mouse_plate { 0.0, 0.0 }; // current mouse in plate-mm
    double               m_plate_w_mm  { DRAW_MODE_WORK_AREA_MM };
    double               m_plate_h_mm  { DRAW_MODE_WORK_AREA_MM };

    // Zoom and pan state
    double               m_zoom_factor { DRAW_MODE_DEFAULT_ZOOM_FACTOR }; // 1.0 = fit plate, >1.0 = zoomed in
    Vec2d                m_pan_offset  { 0.0, 0.0 }; // pan offset in plate-mm
    std::optional<wxPoint> m_pan_start;              // middle-drag start position
    Vec2d                m_pan_start_offset { 0.0, 0.0 };

    // Cached nozzle diameter (read from printer preset in activate())
    double               m_nozzle_d    { 0.4 };
    // Filled-quad width preview toggle
    bool                 m_show_filled { false };
    // Snap-to-grid toggle (on by default); resolution controlled by m_grid_spacing
    bool                 m_snap_to_grid { true };
    // Selectable snap-grid resolution in mm (0.1–1.0, default 0.4)
    double               m_grid_spacing { 0.4 };
    bool                 m_show_measurements { true };
    bool                 m_show_coordinates  { true };

    struct DrawDraftSegment {
        bool                  active = false;
        DrawTool              tool   { DrawTool::Line };
        Vec2d                 start { 0.0, 0.0 };
        std::vector<Vec2d>    intermediate_clicks; // arc: [through_point]; bezier: [ctrl1, ctrl2]
        Vec2d                 raw_mouse { 0.0, 0.0 };
        Vec2d                 grid_snapped_mouse { 0.0, 0.0 };
        Vec2d                 constrained_end { 0.0, 0.0 };
        double                length_mm = 0.0;
        double                angle_degrees = 0.0;
        bool                  has_typed_length = false;
        wxString              typed_length_text;
        DrawDirectionSnapMode snap_mode { DrawDirectionSnapMode::Cardinal };
    };
    DrawDraftSegment    m_draft;
    bool                m_updating_length_input { false };

    // Edit mode selection / drag state
    int                  m_sel_layer_idx { -1 };
    int                  m_sel_seg_idx   { -1 };
    std::optional<EndpointRef>  m_dragging_ep;
    // Connected endpoints that move together with m_dragging_ep (populated on drag start).
    std::vector<ConnectedEndpointRef> m_dragging_connected_eps;

    // Reference to a control handle being dragged in edit mode
    struct ControlHandleRef {
        int layer_index;
        int segment_index;
        int ctrl_idx; // 0 = ctrl1, 1 = ctrl2
    };
    std::optional<ControlHandleRef> m_dragging_ctrl;

    Vec2d                m_drag_preview  { 0.0, 0.0 };
    bool                 m_is_dragging   { false };

    // Command stacks for undo/redo
    std::vector<std::unique_ptr<DrawCommand>> m_undo_stack;
    std::vector<std::unique_ptr<DrawCommand>> m_redo_stack;

    // Uniform coordinate transform: maintains 1:1 aspect ratio (square grid cells).
    // Computed from the current canvas size, plate dimensions, zoom and pan.
    struct DrawTransform {
        double scale    { 0.0 }; // pixels per mm (same for X and Y)
        double draw_x0  { 0.0 }; // canvas X of the plate's left edge
        double draw_y0  { 0.0 }; // canvas Y of the plate's top edge
        double draw_w   { 0.0 }; // drawing area width in pixels
        double draw_h   { 0.0 }; // drawing area height in pixels
        double visible_w{ 0.0 }; // visible plate extent in X (mm)
        double visible_h{ 0.0 }; // visible plate extent in Y (mm)
    };
    DrawTransform get_draw_transform() const;

    // Helpers
    bool restore_existing_draw_object(PartPlate* plate);
    void update_banner();
    void update_layer_label();
    void dispatch_command(std::unique_ptr<DrawCommand> cmd);

    // Coordinate conversion between canvas pixels and plate-space mm
    Vec2d   screen_to_plate(wxPoint pt) const;
    wxPoint plate_to_screen(Vec2d   pt) const;
    // Round pt to the nearest nozzle-diameter grid point (no-op when snap off).
    Vec2d   snap_pos(Vec2d pt) const;
    DrawDirectionSnapMode current_snap_mode(bool ctrl_down = false, bool alt_down = false) const;
    void    update_draft(Vec2d raw_mouse, bool ctrl_down = false, bool alt_down = false);
    void    reset_draft(bool keep_chain_anchor);
    bool    commit_draft_segment();
    void    sync_length_input();
    void    set_typed_length_text(const wxString& text);
    double  draft_relative_angle_degrees(const Vec2d& start, const Vec2d& end) const;

    // Canvas event handlers
    void on_canvas_paint(wxPaintEvent&);
    void on_canvas_erase_bg(wxEraseEvent&);
    void on_canvas_left_down(wxMouseEvent&);
    void on_canvas_right_down(wxMouseEvent&);
    void on_canvas_motion(wxMouseEvent&);
    void on_canvas_left_up(wxMouseEvent&);
    void on_canvas_mouse_wheel(wxMouseEvent&);
    void on_canvas_middle_down(wxMouseEvent&);
    void on_canvas_middle_up(wxMouseEvent&);

    // Button handlers
    void on_draw_toggle(wxCommandEvent& evt);
    void on_edit_toggle(wxCommandEvent& evt);
    void on_line_tool(wxCommandEvent& evt);
    void on_arc_tool(wxCommandEvent& evt);
    void on_curve_tool(wxCommandEvent& evt);
    void on_splice_toggle(wxCommandEvent& evt);
    void exit_splice_mode();
    void on_fill_toggle(wxCommandEvent& evt);
    void on_snap_toggle(wxCommandEvent& evt);
    void on_grid_res_change(wxCommandEvent& evt);
    void on_arc_res_change(wxCommandEvent& evt);
    void on_native_arc_toggle(wxCommandEvent& evt);
    void on_first_layer_flow_change(wxCommandEvent& evt);
    void on_wipe_toggle(wxCommandEvent& evt);
    void on_wipe_dist_change(wxCommandEvent& evt);
    void on_coast_change(wxCommandEvent& evt);
    void on_measure_toggle(wxCommandEvent& evt);
    void on_coord_toggle(wxCommandEvent& evt);
    void on_length_text(wxCommandEvent& evt);
    void on_length_enter(wxCommandEvent& evt);
    void on_snip(wxCommandEvent& evt);
    // Sync the chain anchor (m_pending_start) to the last segment endpoint
    // after undo/redo, so the rubber-band previews the correct continuation.
    void sync_chain_anchor();
    void update_copy_paste_buttons(); // enable/disable copy/paste buttons based on current state
    void on_prev_layer(wxCommandEvent& evt);
    void on_next_layer(wxCommandEvent& evt);
    void on_add_layer(wxCommandEvent& evt);
    void on_insert_below(wxCommandEvent& evt);
    void on_remove_layer(wxCommandEvent& evt);
    void on_delete_layer(wxCommandEvent& evt);
    void on_clear_layer(wxCommandEvent& evt);
    void on_copy_layer(wxCommandEvent& evt);
    void on_paste_layer(wxCommandEvent& evt);
    void on_copy_from_prev(wxCommandEvent& evt);
    void on_mirror_stack(wxCommandEvent& evt);
    void on_simulate(wxCommandEvent& evt);
    void on_finalize(wxCommandEvent& evt);

    // Keyboard hook (Ctrl+Z / Ctrl+Y)
    void on_char_hook(wxKeyEvent& evt);
    // Key-up hook: re-evaluate snap mode when modifier keys are released mid-draft
    void on_key_up(wxKeyEvent& evt);

    // Shared finalize logic: create/update the ModelObject on the plate.
    // When reset_after is true (Finalize), the panel state is cleared afterwards.
    // When false (Simulate), the session stays live so the user can continue editing.
    // Returns true on success. Does NOT switch tabs.
    bool apply_session_to_model(bool reset_after = true);
};

} // namespace GUI
} // namespace Slic3r
