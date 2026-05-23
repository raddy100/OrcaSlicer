#include "DrawModeInputHandler.hpp"
#include "CameraUtils.hpp"
#include "Camera.hpp"
#include "libslic3r/DrawModeFeedback.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r {
namespace GUI {

DrawModeInputHandler::DrawModeInputHandler(double nozzle_diameter_mm)
    : m_nozzle_diameter(nozzle_diameter_mm)
    , m_hit_threshold(nozzle_diameter_mm * 2.0)
{}

std::optional<Vec2d> DrawModeInputHandler::screen_to_plate(
    const Camera& camera, double screen_x, double screen_y, double plane_z) const
{
    Vec3d ray_origin, ray_dir;
    CameraUtils::ray_from_screen_pos(camera, Vec2d(screen_x, screen_y), ray_origin, ray_dir);

    // Intersect with horizontal plane z = plane_z.
    // ray: P = ray_origin + t * ray_dir
    // intersection condition: ray_origin.z + t * ray_dir.z = plane_z
    if (std::abs(ray_dir.z()) < 1e-10)
        return std::nullopt; // ray parallel to plane

    double t = (plane_z - ray_origin.z()) / ray_dir.z();
    Vec3d hit = ray_origin + t * ray_dir;
    return Vec2d(hit.x(), hit.y());
}

double DrawModeInputHandler::point_to_segment_distance(
    const Vec2d& p, const Vec2d& a, const Vec2d& b, double& t_out)
{
    Vec2d ab = b - a;
    double len_sq = ab.squaredNorm();

    if (len_sq < 1e-12) {
        // Degenerate segment — treat as a point
        t_out = 0.0;
        return (p - a).norm();
    }

    t_out = (p - a).dot(ab) / len_sq;
    t_out = std::clamp(t_out, 0.0, 1.0);

    Vec2d closest = a + t_out * ab;
    return (p - closest).norm();
}

std::vector<EndpointRef> DrawModeInputHandler::find_nearby_endpoints(
    const DrawSession& session, int layer_index, const Vec2d& pos) const
{
    if (layer_index < 0 || layer_index >= (int)session.layers.size())
        return {};

    const DrawLayer& layer = session.layers[layer_index];
    std::vector<std::pair<double, EndpointRef>> candidates;

    for (int si = 0; si < (int)layer.segments.size(); ++si) {
        const DrawSegment& seg = layer.segments[si];

        double ds = (pos - seg.start).norm();
        double de = (pos - seg.end).norm();

        if (ds <= DRAW_ENDPOINT_SNAP_RADIUS)
            candidates.push_back({ds, {layer_index, si, true}});
        if (de <= DRAW_ENDPOINT_SNAP_RADIUS)
            candidates.push_back({de, {layer_index, si, false}});
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<EndpointRef> result;
    result.reserve(candidates.size());
    for (auto& c : candidates)
        result.push_back(c.second);
    return result;
}

HitResult DrawModeInputHandler::hit_test(
    const DrawSession& session, int layer_index, const Vec2d& pos) const
{
    if (layer_index < 0 || layer_index >= (int)session.layers.size())
        return {};

    const DrawLayer& layer = session.layers[layer_index];

    // Priority 1: endpoints
    double best_ep_dist = m_hit_threshold;
    HitResult best_ep;
    best_ep.kind = HitResult::Kind::None;

    for (int si = 0; si < (int)layer.segments.size(); ++si) {
        const DrawSegment& seg = layer.segments[si];

        double ds = (pos - seg.start).norm();
        if (ds < best_ep_dist) {
            best_ep_dist = ds;
            best_ep = {HitResult::Kind::Endpoint, layer_index, si, true, 0};
        }

        double de = (pos - seg.end).norm();
        if (de < best_ep_dist) {
            best_ep_dist = de;
            best_ep = {HitResult::Kind::Endpoint, layer_index, si, false, 0};
        }
    }

    if (best_ep.kind == HitResult::Kind::Endpoint)
        return best_ep;

    // Priority 2: control handles (arc through-point, bezier ctrl1/ctrl2)
    double best_ctrl_dist = m_hit_threshold;
    HitResult best_ctrl;
    best_ctrl.kind = HitResult::Kind::None;

    for (int si = 0; si < (int)layer.segments.size(); ++si) {
        const DrawSegment& seg = layer.segments[si];
        if (seg.type == DrawSegmentType::CircularArc) {
            double d = (pos - seg.ctrl1).norm();
            if (d < best_ctrl_dist) {
                best_ctrl_dist = d;
                best_ctrl = {HitResult::Kind::ControlHandle, layer_index, si, false, 0};
            }
        } else if (seg.type == DrawSegmentType::CubicBezier) {
            double d1 = (pos - seg.ctrl1).norm();
            if (d1 < best_ctrl_dist) {
                best_ctrl_dist = d1;
                best_ctrl = {HitResult::Kind::ControlHandle, layer_index, si, false, 0};
            }
            double d2 = (pos - seg.ctrl2).norm();
            if (d2 < best_ctrl_dist) {
                best_ctrl_dist = d2;
                best_ctrl = {HitResult::Kind::ControlHandle, layer_index, si, false, 1};
            }
        }
    }

    if (best_ctrl.kind == HitResult::Kind::ControlHandle)
        return best_ctrl;

    // Priority 3: segment bodies
    double best_body_dist = m_hit_threshold;
    HitResult best_body;
    best_body.kind = HitResult::Kind::None;

    for (int si = 0; si < (int)layer.segments.size(); ++si) {
        const DrawSegment& seg = layer.segments[si];
        if (seg.type == DrawSegmentType::Line) {
            double t = 0.0;
            double d = point_to_segment_distance(pos, seg.start, seg.end, t);
            if (d < best_body_dist) {
                best_body_dist = d;
                best_body = {HitResult::Kind::SegmentBody, layer_index, si, false, 0};
            }
        } else {
            // Arc or bezier: check each sub-segment of the sampled polyline
            auto pts = draw_sample_segment(seg, DRAW_MODE_SAMPLE_TOLERANCE_MM);
            for (size_t k = 1; k < pts.size(); ++k) {
                double t = 0.0;
                double d = point_to_segment_distance(pos, pts[k-1], pts[k], t);
                if (d < best_body_dist) {
                    best_body_dist = d;
                    best_body = {HitResult::Kind::SegmentBody, layer_index, si, false, 0};
                }
            }
        }
    }

    return best_body;
}

Vec2d DrawModeInputHandler::snap_to_endpoint(
    const DrawSession& session, int layer_index, const Vec2d& pos) const
{
    auto nearby = find_nearby_endpoints(session, layer_index, pos);
    if (nearby.empty()) return pos;

    const EndpointRef& ref = nearby.front();
    const DrawSegment& seg = session.layers[ref.layer_index].segments[ref.segment_index];
    return ref.is_start ? seg.start : seg.end;
}

} // namespace GUI
} // namespace Slic3r
