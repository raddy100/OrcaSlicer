#pragma once

#include <string>
#include <vector>
#include "DrawSession.hpp"
#include "GCodeWriter.hpp"
#include "Point.hpp"

namespace Slic3r {

class ModelObject;

// Converts a DrawSession into a complete, machine-ready G-code string using
// the existing GCodeWriter class.  All print parameters are read from
// full_config (merged printer + filament + process config obtained via
// wxGetApp().preset_bundle()->full_config() or Print's merged config).
//
// Coordinate mapping:
//   G-code position = segment_coord (plate-relative) + plate_origin + instance_offset
class DrawPathGCodeGenerator
{
public:
    // full_config : merged DynamicPrintConfig from the active presets.
    // plate_origin: XY offset from machine zero to the part plate origin (mm).
    DrawPathGCodeGenerator(const DynamicPrintConfig& full_config,
                           const Vec2d&              plate_origin);

    // Returns the complete G-code string for one instance of the session.
    // instance_offset: additional XY translation for the specific ModelInstance.
    std::string generate(const DrawSession& session,
                         const Vec2d& instance_offset = Vec2d::Zero());

    // Returns a single G-code string that covers all (session, instance_offset) pairs
    // with one shared preamble and one shared postamble.  Intended for multi-object /
    // multi-instance plates where calling generate() repeatedly would produce unwanted
    // duplicate homing sequences.
    using BatchItem = std::pair<const DrawSession*, Vec2d>;
    std::string generate_batch(const std::vector<BatchItem>& items);

    // Helpers shared by the GUI background slicing path and tests. A draw-path
    // plate is handled outside the normal slicer only if every object on the
    // candidate plate is a draw-path object. Batch collection is stricter and
    // only returns objects that also have the raw DrawSession data required by
    // this generator.
    static bool is_draw_path_object(const ModelObject* object);
    static bool contains_only_draw_path_objects(const std::vector<ModelObject*>& objects);
    static std::vector<BatchItem> collect_batch(const std::vector<ModelObject*>& objects);
    static bool can_generate_for_objects(const std::vector<ModelObject*>& objects);
    static std::string parent_path_for_file(const std::string& path);
    static bool ensure_output_directory(const std::string& path,
                                        std::string*       error_message = nullptr);
    static bool write_gcode_file(const std::string& path,
                                 const std::string& gcode,
                                 std::string*       error_message = nullptr);

private:
    GCodeWriter        m_writer;
    DynamicPrintConfig m_config;
    Vec2d              m_plate_origin;

    std::string generate_preamble(const DrawSession& session);
    std::string generate_layer(const DrawLayer& layer, const Vec2d& abs_offset);
    std::string generate_postamble();

    // Extrusion volume per mm of travel for a straight segment.
    // Formula: (nozzle_diameter * layer_height) / (pi * (filament_diameter/2)^2)
    //          * extrusion_multiplier * segment_length_mm
    // PRD-MAP: "extrusion_multiplier" → uses print_flow_ratio from process config.
    double calc_extrusion(double segment_length_mm) const;

    // Helpers to read typed config values safely (return 0.0 / 0 if key absent).
    double  cfg_float(const std::string& key) const;
    int     cfg_int  (const std::string& key) const;
    // For vector options (per-filament): returns value at index 0.
    double  cfg_float_vec(const std::string& key) const;
    int     cfg_int_vec  (const std::string& key) const;
};

} // namespace Slic3r
