#pragma once

#include "DrawSession.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {

// Builds the preview/edit mesh for a DrawSession.  Consecutive extrusion
// segments that share endpoints are converted to one stroked polyline, so
// corners are represented as joined material instead of independent capped
// boxes.
TriangleMesh make_draw_path_mesh(const DrawSession& session, double nozzle_diameter);

} // namespace Slic3r

