#include "DrawModeFeedback.hpp"

#include "Model.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
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
    // Move the arc through-point and bezier control points too.
    translated.ctrl1 += delta;
    translated.ctrl2 += delta;
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

ModelObject* draw_find_first_restorable_draw_object(const std::vector<ModelObject*>& objects, int* object_index)
{
    if (object_index != nullptr)
        *object_index = -1;

    for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
        ModelObject* object = objects[i];
        if (object != nullptr && object->is_draw_path_object() && object->draw_session != nullptr && !object->draw_session->is_empty()) {
            if (object_index != nullptr)
                *object_index = i;
            return object;
        }
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// Curve sampling helpers (TASK-003)
// ---------------------------------------------------------------------------

namespace {

// Compute the circumcenter of three 2D points.
// Returns false if the points are collinear (within numerical tolerance).
bool circumcenter_2d(Vec2d S, Vec2d P, Vec2d E, Vec2d& center)
{
    const double ax = S.x(), ay = S.y();
    const double bx = P.x(), by = P.y();
    const double cx = E.x(), cy = E.y();

    const double D = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::abs(D) < 1e-10)
        return false; // collinear

    const double sa = ax * ax + ay * ay;
    const double sb = bx * bx + by * by;
    const double sc = cx * cx + cy * cy;

    const double ux = (sa * (by - cy) + sb * (cy - ay) + sc * (ay - by)) / D;
    const double uy = (sa * (cx - bx) + sb * (ax - cx) + sc * (bx - ax)) / D;
    center = Vec2d(ux, uy);
    return true;
}

// Return angle in [0, 2*PI).
double normalize_angle_2pi(double a)
{
    while (a < 0.0)        a += 2.0 * PI;
    while (a >= 2.0 * PI)  a -= 2.0 * PI;
    return a;
}

// True if angle `p` lies strictly on the CCW arc from `s` to `e` (exclusive).
bool angle_on_ccw_arc(double s, double p, double e)
{
    s = normalize_angle_2pi(s);
    p = normalize_angle_2pi(p);
    e = normalize_angle_2pi(e);
    // Translate so s == 0
    p = normalize_angle_2pi(p - s);
    e = normalize_angle_2pi(e - s);
    if (e < 1e-12) e = 2.0 * PI; // full circle — treat as 2π
    return p < e;
}

// Sample the circular arc through S, P (through-point), E.
// Returns {S, ..., E} with chord-to-circle deviation < tolerance_mm.
// Returns {S, E} for degenerate (collinear) input.
std::vector<Vec2d> sample_arc(Vec2d S, Vec2d P, Vec2d E, double tol)
{
    Vec2d C;
    if (!circumcenter_2d(S, P, E, C))
        return { S, E };

    const double R = (S - C).norm();
    if (R < 1e-10)
        return { S, E };

    const double theta_s = std::atan2(S.y() - C.y(), S.x() - C.x());
    const double theta_p = std::atan2(P.y() - C.y(), P.x() - C.x());
    const double theta_e = std::atan2(E.y() - C.y(), E.x() - C.x());

    // Determine winding direction: does going CCW from theta_s reach theta_p
    // before theta_e?  If yes → CCW arc; else → CW arc.
    const bool ccw = angle_on_ccw_arc(theta_s, theta_p, theta_e);

    double sweep;
    if (ccw) {
        sweep = normalize_angle_2pi(theta_e - theta_s);
        if (sweep < 1e-12) sweep = 2.0 * PI; // full circle
    } else {
        sweep = -normalize_angle_2pi(theta_s - theta_e);
        if (sweep > -1e-12) sweep = -2.0 * PI;
    }

    // Maximum angular step such that chord deviation < tol.
    // chord_dev = R*(1 - cos(dθ/2)) < tol  ⟹  dθ < 2*acos(1 - tol/R)
    const double ratio = tol / R;
    double max_step = 2.0 * PI; // fallback: one segment
    if (ratio < 1.0)
        max_step = 2.0 * std::acos(std::max(-1.0, 1.0 - ratio));
    if (max_step < 1e-10)
        max_step = 1e-10;

    const int n = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / max_step)));

    std::vector<Vec2d> pts;
    pts.reserve(static_cast<size_t>(n) + 1);
    pts.push_back(S); // exact start
    for (int i = 1; i < n; ++i) {
        const double t     = static_cast<double>(i) / static_cast<double>(n);
        const double theta = theta_s + t * sweep;
        pts.push_back(Vec2d(C.x() + R * std::cos(theta), C.y() + R * std::sin(theta)));
    }
    pts.push_back(E); // exact end
    return pts;
}

// Point-to-line distance (line through A and B, infinite).
double point_to_line_dist(Vec2d Q, Vec2d A, Vec2d B)
{
    const Vec2d AB = B - A;
    const double len2 = AB.squaredNorm();
    if (len2 < 1e-20)
        return (Q - A).norm();
    // area of parallelogram / base
    const double cross = (B.x() - A.x()) * (Q.y() - A.y()) - (B.y() - A.y()) * (Q.x() - A.x());
    return std::abs(cross) / std::sqrt(len2);
}

// Recursive de Casteljau subdivision for a cubic Bezier.
// pts receives the sampled points (without the final end point, which is added
// by the caller after the last recursive call).
void subdivide_bezier(Vec2d P0, Vec2d P1, Vec2d P2, Vec2d P3,
                      double tol, std::vector<Vec2d>& pts)
{
    // Flatness test: if both control points are within tol of the chord, stop.
    const double d1 = point_to_line_dist(P1, P0, P3);
    const double d2 = point_to_line_dist(P2, P0, P3);
    if (d1 <= tol && d2 <= tol) {
        // Flat enough — do not add P3 here (caller adds end once).
        return;
    }
    // de Casteljau subdivision at t = 0.5
    const Vec2d M01  = (P0 + P1) * 0.5;
    const Vec2d M12  = (P1 + P2) * 0.5;
    const Vec2d M23  = (P2 + P3) * 0.5;
    const Vec2d M012 = (M01 + M12) * 0.5;
    const Vec2d M123 = (M12 + M23) * 0.5;
    const Vec2d M    = (M012 + M123) * 0.5;

    subdivide_bezier(P0, M01, M012, M, tol, pts);
    pts.push_back(M);
    subdivide_bezier(M, M123, M23, P3, tol, pts);
}

} // namespace

// ---------------------------------------------------------------------------
// Public: curve sampling API
// ---------------------------------------------------------------------------

std::vector<Vec2d> draw_sample_segment(const DrawSegment& seg, double tolerance_mm)
{
    if (tolerance_mm <= 0.0)
        tolerance_mm = DRAW_MODE_SAMPLE_TOLERANCE_MM;

    switch (seg.type) {
    case DrawSegmentType::Line:
        return { seg.start, seg.end };

    case DrawSegmentType::CircularArc:
        return sample_arc(seg.start, seg.ctrl1, seg.end, tolerance_mm);

    case DrawSegmentType::CubicBezier: {
        std::vector<Vec2d> pts;
        pts.push_back(seg.start);
        subdivide_bezier(seg.start, seg.ctrl1, seg.ctrl2, seg.end, tolerance_mm, pts);
        pts.push_back(seg.end);
        return pts;
    }
    }
    return { seg.start, seg.end };
}

double draw_segment_sampled_length(const DrawSegment& seg, double tolerance_mm)
{
    const std::vector<Vec2d> pts = draw_sample_segment(seg, tolerance_mm);
    double total = 0.0;
    for (size_t i = 1; i < pts.size(); ++i)
        total += (pts[i] - pts[i - 1]).norm();
    return total;
}

namespace {

constexpr double SPLICE_MIN_SEGMENT_MM = 1e-3;

Vec2d lerp(Vec2d a, Vec2d b, double t)
{
    return a + (b - a) * t;
}

struct ArcGeometry {
    Vec2d  center;
    double radius { 0.0 };
    double theta_start { 0.0 };
    double sweep { 0.0 };
};

bool compute_arc_geometry(const DrawSegment& seg, ArcGeometry& arc)
{
    if (!circumcenter_2d(seg.start, seg.ctrl1, seg.end, arc.center))
        return false;

    arc.radius = (seg.start - arc.center).norm();
    if (arc.radius < 1e-10)
        return false;

    arc.theta_start = std::atan2(seg.start.y() - arc.center.y(), seg.start.x() - arc.center.x());
    const double theta_p = std::atan2(seg.ctrl1.y() - arc.center.y(), seg.ctrl1.x() - arc.center.x());
    const double theta_e = std::atan2(seg.end.y() - arc.center.y(), seg.end.x() - arc.center.x());
    const bool ccw = angle_on_ccw_arc(arc.theta_start, theta_p, theta_e);

    if (ccw) {
        arc.sweep = normalize_angle_2pi(theta_e - arc.theta_start);
        if (arc.sweep < 1e-12)
            arc.sweep = 2.0 * PI;
    } else {
        arc.sweep = -normalize_angle_2pi(arc.theta_start - theta_e);
        if (arc.sweep > -1e-12)
            arc.sweep = -2.0 * PI;
    }
    return true;
}

Vec2d arc_point_at(const ArcGeometry& arc, double s, double total)
{
    const double theta = arc.theta_start + (total > 0.0 ? (s / total) * arc.sweep : 0.0);
    return Vec2d(arc.center.x() + arc.radius * std::cos(theta),
                 arc.center.y() + arc.radius * std::sin(theta));
}

double nearest_polyline_arclength(const std::vector<Vec2d>& pts, const Vec2d& click_pt)
{
    if (pts.empty())
        return 0.0;

    double best_dist2 = std::numeric_limits<double>::max();
    double best_s = 0.0;
    double cumulative = 0.0;

    for (size_t i = 1; i < pts.size(); ++i) {
        const Vec2d a = pts[i - 1];
        const Vec2d b = pts[i];
        const Vec2d ab = b - a;
        const double len2 = ab.squaredNorm();
        double t = 0.0;
        if (len2 > 1e-20)
            t = std::clamp((click_pt - a).dot(ab) / len2, 0.0, 1.0);
        const Vec2d nearest = a + ab * t;
        const double dist2 = (click_pt - nearest).squaredNorm();
        const double seg_len = std::sqrt(len2);
        if (dist2 < best_dist2) {
            best_dist2 = dist2;
            best_s = cumulative + seg_len * t;
        }
        cumulative += seg_len;
    }

    if (pts.size() == 1)
        return 0.0;
    return best_s;
}

void clamp_splice_cuts(double total, double gap_mm, double& s_center, double& cut_a, double& cut_b)
{
    const double half_gap = gap_mm * 0.5;
    const double min_len = SPLICE_MIN_SEGMENT_MM + 1e-12;
    s_center = std::clamp(s_center, half_gap + min_len, total - half_gap - min_len);
    cut_a = s_center - half_gap;
    cut_b = s_center + half_gap;
}

Vec2d bezier_point_at(Vec2d p0, Vec2d p1, Vec2d p2, Vec2d p3, double t)
{
    const Vec2d m01 = lerp(p0, p1, t);
    const Vec2d m12 = lerp(p1, p2, t);
    const Vec2d m23 = lerp(p2, p3, t);
    const Vec2d m012 = lerp(m01, m12, t);
    const Vec2d m123 = lerp(m12, m23, t);
    return lerp(m012, m123, t);
}

double bezier_t_at_arclength(const std::vector<double>& cumulative, double target_s)
{
    if (cumulative.empty() || target_s <= 0.0)
        return 0.0;
    if (target_s >= cumulative.back())
        return 1.0;

    auto it = std::lower_bound(cumulative.begin(), cumulative.end(), target_s);
    const size_t idx = static_cast<size_t>(std::distance(cumulative.begin(), it));
    if (idx == 0)
        return 0.0;

    const double s0 = cumulative[idx - 1];
    const double s1 = cumulative[idx];
    const double local = (s1 > s0) ? (target_s - s0) / (s1 - s0) : 0.0;
    return (static_cast<double>(idx - 1) + local) / 256.0;
}

} // namespace

bool draw_splice_segment(const DrawSegment& seg, const Vec2d& click_pt, double gap_mm,
                         DrawSegment& part_a, DrawSegment& part_b, double tolerance_mm)
{
    gap_mm = std::max(0.0, gap_mm);
    if (tolerance_mm <= 0.0)
        tolerance_mm = DRAW_MODE_SAMPLE_TOLERANCE_MM;

    if (seg.type == DrawSegmentType::Line) {
        const double total = (seg.end - seg.start).norm();
        if (total <= gap_mm + 2.0 * SPLICE_MIN_SEGMENT_MM)
            return false;

        const Vec2d dir = (seg.end - seg.start) / total;
        double s_center = std::clamp((click_pt - seg.start).dot(dir), 0.0, total);
        double cut_a = 0.0;
        double cut_b = 0.0;
        clamp_splice_cuts(total, gap_mm, s_center, cut_a, cut_b);

        part_a = DrawSegment::make_line(seg.start, seg.start + dir * cut_a, seg.is_travel);
        part_b = DrawSegment::make_line(seg.start + dir * cut_b, seg.end, seg.is_travel);
        return true;
    }

    if (seg.type == DrawSegmentType::CircularArc) {
        ArcGeometry arc;
        if (!compute_arc_geometry(seg, arc)) {
            const DrawSegment line_fallback = DrawSegment::make_line(seg.start, seg.end, seg.is_travel);
            return draw_splice_segment(line_fallback, click_pt, gap_mm, part_a, part_b, tolerance_mm);
        }

        const double total = std::abs(arc.sweep) * arc.radius;
        if (total <= gap_mm + 2.0 * SPLICE_MIN_SEGMENT_MM)
            return false;

        const std::vector<Vec2d> pts = sample_arc(seg.start, seg.ctrl1, seg.end, tolerance_mm);
        const double sampled_total = draw_segment_sampled_length(seg, tolerance_mm);
        double s_center = nearest_polyline_arclength(pts, click_pt);
        if (sampled_total > 1e-12)
            s_center *= total / sampled_total;
        s_center = std::clamp(s_center, 0.0, total);

        double cut_a = 0.0;
        double cut_b = 0.0;
        clamp_splice_cuts(total, gap_mm, s_center, cut_a, cut_b);

        const Vec2d a_end = arc_point_at(arc, cut_a, total);
        const Vec2d b_start = arc_point_at(arc, cut_b, total);
        part_a = DrawSegment::make_arc(seg.start, arc_point_at(arc, cut_a * 0.5, total), a_end, seg.is_travel);
        part_b = DrawSegment::make_arc(b_start, arc_point_at(arc, (cut_b + total) * 0.5, total), seg.end, seg.is_travel);
        return true;
    }

    constexpr int BEZIER_SAMPLES = 256;
    std::vector<Vec2d> pts;
    std::vector<double> cumulative;
    pts.reserve(BEZIER_SAMPLES + 1);
    cumulative.reserve(BEZIER_SAMPLES + 1);
    pts.push_back(seg.start);
    cumulative.push_back(0.0);
    for (int i = 1; i <= BEZIER_SAMPLES; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(BEZIER_SAMPLES);
        pts.push_back(bezier_point_at(seg.start, seg.ctrl1, seg.ctrl2, seg.end, t));
        cumulative.push_back(cumulative.back() + (pts.back() - pts[pts.size() - 2]).norm());
    }

    const double total = cumulative.back();
    if (total <= gap_mm + 2.0 * SPLICE_MIN_SEGMENT_MM)
        return false;

    double s_center = nearest_polyline_arclength(pts, click_pt);
    s_center = std::clamp(s_center, 0.0, total);
    double cut_a = 0.0;
    double cut_b = 0.0;
    clamp_splice_cuts(total, gap_mm, s_center, cut_a, cut_b);

    const double t1 = bezier_t_at_arclength(cumulative, cut_a);
    const double t2 = bezier_t_at_arclength(cumulative, cut_b);

    const Vec2d a1 = lerp(seg.start, seg.ctrl1, t1);
    const Vec2d m12_a = lerp(seg.ctrl1, seg.ctrl2, t1);
    const Vec2d a2 = lerp(a1, m12_a, t1);
    const Vec2d a3 = bezier_point_at(seg.start, seg.ctrl1, seg.ctrl2, seg.end, t1);

    const Vec2d m01_b = lerp(seg.start, seg.ctrl1, t2);
    const Vec2d m12_b = lerp(seg.ctrl1, seg.ctrl2, t2);
    const Vec2d m23_b = lerp(seg.ctrl2, seg.end, t2);
    const Vec2d m012_b = lerp(m01_b, m12_b, t2);
    const Vec2d m123_b = lerp(m12_b, m23_b, t2);
    const Vec2d m0123_b = lerp(m012_b, m123_b, t2);

    part_a = DrawSegment::make_bezier(seg.start, a1, a2, a3, seg.is_travel);
    part_b = DrawSegment::make_bezier(m0123_b, m123_b, m23_b, seg.end, seg.is_travel);
    return true;
}

double draw_display_length_mm(const DrawSegment& seg)
{
    if (seg.type == DrawSegmentType::Line)
        return draw_segment_length_mm(seg.start, seg.end);
    return draw_segment_sampled_length(seg);
}

} // namespace Slic3r
