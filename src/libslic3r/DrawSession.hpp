#pragma once

#include <vector>
#include "Point.hpp"
#include "BoundingBox.hpp"

namespace Slic3r {

// Semantic geometry type of a draw segment.
// Default is Line for backward compatibility with existing serialised sessions.
enum class DrawSegmentType {
    Line,          // straight line from start to end
    CircularArc,   // arc through ctrl1 (the "through" point) from start to end
    CubicBezier,   // cubic Bezier with ctrl1 (first CP) and ctrl2 (second CP)
};

// A single move on the build plate.
// Coordinates are plate-relative (mm), relative to PartPlate::get_origin().
// Backward-compatible aggregate: existing code that writes
//   { Vec2d(…), Vec2d(…), false }
// continues to compile — type/ctrl1/ctrl2 take their defaults.
struct DrawSegment {
    Vec2d           start;                               // XY, plate-relative (mm)
    Vec2d           end;                                 // XY, plate-relative (mm)
    bool            is_travel  = false;                  // true = non-extruding travel
    DrawSegmentType type       = DrawSegmentType::Line;  // geometry kind
    Vec2d           ctrl1      = Vec2d::Zero();          // arc: through-point; bezier: CP1
    Vec2d           ctrl2      = Vec2d::Zero();          // bezier: CP2; unused otherwise

    // Chord length (start → end).  Accurate for Line; for Arc/Bezier use
    // draw_segment_sampled_length() from DrawModeFeedback.hpp instead.
    double length() const { return (end - start).norm(); }

    // Factory helpers — preferred for creating non-line segments.
    static DrawSegment make_line(Vec2d start, Vec2d end, bool is_travel = false)
    {
        DrawSegment s;
        s.start     = start;
        s.end       = end;
        s.is_travel = is_travel;
        s.type      = DrawSegmentType::Line;
        return s;
    }

    // The arc travels from start, through the point `through`, to end.
    // `through` is stored in ctrl1.
    static DrawSegment make_arc(Vec2d start, Vec2d through, Vec2d end, bool is_travel = false)
    {
        DrawSegment s;
        s.start     = start;
        s.end       = end;
        s.is_travel = is_travel;
        s.type      = DrawSegmentType::CircularArc;
        s.ctrl1     = through;
        return s;
    }

    static DrawSegment make_bezier(Vec2d start, Vec2d ctrl1, Vec2d ctrl2, Vec2d end, bool is_travel = false)
    {
        DrawSegment s;
        s.start     = start;
        s.end       = end;
        s.is_travel = is_travel;
        s.type      = DrawSegmentType::CubicBezier;
        s.ctrl1     = ctrl1;
        s.ctrl2     = ctrl2;
        return s;
    }
};

// One horizontal layer of drawn segments. Z values are baked in at creation
// time from the active process profile; geometry is preserved if the profile
// changes later.
struct DrawLayer {
    int                      layer_index; // 0-based
    double                   z_start;     // mm — Z at first segment start
    double                   z_end;       // mm — Z at last segment end
    std::vector<DrawSegment> segments;    // in draw order

    double layer_height() const { return z_end - z_start; }
};

// Pure-data session object. No GUI dependencies; must be serializable and
// work on all platforms. All print parameters (temperature, speed, …) are
// intentionally NOT stored here — they come from DynamicPrintConfig at
// G-code generation time.
struct DrawSession {
    std::vector<DrawLayer> layers;       // in Z order
    int                    active_layer; // index into layers[], -1 if none

    // Max chord-to-curve deviation (mm) when sampling arc/bezier segments into
    // G1 polylines for G-code output, mesh generation, and canvas rendering.
    // Smaller = more segments = smoother; default 0.05 matches
    // DRAW_MODE_SAMPLE_TOLERANCE_MM in DrawModeFeedback.hpp.
    double curve_tolerance_mm = 0.05;

    // When true, circular arc segments emit native G2/G3 commands instead of
    // G1 linearization.  Bezier curves always use G1 regardless of this flag.
    bool native_arc_output = false;

    // Elephant's-foot mitigation for Draw Mode. Scales the extrusion amount (E)
    // of the first layer (layer_index == 0) only; the XY toolpath is never moved,
    // so drawn lengths/positions stay accurate. The slightly starved bottom bead
    // has less material to bulge sideways. 1.0 = no reduction; default 0.90 (90%).
    double first_layer_flow_ratio = 0.90;

    // Anti-blob wipe: before each retract, move the nozzle backward along the
    // just-printed segment (no extrusion) so the end-of-path pressure bleeds out
    // over the toolpath instead of forming a blob at the seam point. The total
    // retraction length is unchanged; only WHERE retraction happens shifts.
    bool   wipe_enabled = true;       // wipe backward along last segment while retracting (anti-blob)
    double wipe_distance_mm = 1.0;    // length of the wipe move (clamped to segment length)

    // Optional coasting: stop extruding this many mm before the end of a segment
    // and travel the remainder dry, letting residual nozzle pressure finish the
    // bead. Incompatible with firmware pressure/linear advance — keep at 0 (off)
    // when PA is enabled. 0 = disabled (byte-identical to the no-coast baseline).
    double coast_distance_mm = 0.0;   // stop extruding this many mm early; PA-incompatible; 0 = off

    DrawSession() : active_layer(-1) {}

    bool   is_empty() const;
    // Appends a new layer using layer_height baked from the active profile.
    void   add_layer(double layer_height);
    // Inserts a new empty layer at the given position with the given layer_height.
    // All layers at position and above have their z and layer_index shifted up.
    // Sets active_layer to the inserted layer's index.
    // Throws if position is outside [0, layer_count()] or layer_height <= 0.
    void   insert_layer(int position, double layer_height);
    // Removes the layer at index and adjusts active_layer:
    //   active == index => step to max(0, index-1), or -1 if no layers remain
    //   active >  index => decrement by 1 (layer shifted down)
    //   active <  index => unchanged
    // Returns false if index is out of range.
    bool   remove_layer(int index);
    void   clear();
    double total_height() const;

    // 3-D bounding box across all layers — used to create the synthetic mesh.
    BoundingBoxf3 bounding_box() const;

    int layer_count() const { return static_cast<int>(layers.size()); }
};

} // namespace Slic3r
