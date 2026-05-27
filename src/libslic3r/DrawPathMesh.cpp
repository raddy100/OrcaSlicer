#include "DrawPathMesh.hpp"

#include "ClipperUtils.hpp"
#include "DrawModeFeedback.hpp"
#include "Polyline.hpp"
#include "Tesselate.hpp"

#include <algorithm>

namespace Slic3r {
namespace {

constexpr double DRAW_PATH_MIN_SEGMENT_LENGTH_MM = 1e-6;
constexpr double DRAW_PATH_ENDPOINT_EPSILON_MM   = 1e-6;

bool same_point(const Vec2d& a, const Vec2d& b)
{
    return (a - b).squaredNorm() <= DRAW_PATH_ENDPOINT_EPSILON_MM * DRAW_PATH_ENDPOINT_EPSILON_MM;
}

using DrawPolyline = std::vector<Vec2d>;

void push_path_if_valid(std::vector<DrawPolyline>& paths, DrawPolyline& path)
{
    if (path.size() >= 2)
        paths.push_back(std::move(path));
    path.clear();
}

std::vector<DrawPolyline> collect_extrusion_polylines(const DrawLayer& layer, double tol)
{
    std::vector<DrawPolyline> paths;
    DrawPolyline current;

    for (const DrawSegment& seg : layer.segments) {
        if (seg.is_travel || seg.length() <= DRAW_PATH_MIN_SEGMENT_LENGTH_MM) {
            push_path_if_valid(paths, current);
            continue;
        }

        const std::vector<Vec2d> sampled = draw_sample_segment(seg, tol);

        if (current.empty()) {
            // Start a new chain with all sampled points.
            current.insert(current.end(), sampled.begin(), sampled.end());
        } else if (same_point(current.back(), sampled.front())) {
            // Segments are connected: skip the duplicate start point.
            current.insert(current.end(), sampled.begin() + 1, sampled.end());
        } else {
            push_path_if_valid(paths, current);
            current.insert(current.end(), sampled.begin(), sampled.end());
        }
    }

    push_path_if_valid(paths, current);
    return paths;
}

indexed_triangle_set wall_strip(const Polygon& poly, double lower_z_mm, double upper_z_mm)
{
    indexed_triangle_set ret;
    const size_t offs = poly.points.size();
    if (offs < 3)
        return ret;

    const size_t startidx = ret.vertices.size();
    ret.vertices.reserve(ret.vertices.size() + 2 * offs);

    for (const Point& p : poly.points)
        ret.vertices.emplace_back(to_3d(unscaled(p).cast<float>().eval(), static_cast<float>(lower_z_mm)));

    for (const Point& p : poly.points)
        ret.vertices.emplace_back(to_3d(unscaled(p).cast<float>().eval(), static_cast<float>(upper_z_mm)));

    for (size_t i = startidx + 1; i < startidx + offs; ++i) {
        ret.indices.emplace_back(i - 1, i, i + offs - 1);
        ret.indices.emplace_back(i, i + offs, i + offs - 1);
    }

    ret.indices.emplace_back(startidx + offs - 1, startidx, startidx + 2 * offs - 1);
    ret.indices.emplace_back(startidx, startidx + offs, startidx + 2 * offs - 1);

    return ret;
}

indexed_triangle_set straight_walls(const ExPolygon& expoly, double lower_z_mm, double upper_z_mm)
{
    indexed_triangle_set ret = wall_strip(expoly.contour, lower_z_mm, upper_z_mm);
    for (const Polygon& hole : expoly.holes)
        its_merge(ret, wall_strip(hole, lower_z_mm, upper_z_mm));
    return ret;
}

indexed_triangle_set straight_walls(const ExPolygons& expolys, double lower_z_mm, double upper_z_mm)
{
    indexed_triangle_set ret;
    for (const ExPolygon& expoly : expolys)
        its_merge(ret, straight_walls(expoly, lower_z_mm, upper_z_mm));
    return ret;
}

} // namespace

TriangleMesh make_draw_path_mesh(const DrawSession& session, double nozzle_diameter)
{
    if (session.is_empty())
        return TriangleMesh();

    const double width = std::max(nozzle_diameter, 0.01);
    const double tol   = session.curve_tolerance_mm;
    indexed_triangle_set mesh;

    for (const DrawLayer& layer : session.layers) {
        Polygons layer_polygons;

        for (const DrawPolyline& path : collect_extrusion_polylines(layer, tol)) {
            Polyline polyline = Polyline::new_scale(path);
            Polygons stroked = offset(
                polyline,
                static_cast<float>(scale_(width * 0.5)),
                ClipperLib::jtMiter,
                DefaultMiterLimit,
                ClipperLib::etOpenButt);
            layer_polygons.insert(layer_polygons.end(), stroked.begin(), stroked.end());
        }

        if (layer_polygons.empty())
            continue;

        const ExPolygons footprints = union_ex(layer_polygons);
        its_merge(mesh, triangulate_expolygons_3d(footprints, layer.z_start, NORMALS_DOWN));
        its_merge(mesh, straight_walls(footprints, layer.z_start, layer.z_end));
        its_merge(mesh, triangulate_expolygons_3d(footprints, layer.z_end, NORMALS_UP));
    }

    if (mesh.indices.empty())
        return TriangleMesh();

    its_merge_vertices(mesh);
    its_remove_degenerate_faces(mesh);
    its_compactify_vertices(mesh);
    return TriangleMesh(std::move(mesh));
}

} // namespace Slic3r

