#include "DrawModeFeedback.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace Slic3r {

namespace {

constexpr double PI = 3.14159265358979323846264338327950288;

double normalize_angle_degrees(double degrees)
{
    while (degrees > 180.0)
        degrees -= 360.0;
    while (degrees <= -180.0)
        degrees += 360.0;
    return degrees;
}

std::string fixed_number(double value, int precision)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

} // namespace

double draw_segment_length_mm(const Vec2d& start, const Vec2d& end)
{
    return (end - start).norm();
}

double draw_segment_absolute_angle_degrees(const Vec2d& start, const Vec2d& end)
{
    const Vec2d delta = end - start;
    if (delta.squaredNorm() <= DRAW_MODE_MIN_SEGMENT_LENGTH_MM * DRAW_MODE_MIN_SEGMENT_LENGTH_MM)
        return 0.0;
    return normalize_angle_degrees(std::atan2(delta.y(), delta.x()) * 180.0 / PI);
}

double draw_relative_angle_degrees(const Vec2d& previous_start, const Vec2d& previous_end, const Vec2d& start, const Vec2d& end)
{
    return normalize_angle_degrees(
        draw_segment_absolute_angle_degrees(start, end) - draw_segment_absolute_angle_degrees(previous_start, previous_end));
}

double draw_segment_relative_angle_degrees(const DrawLayer& layer, int segment_index)
{
    if (segment_index <= 0 || segment_index >= static_cast<int>(layer.segments.size()))
        return 0.0;

    const DrawSegment& previous = layer.segments[segment_index - 1];
    const DrawSegment& current  = layer.segments[segment_index];
    return draw_relative_angle_degrees(previous.start, previous.end, current.start, current.end);
}

DrawDirectionSnapMode draw_resolve_snap_mode(bool ctrl_down, bool alt_down)
{
    if (alt_down)
        return DrawDirectionSnapMode::Free;
    if (ctrl_down)
        return DrawDirectionSnapMode::Diagonal45;
    return DrawDirectionSnapMode::Cardinal;
}

Vec2d draw_project_cardinal(const Vec2d& start, const Vec2d& raw_end)
{
    const Vec2d delta = raw_end - start;
    if (delta.squaredNorm() <= DRAW_MODE_MIN_SEGMENT_LENGTH_MM * DRAW_MODE_MIN_SEGMENT_LENGTH_MM)
        return start;

    if (std::abs(delta.x()) >= std::abs(delta.y()))
        return Vec2d(start.x() + delta.x(), start.y());

    return Vec2d(start.x(), start.y() + delta.y());
}

Vec2d draw_project_diagonal_45(const Vec2d& start, const Vec2d& raw_end)
{
    const Vec2d delta = raw_end - start;
    const double length = delta.norm();
    if (length <= DRAW_MODE_MIN_SEGMENT_LENGTH_MM)
        return start;

    const double angle = std::atan2(delta.y(), delta.x());
    const double step = PI / 4.0;
    const double snapped = std::round(angle / step) * step;
    return start + Vec2d(std::cos(snapped), std::sin(snapped)) * length;
}

Vec2d draw_apply_direction_snap(const Vec2d& start, const Vec2d& raw_end, DrawDirectionSnapMode mode)
{
    switch (mode) {
    case DrawDirectionSnapMode::Cardinal:   return draw_project_cardinal(start, raw_end);
    case DrawDirectionSnapMode::Diagonal45: return draw_project_diagonal_45(start, raw_end);
    case DrawDirectionSnapMode::Free:       return raw_end;
    }
    return raw_end;
}

Vec2d draw_project_typed_length(const Vec2d& start, const Vec2d& direction_end, double length_mm)
{
    if (length_mm <= DRAW_MODE_MIN_SEGMENT_LENGTH_MM)
        return start;

    Vec2d direction = direction_end - start;
    const double direction_length = direction.norm();
    if (direction_length <= DRAW_MODE_MIN_SEGMENT_LENGTH_MM)
        direction = Vec2d(1.0, 0.0);
    else
        direction /= direction_length;

    return start + direction * length_mm;
}

std::optional<double> draw_parse_length_mm(const std::string& text)
{
    std::string normalized;
    normalized.reserve(text.size());
    bool has_decimal = false;
    bool has_digit = false;

    for (char ch : text) {
        if (ch >= '0' && ch <= '9') {
            normalized.push_back(ch);
            has_digit = true;
        } else if ((ch == '.' || ch == ',') && !has_decimal) {
            normalized.push_back('.');
            has_decimal = true;
        } else if (ch == ' ' || ch == '\t') {
            continue;
        } else {
            return std::nullopt;
        }
    }

    if (!has_digit)
        return std::nullopt;

    try {
        const double value = std::stod(normalized);
        if (value <= DRAW_MODE_MIN_SEGMENT_LENGTH_MM || !std::isfinite(value))
            return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::string draw_format_length_mm(double length_mm)
{
    return fixed_number(length_mm, 2) + " mm";
}

std::string draw_format_angle_degrees(double angle_degrees)
{
    return fixed_number(angle_degrees, 1) + "\xC2\xB0";
}

std::string draw_format_coordinate_mm(const Vec2d& pt)
{
    return "X: " + fixed_number(pt.x(), 2) + " mm  Y: " + fixed_number(pt.y(), 2) + " mm";
}

Vec2d draw_nudge_delta_from_key(int key_code, bool shift_down)
{
    const double step = shift_down ? DRAW_MODE_NUDGE_LARGE_MM : DRAW_MODE_NUDGE_SMALL_MM;
    switch (key_code) {
    case 314: return Vec2d(-step, 0.0); // WXK_LEFT
    case 315: return Vec2d(0.0, step);  // WXK_UP
    case 316: return Vec2d(step, 0.0);  // WXK_RIGHT
    case 317: return Vec2d(0.0, -step); // WXK_DOWN
    default:  return Vec2d(0.0, 0.0);
    }
}

DrawSegment draw_translate_segment(const DrawSegment& segment, const Vec2d& delta)
{
    DrawSegment translated = segment;
    translated.start += delta;
    translated.end   += delta;
    return translated;
}

double draw_clamp_zoom_factor(double zoom_factor)
{
    if (!std::isfinite(zoom_factor))
        return DRAW_MODE_DEFAULT_ZOOM_FACTOR;
    return std::clamp(zoom_factor, DRAW_MODE_MIN_ZOOM_FACTOR, DRAW_MODE_MAX_ZOOM_FACTOR);
}

Vec2d draw_clamp_pan_offset(const Vec2d& pan_offset, double plate_width_mm, double plate_height_mm, double zoom_factor)
{
    const double zoom = draw_clamp_zoom_factor(zoom_factor);
    if (!std::isfinite(plate_width_mm) || plate_width_mm <= 0.0
            || !std::isfinite(plate_height_mm) || plate_height_mm <= 0.0)
        return Vec2d(0.0, 0.0);

    const double visible_w = plate_width_mm / zoom;
    const double visible_h = plate_height_mm / zoom;

    const auto clamp_axis = [](double requested, double plate_mm, double visible_mm) {
        if (!std::isfinite(requested))
            requested = 0.0;
        if (visible_mm >= plate_mm)
            return (plate_mm - visible_mm) * 0.5;
        return std::clamp(requested, 0.0, plate_mm - visible_mm);
    };

    return Vec2d(clamp_axis(pan_offset.x(), plate_width_mm, visible_w),
                 clamp_axis(pan_offset.y(), plate_height_mm, visible_h));
}

int draw_scale_bar_length_pixels(double grid_spacing_mm, double plate_width_mm, double zoom_factor, double inner_canvas_width_px)
{
    if (!std::isfinite(grid_spacing_mm) || grid_spacing_mm <= 0.0
            || !std::isfinite(plate_width_mm) || plate_width_mm <= 0.0
            || !std::isfinite(inner_canvas_width_px) || inner_canvas_width_px <= 0.0)
        return 0;

    const double zoom = draw_clamp_zoom_factor(zoom_factor);
    const double pixels_per_mm = inner_canvas_width_px * zoom / plate_width_mm;
    return std::max(1, static_cast<int>(std::round(grid_spacing_mm * pixels_per_mm)));
}

bool draw_parse_bool_preference(const std::string& value, bool default_value)
{
    if (value == "1" || value == "true" || value == "True" || value == "TRUE")
        return true;
    if (value == "0" || value == "false" || value == "False" || value == "FALSE")
        return false;
    return default_value;
}

std::string draw_format_bool_preference(bool value)
{
    return value ? "1" : "0";
}

} // namespace Slic3r
