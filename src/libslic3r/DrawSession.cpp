#include "DrawSession.hpp"

#include <algorithm>
#include <stdexcept>
#include <limits>

namespace Slic3r {

bool DrawSession::is_empty() const
{
    if (layers.empty())
        return true;
    for (const DrawLayer& layer : layers)
        if (!layer.segments.empty())
            return false;
    return true;
}

void DrawSession::add_layer(double layer_height)
{
    if (layer_height <= 0.0)
        throw std::invalid_argument("DrawSession::add_layer: layer_height must be positive");

    DrawLayer layer;
    layer.layer_index = static_cast<int>(layers.size());
    if (layers.empty()) {
        layer.z_start = 0.0;
    } else {
        layer.z_start = layers.back().z_end;
    }
    layer.z_end = layer.z_start + layer_height;
    layers.push_back(std::move(layer));
    active_layer = static_cast<int>(layers.size()) - 1;
}

void DrawSession::clear()
{
    layers.clear();
    active_layer = -1;
}

double DrawSession::total_height() const
{
    if (layers.empty())
        return 0.0;
    return layers.back().z_end;
}

BoundingBoxf3 DrawSession::bounding_box() const
{
    BoundingBoxf3 bbox;
    for (const DrawLayer& layer : layers) {
        for (const DrawSegment& seg : layer.segments) {
            bbox.merge(Vec3d(seg.start.x(), seg.start.y(), layer.z_start));
            bbox.merge(Vec3d(seg.end.x(),   seg.end.y(),   layer.z_end));
        }
    }
    return bbox;
}

} // namespace Slic3r
