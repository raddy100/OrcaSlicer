#pragma once

#include <vector>
#include "Point.hpp"
#include "BoundingBox.hpp"

namespace Slic3r {

// A single straight-line move on the build plate.
// Coordinates are plate-relative (mm), relative to PartPlate::get_origin().
struct DrawSegment {
    Vec2d start;        // XY on build plate, plate-relative coords (mm)
    Vec2d end;          // XY on build plate, plate-relative coords (mm)
    bool  is_travel;    // true = non-extruding travel move

    double length() const { return (end - start).norm(); }
};

// One horizontal layer of drawn segments. Z values are baked in at creation
// time from the active process profile; geometry is preserved if the profile
// changes later.
struct DrawLayer {
    int                      layer_index; // 0-based
    double                   z_start;     // mm — Z at first segment start
    double                   z_end;       // mm — Z at last segment end
    std::vector<DrawSegment> segments;    // in draw order

    double layer_height() const { return z_end - z_start; }
};

// Pure-data session object. No GUI dependencies; must be serializable and
// work on all platforms. All print parameters (temperature, speed, …) are
// intentionally NOT stored here — they come from DynamicPrintConfig at
// G-code generation time.
struct DrawSession {
    std::vector<DrawLayer> layers;       // in Z order
    int                    active_layer; // index into layers[], -1 if none

    DrawSession() : active_layer(-1) {}

    bool   is_empty() const;
    // Appends a new layer using layer_height baked from the active profile.
    void   add_layer(double layer_height);
    // Removes the layer at index and adjusts active_layer:
    //   active == index => step to max(0, index-1), or -1 if no layers remain
    //   active >  index => decrement by 1 (layer shifted down)
    //   active <  index => unchanged
    // Returns false if index is out of range.
    bool   remove_layer(int index);
    void   clear();
    double total_height() const;

    // 3-D bounding box across all layers — used to create the synthetic mesh.
    BoundingBoxf3 bounding_box() const;

    int layer_count() const { return static_cast<int>(layers.size()); }
};

} // namespace Slic3r
