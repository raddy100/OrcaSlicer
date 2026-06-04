# Draw Mode GUI Redesign — Architecture, Design & Implementation

**Audience:** Coding agent (`quality-code-developer` / `general-purpose`)
**Scope:** `src/slic3r/GUI/DrawModePanel.{hpp,cpp}` + one new widget pair. No changes to slicing, G-code, or `DrawSession` data model.
**Status:** Ready to implement.

---

## 1. Problem statement

The current Draw Mode toolbar (see `CurrentGUI.png`) is a flat, two-row strip of ~35 controls
laid out with two `wxBoxSizer(wxHORIZONTAL)` rows (`top_sizer`, `nav_sizer` in
`DrawModePanel.cpp:206-249`). Problems:

1. **No grouping** — unrelated controls (drawing tools, flow %, wipe, view toggles, layer ops)
   sit shoulder-to-shoulder with no visual separation.
2. **Overflow / crowding** — both rows are fixed `wxHORIZONTAL` sizers. When the window is
   sized to half-screen the controls are clipped/crowded out instead of wrapping or collapsing.
3. **Layer navigation is button-only** — `< Prev` / `Next >` / `Layer: N` (`DrawModePanel.cpp:238-241`)
   force one-layer-at-a-time stepping. There is no way to scrub or jump to an arbitrary layer.

This document specifies a redesign that (a) groups controls by function, (b) makes the toolbar
responsive so nothing is clipped at half-screen, and (c) adds an interactive vertical **layer
slider** supporting both drag-to-scrub and click-to-teleport.

---

## 2. Current architecture (as-is)

`DrawModePanel` is a `wxPanel` (`DrawModePanel.hpp:38`). Construction (`DrawModePanel.cpp:206-256`):

```
main_sizer (VERTICAL)
├── top_sizer (HORIZONTAL)   // banner + mode + tools + options + quality + view + actions
├── nav_sizer (HORIZONTAL)   // prev/next + layer label + add/insert/delete/copy/paste/mirror
└── m_canvas (1, wxEXPAND)   // 2-D drawing surface (wxPanel)
```

Key existing members (`DrawModePanel.hpp`):

- Mode toggles: `m_draw_toggle`, `m_edit_toggle` (72-73)
- Tools: `m_line_tool_btn`, `m_arc_tool_btn`, `m_curve_tool_btn`, `m_splice_btn` (101-104)
- Geometry opts: `m_fill_toggle`, `m_snap_toggle`, `m_grid_res_choice`, `m_arc_res_choice`,
  `m_native_arc_chk`, `m_length_input`, `m_snip_btn` (73-85)
- Quality: **initial-layers editor** (`m_initial_layer_count_spin` + per-layer
  `m_initial_flow_spin[]` / `m_initial_height_spin[]`, supersedes the former
  `m_first_layer_flow_spin`), `m_wipe_check`, `m_wipe_dist_spin`, `m_coast_spin`
- View: `m_measure_toggle`, `m_coord_toggle` (82-83)
- Layer nav/ops: `m_prev_layer_btn`, `m_next_layer_btn`, `m_remove_layer_btn`, `m_add_layer_btn`,
  `m_insert_below_btn`, `m_delete_layer_btn`, `m_copy_layer_btn`, `m_paste_layer_btn`,
  `m_copy_prev_btn`, `m_mirror_stack_btn`, `m_layer_label` (69, 86-95)
- Session: `m_clear_btn`, `m_simulate_btn`, `m_finalize_btn` (96-98)

Layer state lives in `DrawSession` (`src/libslic3r/DrawSession.hpp`):

- `std::vector<DrawLayer> layers` + `int active_layer` (88-89)
- `int layer_count() const` (155)
- `DrawLayer { int layer_index; double z_start; double z_end; std::vector<DrawSegment> segments; double layer_height(); }` (74-81)
- Navigation today simply does `m_session.active_layer = N; update_layer_label(); m_canvas->Refresh();`
  (see `on_prev_layer` / `on_next_layer` handlers, `DrawModePanel.cpp` button section).

`update_layer_label()` (`DrawModePanel.cpp:542-...`) sets the label text and enables/disables the
nav/ops buttons based on `layer_count()`, `active_layer`, and whether the active layer is empty.

**Constraint:** Do NOT change the `DrawSession` data model, serialization, or any slicing/G-code
code. This is a pure GUI refactor. All existing handlers, undo/redo, splice, and keyboard hooks
must keep working unchanged.

---

## 3. Target design (to-be)

### 3.1 Layout overview

```
main_sizer (VERTICAL)
├── m_banner_text                       // unchanged status line, full width
├── m_toolbar  (DrawToolbar = wxWrapSizer-based responsive grouped bar)
│      [Mode] | [Tools] | [Geometry] | [Quality] | [View] | [Layers] | [Session]
└── content_sizer (HORIZONTAL, flex=1)
       ├── m_canvas       (1, wxEXPAND)   // unchanged drawing surface
       └── m_layer_slider (DrawLayerSlider, fixed width ~64px, wxEXPAND vertically)
```

Two structural changes:

1. **Grouped, responsive toolbar** replacing `top_sizer` + `nav_sizer`.
2. **Vertical layer slider** on the **right** of the canvas (new `content_sizer`).
   Right-side placement matches OrcaSlicer's existing Preview layer slider (`IMSlider`),
   so users keep the same muscle memory. **(Decided — not an open question.)**

The banner stays as its own line above the toolbar so it never competes for horizontal space.

### 3.2 Responsive grouped toolbar

Replace the two flat `wxBoxSizer(wxHORIZONTAL)` rows with a single **`wxWrapSizer(wxHORIZONTAL)`**.
`wxWrapSizer` reflows its children onto additional rows when the panel is too narrow — this directly
fixes the half-screen crowding/clipping problem (no controls get cut off; they wrap).

Each functional group is a child sizer added to the wrap sizer as an indivisible unit. Use a small
helper that builds a labeled, bordered group so groups are visually distinct:

```cpp
// New private helper in DrawModePanel.
// Returns a sizer containing a thin vertical separator + the group's controls,
// so groups read as discrete clusters even when they wrap.
wxSizer* make_tool_group(const wxString& label, std::vector<wxWindow*> items);
```

Implementation guidance for `make_tool_group`:

- **Use `wxStaticBoxSizer(wxHORIZONTAL, this, label)`** (LOCKED) — it draws a titled border so each
  group reads as a discrete cluster. Add each `item` with `wxALIGN_CENTER_VERTICAL | wxALL, 3`.
- Add the whole group to the wrap sizer with `wxALL, 4` so groups keep breathing room as they wrap.
- A group added with `wxStaticBoxSizer` is treated by `wxWrapSizer` as one wrappable element — it
  will never be split mid-group.

**Groups (in order):**

| Group      | Controls (existing members, reused as-is)                                                        |
|------------|--------------------------------------------------------------------------------------------------|
| `Mode`     | `m_draw_toggle`, `m_edit_toggle`                                                                  |
| `Tools`    | `m_line_tool_btn`, `m_arc_tool_btn`, `m_curve_tool_btn`, `m_splice_btn`                           |
| `Geometry` | `m_snap_toggle`, `Grid:`+`m_grid_res_choice`, `Arc:`+`m_arc_res_choice`, `m_native_arc_chk`, `m_fill_toggle`, `Length:`+`m_length_input`, `m_snip_btn` |
| `Quality`  | **Initial-layers editor** (`Init:`+`m_initial_layer_count_spin`, then N shown rows of `L{i} flow%:`+`m_initial_flow_spin[i]` / `h(mm):`+`m_initial_height_spin[i]`), `m_wipe_check`, `Dist:`+`m_wipe_dist_spin`, `Coast:`+`m_coast_spin` |
| `View`     | `m_measure_toggle`, `m_coord_toggle`                                                              |
| `Layers`   | `m_add_layer_btn`, `m_insert_below_btn`, `m_delete_layer_btn`, `m_remove_layer_btn`, `m_copy_layer_btn`, `m_paste_layer_btn`, `m_copy_prev_btn`, `m_mirror_stack_btn`, `m_clear_btn` |
| `Session`  | `m_simulate_btn`, `m_finalize_btn`                                                                |

Notes:
- **Locked control labels (use exactly these — do not improvise).** Inline `wxStaticText` prefixes
  are shortened since the group title gives context; full text + units live in the control's tooltip
  via `SetToolTip`:

  | Group title | Control | Inline label (exact) | Tooltip (exact) |
  |-------------|---------|----------------------|-----------------|
  | `Mode`      | `m_draw_toggle` | (button text "Draw") | "Switch to drawing mode" |
  | `Mode`      | `m_edit_toggle` | (button text "Edit") | "Switch to edit mode" |
  | `Tools`     | `m_line_tool_btn` / `m_arc_tool_btn` / `m_curve_tool_btn` / `m_splice_btn` | (existing button text) | keep existing |
  | `Geometry`  | `m_snap_toggle` | (button text "Snap") | "Snap points to grid" |
  | `Geometry`  | `m_grid_res_choice` | `Grid:` | "Snap grid resolution (mm)" |
  | `Geometry`  | `m_arc_res_choice` | `Arc:` | "Arc/bezier segment resolution" |
  | `Geometry`  | `m_native_arc_chk` | (checkbox text "G2/G3") | "Emit native G2/G3 for circular arcs" |
  | `Geometry`  | `m_fill_toggle` | (button text "Fill") | "Preview filled extrusion width" |
  | `Geometry`  | `m_length_input` | `Len:` | "Type an exact segment length (mm)" |
  | `Geometry`  | `m_snip_btn` | (button text "Snip") | "Break the continuous chain" |
  | `Quality`   | `m_initial_layer_count_spin` | `Init:` | "Number of initial layers (0–8) with independent flow and height. Default 1." |
  | `Quality`   | `m_initial_flow_spin[i]` (per initial layer, N shown) | `L{i} flow%:` | "Extrusion flow (%) for this initial layer. 100% = no reduction." |
  | `Quality`   | `m_initial_height_spin[i]` (per initial layer, N shown) | `h(mm):` | "Height (mm) override for this initial layer. Defaults to the active profile's layer height." |
  | `Quality`   | `m_wipe_check` | (checkbox text "Wipe") | "Wipe backward while retracting (anti-blob)" |
  | `Quality`   | `m_wipe_dist_spin` | `Dist:` | "Wipe distance (mm)" |
  | `Quality`   | `m_coast_spin` | `Coast:` | "Coast distance (mm); incompatible with pressure advance" |
  | `View`      | `m_measure_toggle` | (button text "Measure") | "Show measurement overlays" |
  | `View`      | `m_coord_toggle` | (button text "Coords") | "Show coordinate readout" |

  Group **titles** are exactly: `Mode`, `Tools`, `Geometry`, `Quality`, `View`, `Layers`, `Session`.

- **Quality group — special construction (as implemented).** Unlike the other groups, the Quality
  group is **assembled manually** as a `wxStaticBoxSizer(wxHORIZONTAL, this, "Quality")` rather than
  via `make_tool_group`, because it hosts per-layer **row sub-sizers**, not just flat widgets. It
  contains the **multi-initial-layer editor**: a count spin `m_initial_layer_count_spin` (N = 0–8)
  followed by a fixed pool of `kMaxInitialLayers` (8) pre-built rows (`m_initial_layer_rows[i]`),
  each a `wxBoxSizer` holding `m_initial_flow_spin[i]` (10–150 %, layer 0 default 90 %, others 100 %)
  and `m_initial_height_spin[i]` (0.04–1.0 mm, default = profile layer height). Only the first **N**
  rows are shown via `update_initial_layer_rows_visibility(n)` — rows are never added/removed
  dynamically, only shown/hidden, so sizer layout stays stable. The wipe/coast controls follow.

  This editor drives the **pre-existing** `DrawSession::initial_layer_flow_ratios` /
  `initial_layer_heights` data model (already present in the working tree, with matching
  `bbs_3mf` serialization, `DrawPathGCodeGenerator`, and `tests/libslic3r` coverage). The GUI is the
  only new part. Handlers: `on_initial_layer_count_change`, `on_initial_layer_param_change`,
  `rebuild_initial_layers_from_ui`, `update_initial_layer_rows_visibility`, `profile_layer_height`.
  This **supersedes** the former single `m_first_layer_flow_spin` ("1st flow %"), which is removed
  along with its `on_first_layer_flow_change` handler. **(Kept by user decision — the editor exposes
  a real pre-existing feature; docs updated to match the implementation.)**
- `m_prev_layer_btn` / `m_next_layer_btn` / `m_layer_label` are **removed from the toolbar** — their
  function is replaced by the new layer slider (§3.3). Keep the member pointers + handlers
  (`on_prev_layer`, `on_next_layer`) alive but no longer add them to a sizer, OR delete the buttons
  and route the handlers through the slider. **Recommended:** keep `on_prev_layer`/`on_next_layer`
  handler bodies (they contain valid navigation logic) and call them from the slider + keyboard;
  remove the two button widgets and the `m_layer_label` widget from the layout.

### 3.3 Layer slider component — `DrawLayerSlider`

A new lightweight, **native wxWidgets** custom-drawn widget (the Draw canvas is native wx, not
ImGui, so do NOT use `IMSlider`). Files: `src/slic3r/GUI/DrawLayerSlider.{hpp,cpp}`.

> Prior art: `src/slic3r/GUI/IMSlider.*` is the preview layer slider but it is ImGui-based and tied
> to the GL canvas. Use it only as a behavioral reference (scrub + tick semantics), not a base class.

#### Responsibilities

- Render a **vertical track** spanning the canvas height with the bottom = layer 0 (first/bottom
  print layer) and the top = the highest layer, matching physical print orientation.
- Draw one **tick per layer**; highlight the active layer's thumb. The thumb shows a floating label
  reading **`Layer: N`** (1-based, matching the old `m_layer_label` convention in
  `update_layer_label()`) **plus the layer's z-height** (`DrawLayer::z_end`, e.g. `Layer: 3  0.60mm`).
  This replaces the removed standalone `Layer: N` label. **(Decided — not an open question.)**
- **Drag-to-scrub:** dragging the thumb continuously changes the active layer (snap thumb to nearest
  layer tick while dragging; fire callback on each change so the canvas re-renders live).
- **Click-to-teleport:** a single click anywhere on the track jumps the active layer directly to the
  layer nearest the click position (no stepping through intermediate layers).
- **Keyboard / wheel (LOCKED — required, not optional):** when the slider has focus, **Up arrow**
  steps +1 layer and **Down arrow** steps −1 layer; **PageUp** jumps +5 and **PageDown** jumps −5;
  **Home** jumps to the top layer (`n-1`), **End** jumps to layer 0. **Mouse-wheel** over the slider
  steps +1 (wheel up) / −1 (wheel down). All clamp to `[0, n-1]` and route through the same
  `m_on_change` callback as scrub/teleport. The widget calls `SetFocus()` on left-down so keyboard
  works after a click. Bind `wxEVT_KEY_DOWN` for the arrows/page/home/end keys.
- Disabled/empty state: when `layer_count() == 0`, draw an empty track and ignore input.

#### Public API (header)

```cpp
#pragma once
#include <wx/panel.h>
#include <functional>
#include <vector>

namespace Slic3r { namespace GUI {

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
    int  layer_count() const { return (int)m_z_heights.size(); }

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

    // Layout constants (track margins, thumb size) — keep in the .cpp.
};

}} // namespace Slic3r::GUI
```

#### Rendering / interaction details

- Use a `wxBufferedPaintDC` (flicker-free) and `SetBackgroundStyle(wxBG_STYLE_PAINT)` in the ctor,
  mirroring the canvas approach in `DrawModePanel`.
- Bind: `wxEVT_PAINT`, `wxEVT_LEFT_DOWN`, `wxEVT_MOTION`, `wxEVT_LEFT_UP`, `wxEVT_MOUSEWHEEL`,
  `wxEVT_KEY_DOWN`, `wxEVT_SIZE`. Call `CaptureMouse()` on left-down and `ReleaseMouse()` on left-up
  (guard with `HasCapture()`), matching the pan logic already used in the canvas. Call `SetFocus()`
  on left-down so keyboard input is received after a click. Construct with the `wxWANTS_CHARS` style
  so arrow/page keys are delivered to the widget.
- `y_to_layer`: invert the track so **larger index → smaller Y** (layer 0 at the bottom). With track
  top margin `T`, bottom margin `B`, usable height `H = height - T - B`, and `n = layer_count()`:
  `index = round((1 - (y - T) / H) * (n - 1))`, clamped to `[0, n-1]`. For `n == 1`, always 0.
- Minimum widget width: ~64 px (`SetMinSize(wxSize(64, -1))`) so labels fit. Vertical: `wxEXPAND`.
- **Colours (LOCKED — match the canvas palette, which is hard-coded RGB; do NOT use a theme API,
  the canvas itself does not — see `DrawModePanel.cpp:204,883-998`):**

  | Element | `wxColour` | Notes |
  |---------|------------|-------|
  | Widget/track background | `wxColour(55, 55, 55)` | same as canvas bg (`DrawModePanel.cpp:204`) |
  | Track rail | `wxColour(105, 105, 105)` | 4–6 px wide vertical rounded rail |
  | Track rail fill (below active) | `wxColour(78, 78, 78)` | filled portion from bottom up to thumb |
  | Layer ticks | `wxColour(100, 100, 100)`, 1 px | one per layer |
  | Active thumb fill | `wxColour(245, 245, 220)` | matches canvas readout box (`:695`) |
  | Active thumb border | `wxColour(255, 255, 0)`, 2 px | matches canvas selection accent (`:1092`) |
  | Thumb label text | `wxColour(20, 20, 20)` on thumb; `wxColour(220, 220, 220)` for floating z-text | |

  Draw the thumb label (`Layer: N  z.zzmm`) in a small filled rounded rect using the canvas readout
  style at `DrawModePanel.cpp:694-697` (border `wxColour(20,20,20)`, fill `wxColour(245,245,220)`,
  text `wxColour(20,20,20)`) so it visually matches existing canvas overlays.

### 3.4 Wiring the slider into `DrawModePanel`

Add members to `DrawModePanel.hpp`:

```cpp
DrawLayerSlider* m_layer_slider { nullptr };
```

In the constructor, after building the canvas:

```cpp
m_layer_slider = new DrawLayerSlider(this, [this](int idx) { on_slider_layer_change(idx); });

auto* content_sizer = new wxBoxSizer(wxHORIZONTAL);
content_sizer->Add(m_canvas,       1, wxEXPAND | wxALL, 8);
content_sizer->Add(m_layer_slider, 0, wxEXPAND | wxALL, 4);  // right side (matches Preview)

main_sizer->Add(m_banner_text, 0, wxEXPAND | wxALL, 4);
main_sizer->Add(m_toolbar,     0, wxEXPAND);          // wrap sizer
main_sizer->Add(content_sizer, 1, wxEXPAND);
```

New handler (mirrors existing nav handlers, reuse their body):

```cpp
void DrawModePanel::on_slider_layer_change(int new_index)
{
    if (new_index < 0 || new_index >= m_session.layer_count()) return;
    if (new_index == m_session.active_layer) return;
    m_session.active_layer = new_index;
    sync_chain_anchor();        // keep continuation preview correct (existing helper)
    reset_draft(false);         // cancel any in-progress draft on layer change
    update_layer_label();       // still updates button enable-state
    refresh_layer_slider();     // push new active to the slider
    m_canvas->Refresh();
}
```

Add a `refresh_layer_slider()` helper and call it everywhere the layer set or active layer changes
(i.e. inside or right after: `on_add_layer`, `on_insert_below`, `on_remove_layer`,
`on_delete_layer`, `on_prev_layer`, `on_next_layer`, `on_mirror_stack`, `activate`, `reactivate`,
`load_for_edit`, undo/redo `dispatch_command`, and `update_layer_label`). The simplest robust
approach: **call `refresh_layer_slider()` at the end of `update_layer_label()`**, since that method
is already invoked after every layer mutation — then individual call sites need no change.

> **Z-reflow sync (resolved defect D1):** `rebuild_initial_layers_from_ui()` mutates layer
> `z_end` values via `reflow_layer_z()` *without* going through `update_layer_label()`, so it must
> call `refresh_layer_slider()` itself after the reflow to avoid stale z-height labels on the slider.

```cpp
void DrawModePanel::refresh_layer_slider()
{
    if (!m_layer_slider) return;
    std::vector<double> zs;
    zs.reserve(m_session.layers.size());
    for (const auto& l : m_session.layers) zs.push_back(l.z_end);
    m_layer_slider->set_layers(zs);
    m_layer_slider->set_active(m_session.active_layer);
}
```

Drop the standalone `m_layer_label` widget from the layout — the slider thumb now shows
`Layer: N` + z-height (§3.3). Keep the button enable/disable logic inside `update_layer_label()`
(rename is optional); it still governs the Layers-group buttons. Remove the `m_layer_label` member,
or leave it unused/unparented if that minimizes churn.

### 3.5 Files to add / modify

| File | Action |
|------|--------|
| `src/slic3r/GUI/DrawLayerSlider.hpp` | **new** — widget header (API in §3.3) |
| `src/slic3r/GUI/DrawLayerSlider.cpp` | **new** — widget implementation |
| `src/slic3r/GUI/DrawModePanel.hpp`   | add `m_layer_slider`, `m_toolbar` members; declare `make_tool_group`, `on_slider_layer_change`, `refresh_layer_slider`; `#include "DrawLayerSlider.hpp"` |
| `src/slic3r/GUI/DrawModePanel.cpp`   | replace toolbar construction (`206-256`) with grouped wrap-sizer + slider wiring; add new helpers; remove prev/next/layer-label from layout |
| `src/slic3r/CMakeLists.txt`          | register `GUI/DrawLayerSlider.hpp` + `.cpp` next to `DrawModePanel` entries (lines `114-115`) |

---

## 4. Constraints & invariants

1. **No data-model changes.** `DrawSession`, serialization (`bbs_3mf.cpp`), and all G-code paths are
   untouched. The slider is a *view* over `DrawSession::layers` / `active_layer`.
2. **Backward compatibility.** Existing `.3mf` projects with draw data load exactly as before — only
   the panel layout differs.
3. **Cross-platform.** Must compile and behave on Windows, macOS, Linux. Use only portable wx APIs
   (`wxWrapSizer`, `wxStaticBoxSizer`, `wxBufferedPaintDC`). No platform `#ifdef`s expected.
4. **Reuse, don't rewrite handlers.** All button/toggle handler bodies stay as-is; only their
   placement in sizers changes. Splice mode, undo/redo, keyboard hooks, draft logic untouched.
5. **Code style.** C++17, 4-space indent, 140-col limit, `PascalCase` types / `snake_case`
   members & functions, `#pragma once`, RAII (wx parent owns children — no manual `delete`).
6. **The slider never mutates state directly** — it only reports a desired index via the callback;
   the panel is the single source of truth and pushes state back via `set_active()`.

---

## 5. Build & smoke verification

Build the affected targets (verified commands for this repo):

```
cmake --build build --config RelWithDebInfo --target libslic3r_gui -- /m
cmake --build build --config RelWithDebInfo --target OrcaSlicer_app_gui -- /m
```

Manual smoke (the coding agent should at minimum confirm a clean compile/link; full UI smoke is the
testing agent's job per `02_TESTING.md`):

- App launches, Draw tab opens, toolbar groups render with titles.
- Resize window to half-screen width → toolbar **wraps** onto more rows; nothing is clipped.
- Add several layers; the slider shows that many ticks; dragging scrubs; clicking teleports; the
  canvas updates to the selected layer; active layer label / button enable-states stay correct.

---

## 6. Suggested implementation order

1. Create `DrawLayerSlider.{hpp,cpp}` standalone; register in CMake; build `libslic3r_gui`.
2. Add `m_layer_slider` + `content_sizer` wiring; verify slider shows/scrubs/teleports.
3. Add `make_tool_group` + `wxWrapSizer` toolbar; migrate each group; remove prev/next/label from
   layout; verify wrap behavior at half-screen.
4. Add `refresh_layer_slider()` call at end of `update_layer_label()`; verify slider stays in sync
   across add/insert/delete/copy/paste/mirror/undo/redo.
5. Full GUI build (`OrcaSlicer_app_gui`); hand off to testing agent.

---

## 7. Acceptance criteria (definition of done)

- [ ] `libslic3r_gui` and `OrcaSlicer_app_gui` build clean (RelWithDebInfo) on the target platform.
- [ ] Toolbar controls are grouped (Mode / Tools / Geometry / Quality / View / Layers / Session)
      with visible group separation.
- [ ] At half-screen width no control is clipped — groups wrap to additional rows.
- [ ] Control labels/tooltips match the locked table in §3.2; group titles are exactly
      Mode/Tools/Geometry/Quality/View/Layers/Session.
- [ ] A vertical layer slider sits on the **right** of the canvas, one tick per layer, active layer
      highlighted; thumb shows `Layer: N` + z-height. Colours match the locked palette in §3.3.
- [ ] Dragging the slider scrubs continuously through layers; the canvas updates live.
- [ ] Clicking the slider track jumps directly (teleports) to the clicked layer.
- [ ] Keyboard (Up/Down ±1, PageUp/Down ±5, Home/End) and mouse-wheel (±1) navigate the slider,
      clamped, routed through the same callback.
- [ ] Slider stays in sync after add / insert / delete / clear / copy / paste / mirror / undo / redo.
- [ ] No regressions: drawing, editing, splice, simulate, finalize, undo/redo, keyboard shortcuts
      all behave exactly as before.
- [ ] No changes outside the files listed in §3.5.
