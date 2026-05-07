#pragma once

#include "DrawSession.hpp"

#include <optional>
#include <string>

namespace Slic3r {

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
DrawSegment draw_translate_segment(const DrawSegment& segment, const Vec2d& delta);

bool draw_parse_bool_preference(const std::string& value, bool default_value);
std::string draw_format_bool_preference(bool value);

} // namespace Slic3r
