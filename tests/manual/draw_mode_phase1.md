# Draw Mode — Phase 1 Manual Smoke Test Checklist

**Purpose**: Verify the end-to-end Draw Mode workflow in a running OrcaSlicer build.  
**Scope**: Phase 1 implementation (TASK-017, TASK-019, TASK-025 user-visible behaviours).  
**Prerequisites**: RelWithDebInfo or Release build; a valid printer profile loaded.

---

## 1. Draw → Finalize → New Object

| # | Step | Expected result | Pass/Fail |
|---|------|-----------------|-----------|
| 1.1 | Click the **Draw Mode** tab (tab 10 in the toolbar). | Draw Mode panel appears; banner shows "Draw Mode". | |
| 1.2 | Click **+ Layer** once. | Layer label reads "Layer 1 of 1 (Z: 0.000 → 0.200 mm)" (or matches the active process profile's first layer height). | |
| 1.3 | Click and drag on the canvas to draw 3 connected line segments. | Blue extrusion lines appear on the canvas for each dragged stroke. | |
| 1.4 | Click **Finalize**. | Panel resets to "No layers"; the 3D editor tab opens; one new object "DrawPathObject" appears in the object list. | |
| 1.5 | Select the new object in the 3D editor. | Object is visible as a ribbon mesh on the build plate. | |

---

## 2. Save & Re-open (3mf Round-trip)

| # | Step | Expected result | Pass/Fail |
|---|------|-----------------|-----------|
| 2.1 | With the draw-path object on the plate, **File → Save Project As…** to a `.3mf` file. | Save completes without error. | |
| 2.2 | **File → New Project**, then **File → Open** the saved `.3mf`. | Project loads; draw-path object is restored to the plate. | |
| 2.3 | Right-click the draw-path object → **Edit Drawing**. | Draw Mode panel opens; the previously drawn segments are restored on the canvas; layer count matches what was saved. | |

---

## 3. Re-edit (TASK-017 — mesh replacement)

| # | Step | Expected result | Pass/Fail |
|---|------|-----------------|-----------|
| 3.1 | With a draw-path object on the plate, right-click it → **Edit Drawing**. | Draw Mode panel opens with existing segments loaded; banner shows "Editing object N". | |
| 3.2 | Draw 2 additional segments on the canvas. | New strokes appear alongside the previously loaded segments. | |
| 3.3 | Click **Finalize**. | The mesh on the plate is replaced (not duplicated); the 3D editor opens; the object's ribbon mesh now reflects the combined segments. | |
| 3.4 | Undo (**Ctrl+Z**) within Draw Mode before finalizing (repeat step 3.1–3.2, then undo). | The last drawn segment disappears; re-doing (**Ctrl+Y**) restores it. | |

---

## 4. Mixed-plate Guard (TASK-019)

| # | Step | Expected result | Pass/Fail |
|---|------|-----------------|-----------|
| 4.1 | Import a normal STL object so it is on plate 1. Switch to Draw Mode, draw a segment, click **Finalize**. | Warning dialog: "A plate cannot mix drawn-path objects with normal 3D models." Object is **not** added to the plate. | |
| 4.2 | Add a second plate, switch to it, then repeat: Draw Mode → draw a segment → Finalize. | Draw-path object is added to plate 2 without a warning. | |
| 4.3 | With only draw-path objects on a plate, add another draw-path object (Draw → Finalize). | Object is added without a warning (draw-path + draw-path is allowed on the same plate). | |

---

## 5. Edit Drawing Context Menu (TASK-025)

| # | Step | Expected result | Pass/Fail |
|---|------|-----------------|-----------|
| 5.1 | Right-click a **normal** (non-draw-path) object in the 3D editor. | Context menu appears; **Edit Drawing** is **not** present. | |
| 5.2 | Right-click a **draw-path** object in the 3D editor. | Context menu appears; **Edit Drawing** entry is present and enabled. | |
| 5.3 | Click **Edit Drawing** from the context menu. | OrcaSlicer switches to the Draw Mode tab; the panel loads the object's session (banner shows "Editing object N"; segments are restored on the canvas). | |
| 5.4 | Click **Cancel** (or navigate away without finalizing). | 3D view is unchanged; no spurious mesh updates. | |

---

## 6. Undo / Redo within Draw Mode

| # | Step | Expected result | Pass/Fail |
|---|------|-----------------|-----------|
| 6.1 | In Draw Mode with at least 2 drawn segments, press **Ctrl+Z**. | The most recent segment is removed from the canvas. | |
| 6.2 | Press **Ctrl+Y** (or Ctrl+Shift+Z). | The removed segment is restored. | |
| 6.3 | Press **Ctrl+Z** repeatedly until all segments are gone. | Canvas is empty; Ctrl+Z has no further effect (no crash). | |

---

## Notes

- All tests should be performed on Windows, macOS and Linux before a release.
- If a test step fails, file a bug with the exact step number, OS, build commit, and a screenshot.
- Automated equivalents of sections 1–2 are in `tests/libslic3r/test_draw_session.cpp` (tags `[DrawSession]`, `[Draw3mf]`) and `tests/libslic3r/test_draw_path_gcode.cpp` (tag `[DrawPathGCodeGenerator]`).
