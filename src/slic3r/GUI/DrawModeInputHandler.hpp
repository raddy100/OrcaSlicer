#pragma once

#include "libslic3r/DrawSession.hpp"
#include "libslic3r/Point.hpp"
#include <optional>
#include <vector>

namespace Slic3r {
namespace GUI {

struct Camera;

// Input mode for Draw Mode panel.
enum class DrawInputMode {
    Drawing, // left-click starts a new segment; left-drag draws interactively
    Editing, // click selects; drag moves endpoints
};

// Reference to one endpoint (start or end) of a segment.
struct EndpointRef {
    int  layer_index;
    int  segment_index;
    bool is_start; // true = start, false = end
};

// All constants are in plate-space millimetres.
constexpr double DRAW_SNAP_THRESHOLD       = 0.1;  // endpoint snap grid (mm)
constexpr double DRAW_ENDPOINT_SNAP_RADIUS = 1.0;  // snap-to-existing-endpoint radius (mm)

// Hit-test result for editing mode.
struct HitResult {
    enum class Kind { None, Endpoint, SegmentBody };
    Kind kind = Kind::None;
    int  layer_index    = -1;
    int  segment_index  = -1;
    bool is_start       = false; // only meaningful when kind == Endpoint
};

class DrawModeInputHandler
{
public:
    // nozzle_diameter_mm is used to compute HIT_THRESHOLD = nozzle_diameter * 2.
    explicit DrawModeInputHandler(double nozzle_diameter_mm = 0.4);

    // Convert a screen position (wxPoint-style) to a plate-relative XY
    // coordinate at the specified Z plane using a ray-to-plane intersection.
    // Returns nullopt if the ray is parallel to the plane.
    std::optional<Vec2d> screen_to_plate(
        const Camera& camera, double screen_x, double screen_y, double plane_z) const;

    // Distance from point P to the infinite line defined by segment [A,B].
    // Returns the distance and the closest parameter t in [0,1] for the
    // finite segment.
    static double point_to_segment_distance(const Vec2d& p, const Vec2d& a, const Vec2d& b, double& t_out);

    // Returns endpoints within DRAW_ENDPOINT_SNAP_RADIUS of pos on the
    // given layer.  Sorted nearest-first.
    std::vector<EndpointRef> find_nearby_endpoints(
        const DrawSession& session, int layer_index, const Vec2d& pos) const;

    // Hit-test a plate-space point against all segments in a layer.
    // Priority: endpoints first, then segment bodies.
    HitResult hit_test(
        const DrawSession& session, int layer_index, const Vec2d& pos) const;

    // Snap pos to the nearest existing endpoint within DRAW_ENDPOINT_SNAP_RADIUS,
    // or return pos unchanged.
    Vec2d snap_to_endpoint(
        const DrawSession& session, int layer_index, const Vec2d& pos) const;

private:
    double m_nozzle_diameter; // mm — used for hit threshold
    double m_hit_threshold;   // = nozzle_diameter * 2
};

} // namespace GUI
} // namespace Slic3r
