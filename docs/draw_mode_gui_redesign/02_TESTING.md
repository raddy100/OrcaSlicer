# Draw Mode GUI Redesign — Test Plan

**Audience:** Testing agent (`orca-draw-mode-tester` / `task`)
**Targets under test:** `DrawModePanel` (regrouped toolbar) + new `DrawLayerSlider` widget.
**Companion doc:** `01_IMPLEMENTATION.md` (design), `03_COORDINATION.md` (workflow).

---

## 1. Test strategy

The redesign is split into two testable surfaces:

1. **`DrawLayerSlider` — logic is unit-testable.** The pixel↔layer mapping (`y_to_layer`,
   `layer_to_y`) and active-index clamping are pure functions with no GUI dependency once extracted.
   These are the highest-value automated tests and the primary deliverable for this agent.
2. **`DrawModePanel` toolbar + wiring — verified by build + manual/scripted UI smoke.** Layout,
   wrapping, and live canvas updates require a running GUI; cover them with a structured manual
   checklist and a build gate.

> **Key constraint (from repo memory & conventions):** `libslic3r_tests` does **not** link the GUI
> library. Therefore widget code in `src/slic3r/GUI/*` cannot be unit-tested directly from
> `libslic3r_tests`. Two acceptable options — the testing agent must pick one and state which:
>
> - **Option A (preferred): extract pure mapping helpers** into a header-only, GUI-free form (free
>   functions or a tiny struct in `DrawLayerSlider.hpp` guarded so it pulls in no wx headers, e.g.
>   `layer_slider_y_to_index(...)` taking ints/doubles only) and unit-test those from
>   `libslic3r_tests`. This mirrors the existing pattern where testable Draw Mode logic is kept
>   GUI-independent (e.g. header-only `DrawModeCommands` are testable, GUI `.cpp` is not).
> - **Option B: manual-only.** If extraction is rejected, the mapping is validated only via the
>   manual UI checklist (§4). Lower confidence; use only if Option A is infeasible.

The testing agent should request Option A in the implementation (coordinate via the orchestrator) so
the math is regression-protected.

---

## 2. Build gate (must pass before any further testing)

```
cmake --build build --config RelWithDebInfo --target libslic3r_gui -- /m
cmake --build build --config RelWithDebInfo --target OrcaSlicer_app_gui -- /m
```

If Option A unit tests are added:

```
cmake --build build --config RelWithDebInfo --target libslic3r libslic3r_tests -- /m
build\tests\libslic3r\RelWithDebInfo\libslic3r_tests.exe "[DrawLayerSlider]"
```

Fail the whole test run if any of these fail; report compiler/linker output verbatim.

---

## 3. Automated unit tests — `DrawLayerSlider` mapping (Option A)

**File:** `tests/libslic3r/test_draw_layer_slider.cpp`, tag `[DrawLayerSlider]`, registered in
`tests/libslic3r/CMakeLists.txt`. Catch2 v3. Follow repo Catch2 rules: `DYNAMIC_SECTION` inside
loops, separate `REQUIRE`s, `WithinAbs`/`WithinRel` for floats, no asserts off the main thread.

Assume the extracted pure helper signature (agree exact names with the coding agent):

```cpp
// Pure, GUI-free. Track top margin T, bottom margin B, widget height H_px, n layers.
// Layer 0 maps to the BOTTOM of the track; layer n-1 to the TOP.
int  layer_slider_y_to_index(int y, int height_px, int top, int bottom, int n);
int  layer_slider_index_to_y(int index, int height_px, int top, int bottom, int n);
```

### 3.1 `y_to_index` — boundary & clamping

| # | Scenario | Input (h=200, T=10, B=10, n=10) | Expected index |
|---|----------|----------------------------------|----------------|
| 1 | Click at very top of track     | y = 10            | 9  (top = last layer) |
| 2 | Click at very bottom of track  | y = 190           | 0  (bottom = layer 0) |
| 3 | Click above track (overscroll) | y = -50           | 9  (clamped) |
| 4 | Click below track (overscroll) | y = 999           | 0  (clamped) |
| 5 | Click at exact middle          | y = 100           | 4 or 5 (document rounding; assert it is 4 **or** 5 consistently) |

### 3.2 `n == 1` degenerate case

- For any `y`, `layer_slider_y_to_index(y, ...) == 0`. Test 3+ y values (top, middle, bottom).
- `layer_slider_index_to_y(0, ...)` returns a y within `[top, height-bottom]` (no div-by-zero).

### 3.3 `n == 0` empty case

- `layer_slider_y_to_index(any, ..., n=0)` returns `-1` (no active layer). Verify no crash / no
  division by zero.

### 3.4 Round-trip stability

- For `n` in `{2, 5, 10, 50}` and every `index` in `[0, n-1]` (use `DYNAMIC_SECTION("n=" << n << " i=" << i)`):
  `layer_slider_y_to_index(layer_slider_index_to_y(index, ...), ...) == index`.
  This guarantees a click on a tick selects that exact layer (teleport correctness).

### 3.5 Monotonicity (scrub correctness)

- Sweeping `y` from `height-bottom` up to `top` yields a **non-decreasing** sequence of indices that
  starts at 0 and ends at `n-1` and covers every index at least once (for reasonable `n` ≤ height).
  Confirms drag-scrub passes through every layer with no gaps or reversals.

---

## 4. Manual / scripted UI smoke checklist (`DrawModePanel`)

Run the built `orca-slicer.exe`, open the **Draw** tab. Record PASS/FAIL + a screenshot per section.

### 4.1 Toolbar grouping & responsiveness (the core complaint)

- [ ] **T1** Controls appear in labeled groups: Mode, Tools, Geometry, Quality, View, Layers, Session.
- [ ] **T2** At full-screen width all groups fit on one (or few) rows, visually separated.
- [ ] **T3** Resize the OrcaSlicer window to **half screen width**. Toolbar groups **wrap** onto
      additional rows. **No control is clipped or cut off** (this is the primary regression the
      redesign fixes — fail hard if anything is clipped).
- [ ] **T4** Shrink further to a narrow width; groups continue to wrap gracefully; canvas + slider
      remain usable. No horizontal scrollbar / no overlap.
- [ ] **T5** Every control from the old toolbar is still present and reachable (inventory check
      against `01_IMPLEMENTATION.md` §3.2 group table). Intentionally changed: `< Prev` / `Next >` /
      `Layer: N` label removed (replaced by the slider); the single "1st flow %" spin replaced by the
      Quality-group initial-layers editor (§4.8). Nothing else dropped.

### 4.2 Layer slider — presence & sync

- [ ] **S1** A vertical slider sits beside the canvas. With 0 layers it is empty/disabled.
- [ ] **S2** Click `+ Layer` several times (e.g. 8 layers). Slider shows 8 ticks; thumb at the
      newly-active (top) layer; z-height label matches the active layer's height.
- [ ] **S3** Active-layer highlight matches the canvas content (draw a distinct shape on a couple of
      layers to confirm the canvas shows the layer the slider points to).

### 4.3 Layer slider — drag-to-scrub

- [ ] **S4** Press and drag the thumb downward. Active layer decreases continuously; the canvas
      updates **live** during the drag (passes through each intermediate layer). Release commits.
- [ ] **S5** Drag upward; active layer increases continuously back to the top. No skipped layers,
      no reversal jitter.
- [ ] **S6** Drag past the top/bottom ends; active clamps to last/first layer (no crash, no wrap).

### 4.4 Layer slider — click-to-teleport

- [ ] **S7** With the thumb at the top layer, single-click near the **bottom** of the track. Active
      layer jumps directly to layer 0 in one action (no stepping animation through middle layers).
      Canvas shows layer 0.
- [ ] **S8** Click at ~middle of the track; active jumps to the middle layer; canvas matches.
- [ ] **S9** Click exactly on a specific tick; that exact layer becomes active (teleport precision —
      ties back to unit test §3.4 round-trip).

### 4.4b Layer slider — keyboard & wheel (LOCKED behavior, required)

- [ ] **S9a** Click the slider to focus it, then press **Up** / **Down** arrows → active layer
      steps +1 / −1; canvas updates; clamps at top/bottom (no wrap).
- [ ] **S9b** Press **PageUp** / **PageDown** → active jumps +5 / −5 (clamped).
- [ ] **S9c** Press **Home** / **End** → active jumps to the top layer / layer 0.
- [ ] **S9d** Scroll the **mouse-wheel** over the slider → wheel-up steps +1, wheel-down steps −1
      (clamped). Canvas updates each step.

### 4.5 Slider ↔ panel state synchronization (regression-critical)

For each action, confirm the slider tick count and active highlight update correctly:

- [ ] **S10** `+ Layer` / `Insert Below` → tick count increases, active follows the new layer.
- [ ] **S11** `Delete Layer` / `- Layer` (remove) → tick count decreases, active adjusts per
      `DrawSession::remove_layer` semantics.
- [ ] **S12** `Clear Layer` → tick count unchanged, active unchanged (only segments cleared).
- [ ] **S13** `Copy Layer` then `Paste Layer` → no spurious tick changes; active unchanged.
- [ ] **S14** `Mirror Stack` → tick count unchanged; active layer index remains valid; slider stays
      consistent.
- [ ] **S15** **Undo (Ctrl+Z) / Redo (Ctrl+Y)** of layer add/delete → slider tick count and active
      highlight track the undone/redone state exactly.

### 4.6 No-regression sweep (existing functionality)

- [ ] **R1** Draw tool: Line / Arc / Curve each draw correctly; Snap, Grid res, Arc res still work.
- [ ] **R2** Splice: toggle Splice, click a segment to cut it, right-click exits — unchanged.
- [ ] **R3** Edit mode: select & drag endpoints / control handles works.
- [ ] **R4** Quality controls: Wipe + Dist, Coast spinners change values and persist (initial-layers
      editor is covered separately in §4.8).
- [ ] **R5** View toggles: Show Measurements / Show Coordinates toggle overlays.
- [ ] **R6** Length input + Snip behave as before.
- [ ] **R7** Simulate produces a model/preview; Finalize commits the draw object. Re-open
      (`load_for_edit`) restores layers and the slider repopulates correctly.
- [ ] **R8** Keyboard shortcuts (Ctrl+Z/Y, any layer up/down keys) still work and stay in sync with
      the slider.

### 4.8 Initial-layers editor (Quality group — kept per user decision)

The single "1st flow %" control was replaced by a multi-initial-layer editor that drives the
pre-existing `DrawSession::initial_layer_flow_ratios` / `initial_layer_heights` data model.

- [ ] **Q1** Quality group shows `Init:` + a count spin (default **1**) and exactly **one** visible
      initial-layer row (`L0 flow%:` default 90, `h(mm):` default = profile layer height).
- [ ] **Q2** Increase `Init:` to e.g. 4 → exactly 4 rows become visible (`L0..L3`); rows are
      shown/hidden, not re-laid-out (no flicker/jump). Set to 0 → no rows. Set to 8 (max) → 8 rows.
- [ ] **Q3** Editing a per-layer flow % or height spin updates `m_session.initial_layer_flow_ratios`
      / `initial_layer_heights` and reflows layer Z (the slider's z-height labels update accordingly).
- [ ] **Q4** Defaults are correct: layer 0 flow = 90 % (elephant's-foot), other layers = 100 %;
      heights default to the active profile layer height.
- [ ] **Q5** Finalize then re-open (`load_for_edit`): the editor repopulates N, per-layer flows and
      heights from the saved session (round-trips through `bbs_3mf`).
- [ ] **Q6** `activate()` on a fresh plate resets to N=1, layer-0 flow 90 %, no height overrides.

> The data-model/serialization/G-code for this feature were **pre-existing WIP** and have their own
> `tests/libslic3r` coverage (run via §5 tags). Only the GUI wiring is new and is validated here.

### 4.7 Cross-platform note

Primary verification on Windows (this environment). Flag any use of non-portable wx APIs found
during review for macOS/Linux follow-up. `wxWrapSizer`, `wxStaticBoxSizer`, `wxBufferedPaintDC`,
`wxStaticLine` are all portable — confirm the implementation used only these.

---

## 5. Existing-test regression gate

Run the existing Draw Mode suites to prove the GUI refactor didn't disturb core logic (these don't
touch the GUI but guard the shared `DrawSession`/command layer):

```
build\tests\libslic3r\RelWithDebInfo\libslic3r_tests.exe "[DrawSession],[ConnectedNodeDrag],[DrawLayerCopyPaste],[Draw3mf],[MirrorStack],[DrawModeFeedback],[DeleteLayerZShift],[Splice]"
```

All must pass unchanged. Any new failure here means the coding agent touched shared logic it should
not have (the redesign is GUI-only) — report and bounce back to the coding agent.

---

## 6. Reporting format

For each run, the testing agent reports:

1. **Build:** PASS/FAIL for `libslic3r_gui`, `OrcaSlicer_app_gui`, (and `libslic3r_tests` if Option A).
2. **Unit tests:** Catch2 summary line (`N assertions in M test cases`) for `[DrawLayerSlider]`.
3. **Regression suite:** PASS/FAIL summary for the §5 tag list.
4. **Manual checklist:** the §4 table with PASS/FAIL per item + screenshots for T3 (half-screen
   wrap), S4/S5 (scrub), S7 (teleport).
5. **Defects:** for each FAIL — exact step, expected vs actual, screenshot, and suspected file/area.

---

## 7. Exit criteria

- All build gates PASS.
- All `[DrawLayerSlider]` unit tests PASS (Option A) — or documented justification for Option B.
- §5 existing regression suite PASS with no new failures.
- All §4 checklist items PASS, in particular **T3** (no clipping at half-screen), **S4/S5** (scrub),
  **S7–S9** (teleport), **S9a–S9d** (keyboard/wheel), **S10–S15** (state sync), **Q1–Q6**
  (initial-layers editor).
- Any FAIL is either fixed (loop back to coding agent via orchestrator) or explicitly accepted by the
  user with rationale.
