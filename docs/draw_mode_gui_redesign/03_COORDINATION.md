# Draw Mode GUI Redesign — Orchestration & Coordination Plan

**Audience:** Orchestration agent (`feature-implementation-orchestrator`)
**Coordinates:** a **coding agent** (`quality-code-developer`) and a **testing agent**
(`orca-draw-mode-tester`).
**Inputs:** `01_IMPLEMENTATION.md` (design), `02_TESTING.md` (test plan).
**Goal:** Ship the regrouped, responsive Draw Mode toolbar + interactive layer slider with zero
regressions, verified by tests, ready for the user to review.

---

## 1. Roles

| Agent | Owns | Must NOT |
|-------|------|----------|
| **Orchestrator** (you) | Sequencing, hand-offs, gate decisions, escalation to user, final sign-off | Write production code or tests directly (delegate) |
| **Coding agent** | All code in `01_IMPLEMENTATION.md` §3.5; build green | Touch `DrawSession`/G-code/serialization; write the test plan |
| **Testing agent** | Everything in `02_TESTING.md`; defect reports | Fix production code (reports back instead) |

Single source of truth for status: the session SQL `todos` table (schema below). Update it at every
hand-off so progress is recoverable.

---

## 2. Work breakdown & dependencies

```
P0  read-design        (orchestrator) ── seed todos, confirm scope with user if ambiguous
        │
P1  slider-widget      (coding)  new DrawLayerSlider.{hpp,cpp} + CMake + extract pure mapping helper
        │
P2  slider-unit-tests  (testing) [DrawLayerSlider] tests on the pure helper   ◄── depends P1
        │
P3  slider-wiring       (coding)  m_layer_slider + content_sizer + refresh_layer_slider  ◄── P1
        │
P4  toolbar-regroup     (coding)  wxWrapSizer groups; remove prev/next/label  ◄── P3
        │
P5  full-build          (coding)  OrcaSlicer_app_gui clean                     ◄── P4
        │
P6  ui-smoke + regress  (testing) §4 checklist + §5 regression suite           ◄── P5, P2
        │
P7  triage/fix-loop     (orchestrator drives coding↔testing until green)
        │
P8  sign-off            (orchestrator → user)
```

Critical path: P1 → P3 → P4 → P5 → P6. P2 can run in parallel with P3/P4 once P1's pure helper
exists. **Insist the coding agent land the extracted pure mapping helper (Option A in `02_TESTING.md`
§1) in P1** so P2 can be automated — this is the single most important coordination decision.

---

## 3. Todo seed (run once at P0)

```sql
INSERT INTO todos (id, title, description, status) VALUES
 ('read-design',       'Reviewing design + test docs',          'Read 01_IMPLEMENTATION.md and 02_TESTING.md; confirm scope; seed todos.', 'pending'),
 ('slider-widget',     'Creating DrawLayerSlider widget',       'New src/slic3r/GUI/DrawLayerSlider.{hpp,cpp}; register in CMakeLists.txt:114-115 area; extract pure GUI-free mapping helpers (layer_slider_y_to_index / index_to_y). Build libslic3r_gui.', 'pending'),
 ('slider-unit-tests', 'Writing [DrawLayerSlider] unit tests',  'tests/libslic3r/test_draw_layer_slider.cpp, tag [DrawLayerSlider], per 02_TESTING.md §3. Register in tests CMakeLists.', 'pending'),
 ('slider-wiring',     'Wiring slider into DrawModePanel',      'Add m_layer_slider + content_sizer; on_slider_layer_change; refresh_layer_slider() at end of update_layer_label().', 'pending'),
 ('toolbar-regroup',   'Regrouping toolbar with wxWrapSizer',   'Replace top_sizer+nav_sizer (DrawModePanel.cpp:206-256) with make_tool_group + wxWrapSizer; 7 groups; remove prev/next/layer-label from layout.', 'pending'),
 ('full-build',        'Building full GUI app',                 'cmake --build build --config RelWithDebInfo --target OrcaSlicer_app_gui -- /m. Must be clean.', 'pending'),
 ('ui-smoke',          'Running UI smoke + regression',         'Execute 02_TESTING.md §4 checklist and §5 regression tag suite; capture screenshots T3/S4/S5/S7.', 'pending'),
 ('sign-off',          'Final review and sign-off',             'Verify acceptance criteria; present to user.', 'pending');

INSERT INTO todo_deps (todo_id, depends_on) VALUES
 ('slider-widget','read-design'),
 ('slider-unit-tests','slider-widget'),
 ('slider-wiring','slider-widget'),
 ('toolbar-regroup','slider-wiring'),
 ('full-build','toolbar-regroup'),
 ('ui-smoke','full-build'),
 ('ui-smoke','slider-unit-tests'),
 ('sign-off','ui-smoke');
```

"Ready" query (what to dispatch next):

```sql
SELECT t.* FROM todos t
WHERE t.status='pending'
  AND NOT EXISTS (SELECT 1 FROM todo_deps d JOIN todos dep ON d.depends_on=dep.id
                  WHERE d.todo_id=t.id AND dep.status!='done');
```

---

## 4. Hand-off gates

Each gate must pass before the dependent phase starts. The orchestrator owns the go/no-go.

| Gate | After | Pass condition | On fail |
|------|-------|----------------|---------|
| **G1 widget builds** | P1 | `libslic3r_gui` compiles; pure helper exists & is GUI-free | bounce to coding agent |
| **G2 unit green** | P2 | `[DrawLayerSlider]` all pass; covers boundary/clamp/round-trip/monotonic (§3.1–3.5) | bounce to coding (logic) or testing (test bug) |
| **G3 wiring works** | P3 | slider scrubs+teleports in a dev build; `refresh_layer_slider` synced via `update_layer_label` | bounce to coding |
| **G4 toolbar wraps** | P4 | half-screen wrap, no clipping (`02_TESTING.md` T3); all old controls present (T5) | bounce to coding |
| **G5 full build** | P5 | `OrcaSlicer_app_gui` clean | bounce to coding |
| **G6 verified** | P6 | §4 checklist + §5 regression all PASS | open defects → P7 |

---

## 5. Dispatch prompts (templates)

### 5.1 To coding agent (P1 example)

> Implement Phase **slider-widget** from `docs/draw_mode_gui_redesign/01_IMPLEMENTATION.md` §3.3 &
> §3.5. Create `src/slic3r/GUI/DrawLayerSlider.{hpp,cpp}` (native wxWidgets, NOT ImGui/IMSlider),
> register both in `src/slic3r/CMakeLists.txt` beside the `DrawModePanel` entries (lines 114-115).
> **Required:** also expose pure, GUI-free mapping helpers
> `layer_slider_y_to_index(y,height,top,bottom,n)` and `layer_slider_index_to_y(index,height,top,bottom,n)`
> usable from `libslic3r_tests` (no wx headers pulled in) — see `02_TESTING.md` §1 Option A & §3.
> Follow repo style (C++17, 4-space, 140-col, snake_case members, `#pragma once`). Build
> `libslic3r_gui` clean. Report the exact helper signatures you landed so the testing agent matches.

### 5.2 To testing agent (P2 example)

> Implement Phase **slider-unit-tests** per `docs/draw_mode_gui_redesign/02_TESTING.md` §3. Create
> `tests/libslic3r/test_draw_layer_slider.cpp` (Catch2 v3, tag `[DrawLayerSlider]`), register in
> `tests/libslic3r/CMakeLists.txt`. Use the exact helper signatures the coding agent reported:
> `<paste signatures>`. Cover §3.1 boundary/clamp, §3.2 n==1, §3.3 n==0, §3.4 round-trip, §3.5
> monotonic. Build+run:
> `cmake --build build --config RelWithDebInfo --target libslic3r libslic3r_tests -- /m` then
> `build\tests\libslic3r\RelWithDebInfo\libslic3r_tests.exe "[DrawLayerSlider]"`. Report the Catch2
> summary and any failures with input→expected→actual.

### 5.3 To testing agent (P6 example)

> Execute `docs/draw_mode_gui_redesign/02_TESTING.md` §4 (manual UI smoke) and §5 (regression tag
> suite) against `build\src\RelWithDebInfo\orca-slicer.exe`. Prioritize **T3** (half-screen no
> clipping), **S4/S5** (drag-scrub live update), **S7-S9** (click-teleport), **S10-S15** (slider/
> panel sync incl. undo/redo). Capture screenshots for T3/S4/S5/S7. Run the §5 regression command and
> confirm all existing Draw Mode tags still pass. Report per `02_TESTING.md` §6.

---

## 6. Defect / fix loop (P7)

1. Testing agent files each defect: step, expected, actual, screenshot, suspected file.
2. Orchestrator records each as a `todos` row `fix-<n>` (status `pending`), dependency on nothing,
   and dispatches to the **coding agent** with the defect detail.
3. Coding agent fixes + rebuilds; orchestrator re-dispatches the **specific** failed checklist items
   to the testing agent (not the whole suite) for fast turnaround, then a final full §5 regression
   pass before closing.
4. Repeat until G6 passes. Cap at 3 loops, then escalate to the user with the remaining defects.

Classification rule: a failure in `02_TESTING.md` §5 (existing tags) means shared logic was touched
— **high severity**, since the redesign is supposed to be GUI-only. Always bounce these to coding
with a reminder of constraint `01_IMPLEMENTATION.md` §4.1 (no data-model changes).

---

## 7. Escalate to the user when

- The coding agent rejects Option A (pure-helper extraction), forcing manual-only slider testing.
- Any acceptance criterion in `01_IMPLEMENTATION.md` §7 cannot be met.
- Fix loop exceeds 3 iterations on the same defect.

**Resolved design decisions (locked by user — do NOT re-litigate):**
- Layer slider sits on the **right** of the canvas (matches Preview's `IMSlider`).
- The slider thumb shows **`Layer: N` + z-height**; the standalone `Layer: N` label and the
  `< Prev` / `Next >` buttons are removed.
- **Control labels/tooltips** are fixed by the locked table in `01_IMPLEMENTATION.md` §3.2; group
  titles are exactly Mode/Tools/Geometry/Quality/View/Layers/Session.
- **Slider colours** are the locked hard-coded palette in `01_IMPLEMENTATION.md` §3.3 (match canvas;
  no theme API).
- **Keyboard + wheel navigation** on the slider is **required** (Up/Down ±1, PageUp/Down ±5,
  Home/End, wheel ±1) — see §3.3 and `02_TESTING.md` §4.4b.
- **Quality group hosts the multi-initial-layer editor** (`m_initial_layer_count_spin` + per-layer
  flow/height rows), kept by user decision during implementation. It supersedes the former single
  `m_first_layer_flow_spin` and drives the pre-existing `DrawSession::initial_layer_flow_ratios` /
  `initial_layer_heights` WIP data model. Docs (§3.2/§3.3 of `01`, §4.8 of `02`) updated to match.

Use the `ask_user` tool with concrete choices; do not block silently.

---

## 8. Final sign-off checklist (P8)

Mirror of `01_IMPLEMENTATION.md` §7 — all must be ✅ before declaring done:

- [ ] `libslic3r_gui` + `OrcaSlicer_app_gui` build clean (RelWithDebInfo).
- [ ] `[DrawLayerSlider]` unit tests pass (or accepted Option B rationale).
- [ ] `02_TESTING.md` §5 existing regression suite passes — no new failures.
- [ ] Toolbar grouped (Mode/Tools/Geometry/Quality/View/Layers/Session) with visible separation.
- [ ] Half-screen width: groups wrap, nothing clipped (T3).
- [ ] Vertical layer slider present; one tick/layer; active highlighted with z-height.
- [ ] Drag scrubs continuously (S4/S5); click teleports (S7-S9); stays synced (S10-S15).
- [ ] No regression in draw/edit/splice/simulate/finalize/undo-redo/keyboard.
- [ ] Changes confined to files in `01_IMPLEMENTATION.md` §3.5.

On all-green: summarize what changed, attach the T3/S4/S7 screenshots, mark `sign-off` done, and
hand back to the user for review (optionally invoke the `commit-push-pr` skill if the user wants a PR).

---

## 9. Quick status board (keep updated)

| Phase | Owner | Status | Gate |
|-------|-------|--------|------|
| P0 read-design       | orch    | ⬜ | — |
| P1 slider-widget     | coding  | ⬜ | G1 |
| P2 slider-unit-tests | testing | ⬜ | G2 |
| P3 slider-wiring     | coding  | ⬜ | G3 |
| P4 toolbar-regroup   | coding  | ⬜ | G4 |
| P5 full-build        | coding  | ⬜ | G5 |
| P6 ui-smoke+regress  | testing | ⬜ | G6 |
| P7 fix-loop          | orch    | ⬜ | — |
| P8 sign-off          | orch    | ⬜ | — |
