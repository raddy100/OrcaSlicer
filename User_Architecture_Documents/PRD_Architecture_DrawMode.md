# PRD & Architecture: OrcaSlicer Draw Mode
**Document version:** 2.0 (Unified — consolidates base document + Addenda 1, 2, 3)  
**Status:** Ready for implementation planning  
**Audience:** AI implementation agent  

---

## Executive Summary

"Draw Mode" is a new feature that **bypasses the entire CAD → model → slice → G-code pipeline** and instead lets users directly author print-head toolpaths by drawing on a 2D canvas. The output is real G-code that can be sent to a physical FFF printer.

Think of it as the existing G-code Preview mode, but instead of *viewing* paths produced by the slicer, the user *creates* those paths by hand. There is no model, no mesh, no slicing. The user draws lines on a top-down view of the build plate; those lines become extrusion moves.

**The feature targets users who want absolute, explicit control over print-head motion** — e.g., experimental printing, single-line structures, custom extrusion art, or calibration sequences.

All print parameters (temperature, speed, nozzle size, layer height, retraction, fan, etc.) come from the **active OrcaSlicer printer/filament/process profile** — the user configures Draw Mode paths only, not settings.

---

## Confirmed Requirements

### Core Drawing (Phase 1)

| # | Requirement | Notes |
|---|-------------|-------|
| R1 | User can draw straight-line extrusion paths on a 2D top-down canvas | Phase 1: straight lines only. Curves are out of scope. |
| R2 | Paths produce real, machine-ready G-code | Not a visualization tool — the output prints |
| R3 | Continuous Z rise (vase/spiral mode) within each layer | Z increases linearly from segment to segment, not discrete jumps |
| R4 | Layer height is read from the active process profile | `layer_height` and `first_layer_height` from `DynamicPrintConfig` |
| R5 | Top-down orthographic view with previous layers shown as ghost/faded reference | Current layer highlighted; previous layers dimmed at 25% opacity |
| R6 | Travel moves are supported | When user "lifts pen" and places a new start point, a non-extruding travel move is inserted |
| R9 | Simulate the drawn paths using the existing G-code preview before printing | Feed generated G-code through GCodeProcessor → GCodeViewer |
| R10 | Save/load the drawing session inside an OrcaSlicer `.3mf` project file | Must survive round-trip without data loss |
| R11 | Export a standalone `.gcode` file | Using existing "Export G-code" mechanism |

### Profile-Driven Settings

| # | Requirement |
|---|-------------|
| P1 | All print parameters (nozzle diameter, speed, temperature, retraction, fan, G-code flavor, start/end G-code) are read from the active printer/filament/process presets — no settings UI in Draw Mode |
| P2 | Draw Mode displays a read-only profile banner: **"Using: [Printer] / [Filament] / [Process]"** so the user can confirm active settings before drawing |
| P3 | Layer height is snapshotted from the active profile when each new layer is created; geometry is preserved if profile changes later, but temperature/speed are always taken fresh at G-code generation time |

### Path Model (Copy/Paste)

| # | Requirement |
|---|-------------|
| A1 | User can "finalize" a Draw Mode session into a named path model object |
| A2 | The path model appears in the Prepare screen's object list and on the build plate |
| A3 | The path model can be dragged to any position on the build plate |
| A4 | The path model can be copy/pasted (Ctrl+C / Ctrl+V) and duplicated to produce multiple instances |
| A5 | All instances of the path model print sequentially: one instance fully completes before the next begins |
| A6 | Sequential print order is set automatically; the user does not configure it manually |
| A7 | Phase 1: position-only placement (no rotation, no scaling) |
| A8 | The path model is saved and loaded as part of the `.3mf` project file |
| A9 | One drawn design per plate — normal mesh objects and draw path objects cannot share the same plate |

### Editing, Undo/Redo, and Verification

| # | Requirement |
|---|-------------|
| E1 | Ctrl+Z / Ctrl+Y (undo/redo) removes/restores the last drawn segment while in Draw Mode |
| E2 | Clicking on an existing segment selects it (highlighted); pressing Delete or clicking "Delete Segment" removes it |
| E3 | Clicking and dragging a segment endpoint moves it; if two segments share that endpoint, both are updated |
| E4 | A "Preview Path" toggle in Draw Mode renders segments as filled-width stripes (showing actual nozzle width) for quick visual verification |
| E5 | A "Edit Drawing" button exists on the Prepare screen (right-click context menu and/or object properties panel) |
| E6 | A "Edit Drawing" button exists in the G-code Preview toolbar when a draw path object is present |
| E7 | Clicking "Edit Drawing" opens Draw Mode with the existing session loaded; editing and re-finalizing updates the object in-place without losing placed instances |
| E8 | Re-finalizing an edited drawing invalidates any cached G-code and forces a re-slice |

---

## Out of Scope (Phase 1)

- Curved or arc segments
- Per-segment speed/temperature overrides
- Drawing on top of an existing loaded model
- Support structures, brim, or skirt generation
- Variable layer heights (Phase 2 — **but the data model must support it**)
- Rotation or scaling of path model instances
- Multiple different drawn designs on the same plate

---

## Complete User Workflow

```
PRE-CONDITIONS (already done in normal OrcaSlicer usage):
  ✓ User has loaded their printer profile (e.g. Centauri Carbon)
  ✓ User has selected a filament preset (e.g. PLA Basic)
  ✓ User has selected a process preset (e.g. 0.20mm Standard)

STEP 1 — Enter Draw Mode
  Click the "Draw" tab (new, alongside Prepare / Preview)
  Profile banner shows: Centauri Carbon / PLA Basic / 0.20mm Standard
  All print parameters already set — nothing to configure

STEP 2 — Draw
  Click-drag on the canvas to draw extrusion lines
  Release and click somewhere else → travel move inserted automatically
  Previous layers shown as faded reference lines

  ┌─ Switch to Edit mode (cursor icon) at any time:
  │    Click a segment to select (highlighted) → Delete to remove
  │    Drag an endpoint to move it (connected segments update together)
  │    Ctrl+Z to undo last change; Ctrl+Y to redo
  └─ Switch back to Draw mode (pencil icon) to add more segments

  STEP 2a — Quick Verify (optional, any time):
    Click "Preview Path" toggle
    Canvas switches to filled-width view showing nozzle footprint
    Toggle off to return to wire view for editing

  When finished with the current layer → click "Next Layer ▶"
  Repeat for all layers needed

STEP 3 — Finalize
  Click "Finalize as Model → Prepare"
  A path model object is created and placed on the build plate
  OrcaSlicer automatically switches to the Prepare screen

STEP 4 — Arrange (optional)
  Ctrl+C / Ctrl+V to create copies of the path model
  Drag copies to fill the build plate
  OrcaSlicer shows sequential print clearance zones between copies

STEP 5 — Slice & Verify
  Click "Slice" (same button as always)
  G-code generated using DrawPathGCodeGenerator with active profile
  Existing G-code Preview opens — paths are visible exactly as they will print

  If something looks wrong in G-code Preview:
    Click "Edit Drawing" in the toolbar
    → Returns to Draw Mode with session loaded
    → Make corrections (undo/redo, delete, move endpoints)
    → Click "Finalize" → updates object in-place, preserves all copies
    → Returns to Prepare screen; must re-slice to see updated preview

STEP 6 — Print
  Click "Print" or "Export G-code"
  Done
```

---

## Architecture Overview

Draw Mode introduces six new components and extends five existing ones:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           NEW COMPONENTS                                │
│                                                                         │
│  ┌────────────────┐    ┌──────────────────────────────────────────┐    │
│  │  DrawSession   │    │  DrawModePanel (wxPanel)                 │    │
│  │  (Data Model)  │◄──►│  - Top-down canvas (GLCanvas3D)          │    │
│  │                │    │  - Profile banner (read-only)            │    │
│  │  DrawLayer[]   │    │  - Layer navigation                      │    │
│  │  DrawSegment[] │    │  - Undo/redo stack                       │    │
│  └───────┬────────┘    │  - Draw / Edit mode toggle               │    │
│          │              │  - "Preview Path" toggle                │    │
│          │              └──────────────────────────────────────────┘    │
│          │              ┌──────────────────────────────────────────┐    │
│          │              │  DrawModeInputHandler                    │    │
│          │              │  - Drawing mode (click-drag = new seg)   │    │
│          │              │  - Editing mode (click = select/move)    │    │
│          │              │  - Ray-to-plane coord mapping           │    │
│          │              └──────────────────────────────────────────┘    │
│          │              ┌──────────────────────────────────────────┐    │
│          │              │  DrawModeCommands                        │    │
│          │              │  - AddSegmentCommand                     │    │
│          │              │  - DeleteSegmentCommand                  │    │
│          │              │  - MoveEndpointCommand                   │    │
│          │              │  - AddLayerCommand / DeleteLayerCommand  │    │
│          │              └──────────────────────────────────────────┘    │
│  ┌───────▼────────┐    ┌──────────────────────────────────────────┐    │
│  │ DrawPathGCode  │    │  DrawModeSerializer                      │    │
│  │ Generator      │    │  - Reads/writes draw_session_obj_N.xml   │    │
│  │ - Uses         │    │  - Plugs into bbs_3mf exporter/importer  │    │
│  │   GCodeWriter  │    └──────────────────────────────────────────┘    │
│  │ - Reads full   │                                                     │
│  │   DynamicPrint │                                                     │
│  │   Config       │                                                     │
│  └───────┬────────┘                                                     │
└──────────┼──────────────────────────────────────────────────────────────┘
           │
┌──────────▼──────────────────────────────────────────────────────────────┐
│                   EXISTING COMPONENTS (extended)                        │
│                                                                         │
│  GCodeWriter ──► GCodeProcessor ──► GCodeViewer (Preview)              │
│  bbs_3mf exporter/importer (new per-object files in archive)           │
│  GLCanvas3D (new CanvasDrawMode enum value + mouse routing)             │
│  ModelObject (new draw_session member + draw_path_object config flag)   │
│  Plater (edit_draw_path_object() method + right-click menu)            │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Component 1: DrawSession Data Model

**Location:** `src/libslic3r/DrawSession.hpp` / `DrawSession.cpp` (new files)

This is the pure-data layer with no GUI dependencies. It must be serializable and work on all platforms.

### Data Structures

```cpp
namespace Slic3r {

struct DrawSegment {
    Vec2d  start;        // XY on build plate, plate-relative coords (mm)
    Vec2d  end;          // XY on build plate, plate-relative coords (mm)
    bool   is_travel;    // true = non-extruding travel move
};

struct DrawLayer {
    int                       layer_index;   // 0-based
    double                    z_start;       // mm — Z at first segment start
    double                    z_end;         // mm — Z at last segment end
    // z_end - z_start = layer_height baked in from profile at layer creation time.
    // Geometry is preserved even if profile changes later.
    std::vector<DrawSegment>  segments;      // in draw order
};

struct DrawSession {
    std::vector<DrawLayer>    layers;        // in Z order
    int                       active_layer;  // index into layers[], -1 if none

    bool         is_empty() const;
    void         add_layer(double layer_height);  // layer_height from active profile
    void         clear();
    double       total_height() const;
    BoundingBoxf3 bounding_box() const;     // for synthetic mesh creation
    int           layer_count() const { return (int)layers.size(); }
};

} // namespace Slic3r
```

### Key Design Notes

- **No settings stored.** All print parameters (temperature, speed, nozzle diameter, filament diameter, retraction, fan, G-code flavor, start/end G-code templates) come from `DynamicPrintConfig` at G-code generation time. `DrawSession` stores geometry only.
- **`Vec2d` coordinates are relative to the part plate origin** (`PartPlate::get_origin()`). G-code generation adds the plate offset. Sessions are portable if the plate moves.
- **`DrawLayer::z_end - z_start`** is captured from the active profile's `layer_height` when `add_layer()` is called and baked in. The geometry is preserved even if the profile later changes; temperature and speed always use the current profile at generation time.
- **Per-layer height hook for Phase 2**: `DrawLayer` stores its own height. In Phase 1 the UI always uses the profile's `layer_height`. In Phase 2 a per-layer UI can unlock variable heights with no data model change.
- **Continuous Z within a layer**: When generating G-code, Z is distributed linearly across all non-travel segments in a layer. Each extrusion segment advances Z by `layer_height / non_travel_segment_count`. This matches the existing spiral vase logic in `src/libslic3r/GCode.cpp` (`spiral_vase`).

---

## Component 2: DrawModePanel (GUI)

**Location:** `src/slic3r/GUI/DrawModePanel.hpp` / `DrawModePanel.cpp` (new files)

**Parent:** `MainFrame` owns it alongside the existing `Plater`. The mode switcher (top toolbar) shows **Prepare | Draw | Preview** tabs.

### GLCanvas3D Canvas Type

Add a new enum value to `GLCanvas3D::EType` in `src/slic3r/GUI/GLCanvas3D.hpp`:

```cpp
enum class EType : unsigned char {
    // ... existing values ...
    CanvasView3D,
    CanvasPreview,
    CanvasAssembleView,
    CanvasDrawMode   // ← NEW
};
```

When `CanvasDrawMode` is active, `GLCanvas3D` must:
- Use an **orthographic top-down camera** (lock camera to Z-up, looking straight down)
- Disable model-selection and normal gizmos
- Enable the Draw Mode input handler

### UI Layout

```
┌─────────────── Draw Mode ───────────────────────────────┐
│ [Profile Banner — read-only]                            │
│ 🖨 Centauri Carbon │ 🧵 PLA Basic │ ⚙ 0.20mm Standard  │
│ Layer: 0.20mm │ Nozzle: 0.4mm │ Temp: 220°C            │
│                                       [Change Profile ↗] │
│─────────────────────────────────────────────────────────│
│                                                         │
│  [✏ Draw] [↖ Edit]  [👁 Preview Path]                  │
│                                                         │
│  ┌───────────────────────────────────────────────────┐  │
│  │                                                   │  │
│  │       Top-down build plate canvas                 │  │
│  │       (click-drag to draw lines in Draw mode)     │  │
│  │       (click to select/drag in Edit mode)         │  │
│  │                                                   │  │
│  └───────────────────────────────────────────────────┘  │
│                                                         │
│  Layer 1 of 3  (Z: 0.00 → 0.20 mm)                     │
│  [◀ Prev Layer]  [Next Layer ▶]                         │
│                                                         │
│  [  Clear Layer  ]  [ Delete Layer ]  [ Delete Segment ]│
│                                                         │
│  ════════════════════════════════════════════════        │
│  [   Finalize as Model  →  Prepare  ]                   │
└─────────────────────────────────────────────────────────┘
```

- **Profile Banner**: Read-only display of active printer/filament/process preset names and key values (layer height, nozzle diameter, temperature), read from `wxGetApp().preset_bundle()`. "Change Profile ↗" opens the existing OrcaSlicer profile selector.
- **Draw / Edit toggle**: Switches `DrawModeInputHandler` between `DrawInputMode::Drawing` and `DrawInputMode::Editing` (see Component 3).
- **Preview Path toggle**: Switches canvas rendering between wire view (thin centreline lines) and filled-width view (nozzle footprint quads).

### Rendering

Rendering in `DrawModePanel::render()` (called by `GLCanvas3D::on_render()` when in draw mode):

1. **Build plate grid**: Reuse existing `3DBed` rendering or draw a simple grid overlay.
2. **Previous layers (ghost)**: Render all layers below `active_layer` at alpha ≈ 0.25 in grey.
3. **Active layer — wire view (default)**:
   - Unselected extrusion segments: solid orange line (`#F2862A`)
   - Selected extrusion segment: solid yellow line with endpoint circles
   - Travel segments: dashed blue
4. **Active layer — preview view** (when "Preview Path" is toggled on):
   - Each non-travel segment rendered as a filled quad (two triangles), width = `nozzle_diameter` from active profile, aligned to segment direction — using `GLModel` with a dynamic mesh
   - Travel segments: dashed blue (same as wire view)
   - Previous layers rendered as filled quads at 25% opacity
5. **Rubber-band line**: While dragging in Draw mode, render a thin line from `m_segment_start` to current mouse position.

Use `GLModel` line and triangle primitives (already used by existing gizmos) for all rendering. No G-code generation is needed for the preview view — it is purely geometric.

### Undo/Redo Stack

The command stack lives on `DrawModePanel` (not `DrawSession` — it is a UI concern):

```cpp
std::vector<std::unique_ptr<DrawCommand>> m_undo_stack;
std::vector<std::unique_ptr<DrawCommand>> m_redo_stack;
int m_editing_obj_idx = -1;  // ≥0 when editing an existing DrawPathObject
```

Behaviour:
- Max stack depth: 100 commands (oldest dropped when exceeded)
- Ctrl+Z pops from undo, calls `command->undo()`, pushes to redo
- Ctrl+Y pops from redo, calls `command->redo()`, pushes to undo
- Stack is **cleared on Finalize** (session is now immutable as a model)
- Stack is **cleared on `load_for_edit()`** (fresh edit starts empty)
- Keyboard shortcuts intercepted via `wxEVT_CHAR_HOOK` on `DrawModePanel`; call `evt.Skip(false)` to prevent reaching OrcaSlicer's global undo handler

### Finalize Logic

`DrawModePanel::on_finalize()` checks `m_editing_obj_idx`:

```
If m_editing_obj_idx == -1:   → CREATE new ModelObject (original path)
If m_editing_obj_idx >= 0:    → UPDATE existing ModelObject in-place
```

**Create path** (new object):
1. Validate session has at least one segment
2. Compute `BoundingBoxf3` from all session segments across all layers
3. Create 12-triangle box mesh via `TriangleMesh::make_cube(sx, sy, sz)`
4. `model.add_object()` with `draw_path_object = true` config flag
5. Attach deep copy of `m_session` as `obj->draw_session`
6. Add box mesh as sole `ModelVolume` (`vol->name = "path_footprint"`)
7. Add one `ModelInstance` centered on the current plate
8. Set `print_sequence = PrintSequence::ByObject` on the plate config
9. Notify `Plater::update()` and `ObjectList`
10. `MainFrame::select_tab(TabPrepare)`

**Update path** (re-edit of existing object):
1. Recompute bounding box mesh from updated session
2. Replace `obj->draw_session` with updated session
3. Replace `obj->volumes[0]` mesh with new bounding box mesh
4. Preserve all `ModelInstance`s (positions unchanged)
5. `wxGetApp().plater()->set_project_dirty()` — invalidates G-code cache
6. `wxGetApp().plater()->update()` — triggers re-render
7. `MainFrame::select_tab(TabPrepare)`
8. Reset `m_editing_obj_idx = -1`

### load_for_edit()

```cpp
void DrawModePanel::load_for_edit(ModelObject* obj)
{
    // obj must have draw_path_object == true
    m_session = *obj->draw_session;       // deep copy
    m_editing_obj_idx = obj_idx;          // caller provides index
    m_undo_stack.clear();
    m_redo_stack.clear();
    // Set active_layer to last layer so user sees most recent work
    m_session.active_layer = m_session.layer_count() - 1;
}
```

---

## Component 3: DrawModeInputHandler

**Location:** `src/slic3r/GUI/DrawModeInputHandler.hpp` / `.cpp` (new files)

### Input Modes

```cpp
enum class DrawInputMode {
    Drawing,   // click-drag creates new segments
    Editing    // click selects/moves existing segments
};
```

### Mouse Routing

In `GLCanvas3D::on_mouse()`, add before the gizmo check:

```cpp
if (m_type == EType::CanvasDrawMode) {
    if (m_draw_input_handler.on_mouse(evt, *this)) {
        m_dirty = true;
        return;
    }
}
```

### Drawing Mode — Mouse Logic

Draw Mode does **not** use `GLGizmoPainterBase` (which raycasts against meshes). It uses a **ray-to-plane intersection**: the user's 2D mouse position is unprojected onto the build plate plane (Z = current layer's `z_end`) using the orthographic camera matrix.

```
LeftDown  → record m_segment_start (screen_to_plate), set m_is_drawing = true
LeftUp    → if m_is_drawing:
              end = screen_to_plate(pos)
              if distance(start, end) > SNAP_THRESHOLD (0.1 mm):
                if start != last_segment.end (within SNAP_THRESHOLD):
                  dispatch AddSegmentCommand for travel move first
                dispatch AddSegmentCommand{start, end, is_travel=false}
              m_is_drawing = false
Motion    → if m_is_drawing: update rubber-band preview line
```

### Editing Mode — Hit Testing

In Editing mode, `on_mouse(LeftDown)` checks **endpoint proximity first**, then segment body:

```
Priority 1 — Endpoint hit:
  For each segment s in active_layer:
    if distance(mouse_pos, s.start) < ENDPOINT_SNAP_RADIUS (1.0 mm):
        → begin endpoint drag
    if distance(mouse_pos, s.end) < ENDPOINT_SNAP_RADIUS:
        → begin endpoint drag

Priority 2 — Segment body hit:
  For each segment s in active_layer:
    d = point_to_segment_distance(mouse_pos, s.start, s.end)
    if d < HIT_THRESHOLD (nozzle_diameter × 2.0):
        → select this segment (m_selected_segment_index = i)
```

### Endpoint Dragging — Shared Endpoints

```cpp
struct EndpointRef { int segment_index; bool is_start; };
std::vector<EndpointRef> m_dragging_endpoints;

// On drag start: find ALL endpoints within SNAP_THRESHOLD of drag origin
m_dragging_endpoints.clear();
for (int i = 0; i < active_layer.segments.size(); ++i) {
    if (distance(active_layer.segments[i].start, drag_origin) < SNAP_THRESHOLD)
        m_dragging_endpoints.push_back({i, true});
    if (distance(active_layer.segments[i].end, drag_origin) < SNAP_THRESHOLD)
        m_dragging_endpoints.push_back({i, false});
}

// On motion: apply delta in-place (live preview, no command yet)
// On mouse up: dispatch MoveEndpointCommand({old_positions, new_positions})
```

Connected path nodes (e.g., end of segment A = start of segment B) move together, preserving path continuity.

**Scope note:** Editing is restricted to the **active layer** only. Segments on other layers are visible as ghosts but are not selectable or draggable.

---

## Component 4: DrawModeCommands

**Location:** `src/slic3r/GUI/DrawModeCommands.hpp` / `.cpp` (new files)

```
DrawCommand (abstract base)
  ├── AddSegmentCommand    { layer_index, segment_index, DrawSegment copy }
  │     undo → remove segment at [layer_index][segment_index]
  │     redo → re-insert at same position
  │
  ├── DeleteSegmentCommand { layer_index, segment_index, DrawSegment copy }
  │     undo → re-insert segment
  │     redo → remove again
  │
  ├── MoveEndpointCommand  { list of { layer_index, segment_index, is_start, old_pos, new_pos } }
  │     undo → restore all old positions
  │     redo → apply all new positions
  │
  ├── AddLayerCommand      { DrawLayer copy }
  │     undo → remove last layer
  │     redo → re-append layer
  │
  └── DeleteLayerCommand   { layer_index, DrawLayer copy }
        undo → re-insert layer at layer_index
        redo → remove again
```

`AddLayerCommand` and its inverse treat the layer as an atomic unit — undoing "Add Layer" removes the entire layer including all its segments in one step.

---

## Component 5: DrawPathGCodeGenerator

**Location:** `src/libslic3r/DrawPathGCodeGenerator.hpp` / `.cpp` (new files)

This class converts a `DrawSession` into a complete `.gcode` string using the existing `GCodeWriter` class. All parameters are read from `DynamicPrintConfig`.

```cpp
class DrawPathGCodeGenerator {
public:
    // full_config: merged printer + filament + process config
    //   from wxGetApp().preset_bundle()->full_config() or Print's merged config
    // plate_origin: Vec2d offset from machine zero to plate origin
    DrawPathGCodeGenerator(const DynamicPrintConfig& full_config,
                           const Vec2d&              plate_origin);

    // Returns complete G-code string for one instance of the session
    // (all layers, translated to absolute machine coords via plate_origin + instance_offset)
    std::string generate(const DrawSession& session,
                         const Vec2d& instance_offset = Vec2d::Zero());

private:
    GCodeWriter        m_writer;
    DynamicPrintConfig m_config;
    Vec2d              m_plate_origin;

    std::string generate_preamble();
    std::string generate_layer(const DrawLayer& layer, bool is_first_layer);
    std::string generate_postamble();
    double      calc_extrusion(double segment_length_mm) const;
};
```

### Parameters Read from DynamicPrintConfig

| Parameter | Config Key | Used For |
|-----------|------------|----------|
| Layer height | `layer_height` | Z distribution per layer |
| First layer height | `first_layer_height` | Z of first layer |
| Nozzle diameter | `nozzle_diameter` | Extrusion width, E calculation |
| Filament diameter | `filament_diameter` | E calculation denominator |
| Nozzle temperature | `nozzle_temperature` | M104/M109 in preamble |
| First layer temperature | `nozzle_temperature_initial_layer` | M109 for layer 0, then switch |
| Bed temperature | `bed_temperature` | M190 in preamble |
| Print speed | `outer_wall_speed` (or `default_speed`) | F value in extrusion moves |
| First layer speed | `initial_layer_speed` | F value for layer 0 moves |
| Retraction length | `retraction_length` | Before travel moves |
| Retraction speed | `retraction_speed` | Retraction F value |
| Fan speed | `fan_min_speed` / `fan_max_speed` | M106 commands |
| G-code flavor | `gcode_flavor` | Dialect (Marlin/Klipper/etc.) |
| Machine start G-code | `machine_start_gcode` | Preamble template |
| Machine end G-code | `machine_end_gcode` | Postamble template |

First-layer handling is automatic: the generator checks `layer.layer_index == 0` and uses `nozzle_temperature_initial_layer` and `initial_layer_speed` for that layer only.

### G-code Generation Logic

```
generate_preamble():
  m_writer.preamble()                          → G90, G21, M82/M83
  machine_start_gcode template (homing, etc.)
  set_temperature(first_layer_temp, wait=true) → M109 Sxxx
  set_bed_temperature(bed_temp, wait=true)     → M190 Sxxx
  travel_to_xy(first_point)                   → G0 to start position

generate_layer(layer):
  if layer_index == 0: use first_layer_temp, first_layer_speed
  else:                use nozzle_temperature, outer_wall_speed

  Continuous Z distribution:
    extrusion_segs = [s for s in layer.segments if not s.is_travel]
    z_per_seg = (layer.z_end - layer.z_start) / max(len(extrusion_segs), 1)
    current_z = layer.z_start

  For each segment in layer.segments:
    abs_start = segment.start + plate_origin + instance_offset
    abs_end   = segment.end   + plate_origin + instance_offset
    if segment.is_travel:
      m_writer.retract()
      m_writer.travel_to_xyz({abs_end.x, abs_end.y, current_z})
      m_writer.unretract()
    else:
      target_z = current_z + z_per_seg
      dE = calc_extrusion(segment.length())
      m_writer.extrude_to_xyz({abs_end.x, abs_end.y, target_z}, dE)
      current_z = target_z

generate_postamble():
  retract()
  travel_to_z(current_z + 10)      → lift nozzle
  set_temperature(0, wait=false)   → M104 S0
  set_fan(0)                       → M106 S0
  machine_end_gcode template
  m_writer.postamble()
```

### Extrusion Calculation

```
filament_radius = filament_diameter / 2.0
filament_area   = π × filament_radius²
extrusion_area  = nozzle_diameter × layer_height   // rectangle approximation
E_per_mm        = extrusion_area / filament_area × extrusion_multiplier
dE              = E_per_mm × segment_length_mm
```

This is identical to how OrcaSlicer computes `ExtrusionPath::mm3_per_mm` in the normal slicing pipeline.

### Key GCodeWriter Methods Used

From `src/libslic3r/GCodeWriter.hpp/.cpp`:
- `preamble()` — G90, G21, M82/M83, E reset
- `postamble()` — flavor-specific end sequence
- `set_temperature(int temp, bool wait, int tool)` — M104/M109
- `set_bed_temperature(int temp, bool wait)` — M140/M190
- `set_fan(unsigned int speed)` — M106
- `travel_to_xyz(const Vec3d&, comment)` — G0 move
- `extrude_to_xyz(const Vec3d&, double dE, comment)` — G1 E move
- `retract(bool before_wipe, double length)` — retraction
- `unretract()` — prime after travel

---

## Component 6: DrawPathObject (ModelObject Adapter)

**This is not a new class.** It is a regular `ModelObject` with:
1. A config option `draw_path_object = true` marking it as a path object
2. A `DrawSession` stored directly on it (see below)
3. A synthetic bounding-box mesh as its sole `ModelVolume` (for rendering and collision)

### Config Flag

In `src/libslic3r/PrintConfig.hpp`, in the `ModelConfig` options block:

```cpp
((ConfigOptionBool, draw_path_object))  // true = this is a Draw Mode path object
```

Default: `false`. Normal sliceable objects are unaffected.

### DrawSession on ModelObject

In `src/libslic3r/Model.hpp`, add to `ModelObject`:

```cpp
// Owned only when draw_path_object config option is true.
// nullptr for all normal sliceable objects.
std::unique_ptr<DrawSession> draw_session;
```

In `ModelObject::clone()`, ensure deep copy:
```cpp
draw_session = src.draw_session ? std::make_unique<DrawSession>(*src.draw_session) : nullptr;
```

### Synthetic Bounding Box Mesh

When "Finalize as Model" is called, compute the 2D bounding box of all segments across all layers and create a box mesh:

```
bbox   = union of all segment endpoints in all layers (XY)
height = DrawSession::total_height()
mesh   = TriangleMesh::make_cube(bbox.size().x(), bbox.size().y(), height)
```

This 12-triangle mesh:
- Renders as a visible footprint in the Prepare screen
- Participates in existing collision/clearance detection (`update_sequential_clearance()`)
- Does **not** get sliced — the `draw_path_object` flag prevents that

### Copy/Paste — Free from Existing Infrastructure

Once the `DrawPathObject` is a proper `ModelObject` in `Model::objects`, the following all work **without new code**:

| Action | Mechanism | Result |
|--------|-----------|--------|
| **Drag on plate** | `GLCanvas3D::on_mouse()` → `ModelInstance::set_offset()` | Instance moves; `draw_session` stays on parent `ModelObject` |
| **Ctrl+C / Ctrl+V** | `Plater::copy_selection_to_clipboard()` + `paste_from_clipboard()` | New `ModelInstance` added to the same `ModelObject` |
| **Right-click → Add Instance** | `Plater::increase_instances()` | New `ModelInstance` with offset |
| **Delete instance** | Existing delete logic | Instance removed; if last, object removed |
| **ObjectList display** | `GUI_ObjectList::add_object_to_list()` | Appears in left panel with sub-rows per instance |
| **Save to .3mf** | Extended `bbs_3mf` exporter | `draw_session` serialized to `Metadata/draw_session_obj_N.xml` |

The `DrawSession` is owned by the `ModelObject`, not the `ModelInstance`. All instances share one path design; only their XY offset (`ModelInstance::get_offset()`) differs.

### One Design Per Plate

Mixed draw + mesh objects on the same plate must be blocked:
- In `Plater::load_files()`: if target plate has a `draw_path_object`, refuse mesh objects (and vice versa)
- In `Plater::paste_from_clipboard()`: same check
- Show error: *"This plate contains a Draw Path object. Normal mesh objects cannot be added to the same plate."*

### Sequential Printing

`on_finalize()` automatically sets `print_sequence = PrintSequence::ByObject` on the plate config. Show a one-time notification:
> *"Draw Path objects always print sequentially (one at a time). Print sequence has been set to 'By Object' for this plate."*

### Round-Trip "Edit Drawing"

`Plater::edit_draw_path_object(int obj_idx)`:

```cpp
void Plater::edit_draw_path_object(int obj_idx)
{
    ModelObject* obj = model().objects[obj_idx];
    assert(obj->config.opt_bool("draw_path_object"));
    DrawModePanel* panel = wxGetApp().mainframe()->draw_mode_panel();
    panel->load_for_edit(obj, obj_idx);
    wxGetApp().mainframe()->switch_to_draw_mode();
}
```

Entry points that call this:
1. **Prepare screen** — right-click context menu on a draw path object → "Edit Drawing"
2. **Prepare screen** — object properties panel → "Edit Drawing" button
3. **G-code Preview toolbar** — "Edit Drawing" button (visible when plate contains a draw path object)

After re-finalizing from G-code Preview, navigate to the Prepare screen (not back to Preview — G-code is now stale and must be re-sliced).

---

## Component 7: DrawModeSerializer

**Location:** Extend `src/libslic3r/Format/bbs_3mf.cpp` / `bbs_3mf.hpp`

### Archive Paths (Per-Object)

```
Metadata/draw_session_obj_0.xml    ← DrawSession for Model::objects[0]
Metadata/draw_session_obj_1.xml    ← DrawSession for Model::objects[1]
...
```

Keyed by the `ModelObject`'s index in the archive. On load, after the `ModelObject` list is reconstructed, each draw session is matched back by index.

The `draw_path_object` config flag is serialized in the normal per-object config block (already handled by `bbs_3mf` as part of `ModelObject::config`).

### XML Schema

```xml
<?xml version="1.0" encoding="UTF-8"?>
<draw_session version="1">
  <layers>
    <layer index="0" z_start="0.0" z_end="0.2">
      <segment x1="10.0" y1="10.0" x2="50.0" y2="10.0" travel="0"/>
      <segment x1="50.0" y1="10.0" x2="50.0" y2="50.0" travel="0"/>
      <segment x1="50.0" y1="50.0" x2="80.0" y2="20.0" travel="1"/>
      <segment x1="80.0" y1="20.0" x2="100.0" y2="20.0" travel="0"/>
    </layer>
    <layer index="1" z_start="0.2" z_end="0.4">
      <!-- ... -->
    </layer>
  </layers>
</draw_session>
```

Note: no `<settings>` block — all print parameters come from the active profile at runtime, not the saved session.

### Serialization Implementation

Extend `_BBS_3MF_Exporter`:
```cpp
bool _add_draw_session_to_archive(mz_zip_archive& archive,
                                   const DrawSession& session,
                                   int obj_index);
```

Extend `_BBS_3MF_Importer`:
```cpp
bool _extract_draw_session_from_archive(mz_zip_archive& archive,
                                         const mz_zip_archive_file_stat& stat,
                                         DrawSession& out_session);
```

Use `boost::property_tree` XML (matching the existing `custom_gcode_per_layer.xml` pattern). On load, after the model is reconstructed, match each `draw_session_obj_N.xml` back to `model.objects[N]`.

### Version Migration

- `version="1"` attribute on root element
- Missing or version 0: treat as legacy (empty session)
- Phase 2 variable layer heights: bump to version 2; handle old files by keeping `z_start`/`z_end` as stored

---

## G-code Generation for Multiple Instances

### Detection and Dispatch

In `BackgroundSlicingProcess::process_fff()` (or equivalent), add a pre-pass before normal G-code generation:

```
For each ModelObject in model.objects:
    if obj->config.opt_bool("draw_path_object"):
        Mark as "pre-generated" — do NOT feed to normal Print::process()
        For each ModelInstance (in ByObject print order):
            offset = instance.get_offset().head<2>()
            gen = DrawPathGCodeGenerator(full_config, plate_origin)
            gcode_block = gen.generate(*obj->draw_session, offset)
            // Inject into final G-code at the correct sequential position
```

### Output Flow (Phase 1 — Minimal Invasiveness)

```
1. Normal slicing + G-code generation runs for all non-draw objects
2. For each DrawPathObject on the plate (in plate order):
     For each ModelInstance (in ByObject print order):
       Generate G-code for that instance at its offset
       Inject into final .gcode file at the correct position
3. Final .gcode = [preamble] + [draw instances in by-object order] + [postamble]
```

Phase 1 assumes a plate contains **only** draw path objects (no mixed draw + normal objects in one print job). Phase 2 adds proper interleaving with normal objects via a virtual dispatch mechanism in `Print`.

---

## Simulate → GCodeViewer

When the user clicks "Simulate" / "Preview" after slicing:

```cpp
void DrawModePanel::on_simulate() {
    DrawPathGCodeGenerator gen(wxGetApp().preset_bundle()->full_config(),
                               m_plate->get_origin().head<2>());
    std::string gcode_str = gen.generate(m_session);

    GCodeProcessor processor;
    processor.process_string(gcode_str, [](float){});
    GCodeProcessorResult result = std::move(processor.extract_result());

    wxGetApp().plater()->get_current_canvas3D()
        ->get_gcode_viewer().load_as_gcode(result, ...);

    wxGetApp().mainframe()->select_tab(MainFrame::TabPreview);
}
```

`GCodeViewer::load_as_gcode()` accepts an in-memory `GCodeProcessorResult` — no disk file required.

---

## Coordinate Systems

```
Screen coords (pixels)
        ↓  GLCanvas3D ray-to-plane intersection (orthographic camera)
Plate coords (mm, relative to PartPlate::get_origin())
        ↓  + plate_origin (Vec2d) + instance_offset (Vec2d)
Machine/World coords (mm, absolute from machine zero)
        ↓  GCodeWriter outputs these
G-code file
```

- **`DrawSession` stores plate-relative coords** — sessions are portable if the plate is moved
- **`DrawPathGCodeGenerator` adds plate origin + instance offset** when calling `GCodeWriter`
- Plate origin: `PartPlate::get_origin()` returns `Vec3d`; use `.head<2>()` for XY

---

## Mode Switcher Integration

**File:** `src/slic3r/GUI/MainFrame.cpp` / `MainFrame.hpp`

```cpp
// MainFrame::init_tabpanel() or equivalent
m_mode_toolbar->AddButton(Draw_icon, _L("Draw"), [this]{ switch_to_draw_mode(); });

void MainFrame::switch_to_draw_mode() {
    m_plater->Hide();
    m_draw_mode_panel->Show();
    m_draw_mode_panel->activate(m_plater->get_current_plate());
}
```

`DrawModePanel::activate(PartPlate*)` receives the current plate to read dimensions and origin for coordinate conversion.

---

## Dependencies Summary

| Dependency | Type | Location | Purpose |
|------------|------|----------|---------|
| `GCodeWriter` | Internal (existing) | `src/libslic3r/GCodeWriter.hpp` | Emit G-code commands |
| `GCodeProcessor` | Internal (existing) | `src/libslic3r/GCode/GCodeProcessor.hpp` | Parse generated G-code for simulation |
| `GCodeViewer` | Internal (existing) | `src/slic3r/GUI/GCodeViewer.hpp` | Display simulation; `load_as_gcode()` |
| `DynamicPrintConfig` | Internal (existing) | `src/libslic3r/PrintConfig.hpp` | All print parameters from active preset |
| `PresetBundle` | Internal (existing) | `src/slic3r/GUI/Preset.hpp` | `full_config()` — merged active presets |
| `bbs_3mf` exporter/importer | Internal (existing, extended) | `src/libslic3r/Format/bbs_3mf.{hpp,cpp}` | Save/load `.3mf` |
| `GLCanvas3D` | Internal (existing, extended) | `src/slic3r/GUI/GLCanvas3D.{hpp,cpp}` | Canvas, mouse routing, orthographic camera |
| `PartPlate` | Internal (existing) | `src/slic3r/GUI/PartPlate.hpp` | Plate dimensions and origin offset |
| `Model` / `ModelObject` / `ModelInstance` | Internal (existing, extended) | `src/libslic3r/Model.hpp` | Add `draw_session` and `draw_path_object` config |
| `MainFrame` | Internal (existing, extended) | `src/slic3r/GUI/MainFrame.{hpp,cpp}` | Add Draw mode tab and panel slot |
| `Plater` | Internal (existing, extended) | `src/slic3r/GUI/Plater.{hpp,cpp}` | `edit_draw_path_object()`, mixed-plate blocking |
| `GUI_Preview` | Internal (existing, extended) | `src/slic3r/GUI/GUI_Preview.cpp` | "Edit Drawing" toolbar button |
| `GLModel` | Internal (existing) | `src/slic3r/GUI/GLModel.hpp` | Line and filled-quad primitive rendering |
| `TriangleMesh::make_cube` | Internal (existing) | `src/libslic3r/TriangleMesh.hpp` | Synthetic bounding box mesh |
| `boost::property_tree` | External (already used) | deps | XML serialization |
| `miniz` (`mz_zip_*`) | External (already used) | deps | .3mf archive compression |
| `wxWidgets` | External (already used) | deps | UI panels, events, keyboard handling |

**No new external library dependencies are required.**

---

## Key Architectural Decisions & Rationale

| Decision | Rationale |
|----------|-----------|
| **No settings in DrawSession** | All parameters from active profile. Simplifies data model; makes Draw Mode a first-class profile-aware feature. No settings to keep in sync. |
| **Tag ModelObject, don't subclass** | `ModelObject` is not designed for subclassing. A config flag + `draw_session` member gives free Prepare-screen integration (drag, copy/paste, ObjectList) without invasive changes. |
| **DrawSession on ModelObject, not Model** | Supports future multiple drawn designs per model. Follows existing `ModelObject::layer_height_profile` pattern. |
| **New panel, not a gizmo** | `GLGizmoPainterBase` raycasts against meshes. Draw Mode has no mesh. A dedicated `DrawModePanel` with `CanvasDrawMode` is cleaner. |
| **Direct GCodeWriter, bypass Print** | `Print` drives slicing — irrelevant here. Using `GCodeWriter` directly is simpler and exactly what `GCode::do_export()` uses internally. |
| **Continuous Z within a layer** | Matches vase mode feel. Distributes Z linearly across segments — identical to `SpiralVasePass` in `src/libslic3r/GCode.cpp`. |
| **Plate-relative coordinates in DrawSession** | Decouples drawing data from machine position. Sessions work correctly if the plate is moved. |
| **Reuse GCodeViewer for simulation** | Zero duplication. `GCodeViewer::load_as_gcode()` accepts in-memory `GCodeProcessorResult` — no disk file needed. |
| **Command stack on DrawModePanel, not DrawSession** | Undo/redo is a UI concern. `DrawSession` stays a clean, serializable data object. |
| **Synthetic bbox mesh for Prepare rendering** | 12 triangles, no custom renderer needed. Participates in existing clearance detection. Phase 2 can overlay actual path lines. |

---

## Complete File Manifest

### New Files to Create

```
src/libslic3r/DrawSession.hpp
src/libslic3r/DrawSession.cpp
src/libslic3r/DrawPathGCodeGenerator.hpp
src/libslic3r/DrawPathGCodeGenerator.cpp
src/slic3r/GUI/DrawModePanel.hpp
src/slic3r/GUI/DrawModePanel.cpp
src/slic3r/GUI/DrawModeInputHandler.hpp
src/slic3r/GUI/DrawModeInputHandler.cpp
src/slic3r/GUI/DrawModeCommands.hpp
src/slic3r/GUI/DrawModeCommands.cpp
tests/libslic3r/test_draw_session.cpp
tests/libslic3r/test_draw_path_gcode.cpp
```

### Existing Files to Modify

```
src/libslic3r/Model.hpp
  + std::unique_ptr<DrawSession> draw_session  on ModelObject
  + deep-copy in ModelObject::clone()

src/libslic3r/PrintConfig.hpp
  + ConfigOptionBool draw_path_object  in ModelConfig options

src/libslic3r/CMakeLists.txt
  + DrawSession.cpp, DrawPathGCodeGenerator.cpp to libslic3r target

src/libslic3r/Format/bbs_3mf.hpp
  + declare _add_draw_session_to_archive()
  + declare _extract_draw_session_from_archive()

src/libslic3r/Format/bbs_3mf.cpp
  + _add_draw_session_to_archive() per object (Metadata/draw_session_obj_N.xml)
  + _extract_draw_session_from_archive()
  + per-object serialization on export/import

src/libslic3r/GCode.cpp  (or BackgroundSlicingProcess.cpp)
  + draw_path_object detection in process loop
  + DrawPathGCodeGenerator dispatch per instance
  + skip normal slicing for draw path objects

src/slic3r/GUI/GLCanvas3D.hpp
  + CanvasDrawMode value in EType enum

src/slic3r/GUI/GLCanvas3D.cpp
  + mouse routing branch for CanvasDrawMode

src/slic3r/GUI/MainFrame.hpp
  + DrawModePanel* m_draw_mode_panel member
  + switch_to_draw_mode() method

src/slic3r/GUI/MainFrame.cpp
  + "Draw" tab button in mode toolbar
  + DrawModePanel construction and wiring
  + switch_to_draw_mode() implementation

src/slic3r/GUI/Plater.hpp
  + edit_draw_path_object(int obj_idx) declaration

src/slic3r/GUI/Plater.cpp
  + edit_draw_path_object() implementation
  + mixed-plate blocking in load_files() and paste_from_clipboard()
  + right-click context menu: "Edit Drawing" for draw path objects

src/slic3r/GUI/GUI_Preview.cpp
  + "Edit Drawing" toolbar button (visible only when plate has draw path object)
  + calls Plater::edit_draw_path_object()

src/slic3r/GUI/CMakeLists.txt
  + DrawModePanel.cpp, DrawModeInputHandler.cpp, DrawModeCommands.cpp

tests/libslic3r/CMakeLists.txt
  + register test_draw_session.cpp and test_draw_path_gcode.cpp
```

---

## Phased Implementation Roadmap

### Phase 1 — MVP (implement in this order)

Each step is independently testable before the next begins:

1. **`DrawSession` data model** — `DrawSession.hpp/.cpp`, no GUI dependencies. Unit tests: create layers, add segments, serialize/deserialize, bounding box.
2. **`DrawPathGCodeGenerator`** — depends only on `DrawSession` + `GCodeWriter`. Headless test: generate G-code for a known session, compare output string.
3. **`DrawModeSerializer`** — extend `bbs_3mf.cpp` for per-object XML. Round-trip test: save session → load → compare segment-for-segment.
4. **`DrawModeCommands`** — pure data manipulation, unit testable standalone.
5. **`DrawModeInputHandler`** — ray-to-plane math + hit testing. Unit-testable if camera matrices are injectable.
6. **`DrawModePanel` (basic)** — wxWidgets panel, canvas, layer controls, profile banner, undo stack.
7. **Mode switcher wiring** — connect `MainFrame` Draw tab to `DrawModePanel`.
8. **`DrawPathObject` (ModelObject adapter)** — config flag + `draw_session` member + synthetic mesh + `on_finalize()`.
9. **Copy/paste and Prepare screen** — verify drag, Ctrl+C/V, ObjectList appearance (should work automatically from step 8).
10. **G-code generation dispatch** — draw object detection, `DrawPathGCodeGenerator` per instance, sequential output.
11. **Sequential print config** — auto-set on finalize, notification UI, one-design-per-plate enforcement.
12. **Simulate button** — wire generator output through `GCodeProcessor` → `GCodeViewer`.
13. **Export/Print wiring** — reuse existing file export and send-to-printer dialogs.
14. **Editing UI** — Edit mode toggle, segment selection, endpoint dragging.
15. **Round-trip "Edit Drawing"** — `load_for_edit()`, re-finalize update path, entry points in Prepare + Preview.
16. **"Preview Path" toggle** — filled-quad rendering for nozzle footprint visualization.

### Phase 2 — Variable Layer Heights

- Add per-layer height UI to `DrawModePanel`
- `DrawLayer` already stores its own height — just unlock the UI
- Update serializer (bump XML version to 2)

### Phase 3 — Quality of Life

- Snap-to-grid option
- Segment length tooltip while drawing
- Import/export session as standalone JSON
- Overlay actual path lines in Prepare screen rendering (instead of just the bounding box)

---

## Open Questions for Implementer

1. **Camera locking**: `GLCanvas3D` must lock the camera to orthographic top-down when `CanvasDrawMode` is active. Verify the `Camera` class in `src/slic3r/GUI/Camera.hpp` supports forced orthographic mode.

2. **Empty plate origin**: When no model is loaded, `PartPlate::get_origin()` should still return a valid position. Verify behaviour on an empty plate.

3. **`GCodeProcessor::process_string()`**: Verify this method exists and accepts a `std::string` directly (vs. a file path). If only file-path input exists, write to a temp file first.

4. **`ModelObject` config flag vs dedicated member sync**: The `draw_path_object` bool lives in `config` (for .3mf compatibility) but `draw_session` is a dedicated member. These must stay in sync — if `config("draw_path_object") == false`, `draw_session` must be null.

5. **Rotation gizmo disabled for draw objects**: The UI should disable the rotation gizmo for draw path objects in the Prepare screen. Check `GLGizmosManager` for where per-object gizmo availability is determined.

6. **Clearance validation**: `update_sequential_clearance()` in `GLCanvas3D.cpp` uses the object's bounding polygon for `ByObject` mode. Verify it runs correctly for draw objects with the synthetic bbox mesh.

7. **`draw_session` copy semantics in `ModelObject::clone()`**: Ensure deep-copy is added (not shared_ptr sharing) so plate duplication doesn't produce aliased sessions.

8. **Re-finalize with scaled instances**: If bounding box changes on re-edit, instance offsets are preserved but may cause overlap. The existing sequential-print clearance validation will catch this and warn the user.

9. **Cross-layer endpoint editing**: Phase 1 restricts editing to the active layer only. Segments on other layers are visible as ghosts but not selectable. Confirm this scope for Phase 1.

10. **Undo across layer boundaries**: Recommended: `AddLayerCommand` undoes the entire layer atomically (removes the layer and all its segments in one step). Confirm this is the intended behaviour.

11. **Keyboard shortcut conflicts**: Ctrl+Z and Ctrl+Y are used by OrcaSlicer's global undo. When Draw Mode is active, `DrawModePanel` must intercept these via `wxEVT_CHAR_HOOK` and call `evt.Skip(false)` before they reach the global handler.
