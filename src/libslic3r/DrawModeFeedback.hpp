#pragma once

#include "DrawSession.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Slic3r {

class ModelObject;

// Draw Mode feedback defaults are intentionally independent from nozzle/grid
// snapping. Grid snapping rounds coordinates; direction snapping constrains the
// line direction after any coordinate snap has been applied.
enum class DrawDirectionSnapMode {
    Cardinal,
    Diagonal45,
    Free,
};

struct DrawModeDisplayPreferences {
    bool show_measurements = true;
    bool show_coordinates  = true;
};

constexpr double DRAW_MODE_MIN_SEGMENT_LENGTH_MM = 1e-6;
constexpr double DRAW_MODE_NUDGE_SMALL_MM        = 0.1;
constexpr double DRAW_MODE_NUDGE_LARGE_MM        = 1.0;
constexpr double DRAW_MODE_DEFAULT_ZOOM_FACTOR   = 1.0;
constexpr double DRAW_MODE_MIN_ZOOM_FACTOR       = 0.5;
constexpr double DRAW_MODE_MAX_ZOOM_FACTOR       = 10.0;
constexpr double DRAW_MODE_SCALE_GRID_MM         = 10.0;
constexpr double DRAW_MODE_WORK_AREA_MM          = 20.0; // fixed canvas size (mm)

// Maximum chord-to-curve deviation used when sampling arc/bezier segments (mm).
constexpr double DRAW_MODE_SAMPLE_TOLERANCE_MM   = 0.05;

double draw_segment_length_mm(const Vec2d& start, const Vec2d& end);
double draw_segment_absolute_angle_degrees(const Vec2d& start, const Vec2d& end);
double draw_segment_relative_angle_degrees(const DrawLayer& layer, int segment_index);
double draw_relative_angle_degrees(const Vec2d& previous_start, const Vec2d& previous_end, const Vec2d& start, const Vec2d& end);

DrawDirectionSnapMode draw_resolve_snap_mode(bool ctrl_down, bool alt_down);
Vec2d draw_project_cardinal(const Vec2d& start, const Vec2d& raw_end);
Vec2d draw_project_diagonal_45(const Vec2d& start, const Vec2d& raw_end);
Vec2d draw_apply_direction_snap(const Vec2d& start, const Vec2d& raw_end, DrawDirectionSnapMode mode);
Vec2d draw_project_typed_length(const Vec2d& start, const Vec2d& direction_end, double length_mm);

std::optional<double> draw_parse_length_mm(const std::string& text);
std::string draw_format_length_mm(double length_mm);
std::string draw_format_angle_degrees(double angle_degrees);
std::string draw_format_coordinate_mm(const Vec2d& pt);

Vec2d draw_nudge_delta_from_key(int key_code, bool shift_down);

// Translates all geometry points in a segment (start, end, and the arc through-
// point / bezier control points) by delta.
DrawSegment draw_translate_segment(const DrawSegment& segment, const Vec2d& delta);

double draw_clamp_zoom_factor(double zoom_factor);
Vec2d draw_clamp_pan_offset(const Vec2d& pan_offset, double plate_width_mm, double plate_height_mm, double zoom_factor);
int draw_scale_bar_length_pixels(double grid_spacing_mm, double plate_width_mm, double zoom_factor, double inner_canvas_width_px);

bool draw_parse_bool_preference(const std::string& value, bool default_value);
std::string draw_format_bool_preference(bool value);

ModelObject* draw_find_first_restorable_draw_object(const std::vector<ModelObject*>& objects, int* object_index = nullptr);

// ---------------------------------------------------------------------------
// Curve sampling
// ---------------------------------------------------------------------------

// Returns the sequence of 2D points representing the segment, suitable for
// polyline rendering, mesh building, hit testing, and G-code generation.
//   Line:         returns {start, end} exactly.
//   CircularArc:  computes circumcircle through start/ctrl1(through)/end and
//                 samples the arc maintaining correct winding order.
//   CubicBezier:  recursively subdivides until chord deviation < tolerance_mm.
// In all cases first == seg.start and last == seg.end exactly.
// Degenerate inputs (collinear for arcs, zero-length for beziers) produce
// {start, end} safely without throwing.
std::vector<Vec2d> draw_sample_segment(
    const DrawSegment& seg,
    double tolerance_mm = DRAW_MODE_SAMPLE_TOLERANCE_MM);

// Approximate path length by summing sampled polyline chord lengths.
// For Line this equals (end-start).norm(); for Arc/Bezier it integrates the curve.
double draw_segment_sampled_length(
    const DrawSegment& seg,
    double tolerance_mm = DRAW_MODE_SAMPLE_TOLERANCE_MM);

// Returns the display length for any segment type.
// Uses sampling for CircularArc and CubicBezier; simple norm for Line.
double draw_display_length_mm(const DrawSegment& seg);

} // namespace Slic3r
