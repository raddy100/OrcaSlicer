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

void DrawSession::insert_layer(int position, double layer_height)
{
    if (layer_height <= 0.0)
        throw std::invalid_argument("DrawSession::insert_layer: layer_height must be positive");
    if (position < 0 || position > (int)layers.size())
        throw std::out_of_range("DrawSession::insert_layer: position out of range");

    const double z_start = (position > 0) ? layers[position - 1].z_end : 0.0;

    // Shift z and layer_index for all layers at and above the insertion point.
    for (int i = position; i < (int)layers.size(); ++i) {
        layers[i].z_start     += layer_height;
        layers[i].z_end       += layer_height;
        layers[i].layer_index  = i + 1;
    }

    DrawLayer new_layer;
    new_layer.layer_index = position;
    new_layer.z_start     = z_start;
    new_layer.z_end       = z_start + layer_height;
    layers.insert(layers.begin() + position, std::move(new_layer));
    active_layer = position;
}

bool DrawSession::remove_layer(int index)
{
    if (index < 0 || index >= (int)layers.size())
        return false;

    const int prev_active = active_layer;
    layers.erase(layers.begin() + index);

    if (layers.empty()) {
        active_layer = -1;
    } else if (prev_active == index) {
        // Removed the active layer: step back to previous, floor at 0
        active_layer = std::max(0, index - 1);
    } else if (prev_active > index) {
        // Active layer shifted down by the removal above it
        active_layer = prev_active - 1;
    }
    // prev_active < index: unchanged

    return true;
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
