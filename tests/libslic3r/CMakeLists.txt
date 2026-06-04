get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests.cpp
    test_3mf.cpp
    test_aabbindirect.cpp
    test_appconfig.cpp
    test_bambu_networking.cpp
    test_clipper_offset.cpp
    test_clipper_utils.cpp
    test_config.cpp
    test_preset_bundle_loading.cpp
    test_draw_session.cpp
    test_draw_mode_feedback.cpp
    test_draw_layer_slider.cpp
    test_draw_path_mesh.cpp
    test_draw_path_gcode.cpp
    test_elephant_foot_compensation.cpp
    test_geometry.cpp
    test_placeholder_parser.cpp
    test_polygon.cpp
    test_mutable_polygon.cpp
    test_mutable_priority_queue.cpp
    test_stl.cpp
    test_meshboolean.cpp
    test_marchingsquares.cpp
    test_timeutils.cpp
    test_voronoi.cpp
    test_optimizers.cpp
    # test_png_io.cpp
    test_indexed_triangle_set.cpp
    ../libnest2d/printer_parts.cpp
    )

if (TARGET OpenVDB::openvdb)
    target_sources(${_TEST_NAME}_tests PRIVATE test_hollowing.cpp)
endif()

target_link_libraries(${_TEST_NAME}_tests test_common libslic3r Catch2::Catch2WithMain)
target_include_directories(${_TEST_NAME}_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

# ---------------------------------------------------------------------------
# DrawLayerSlider pure-helper coverage ([DrawLayerSlider]).
#
# The pixel<->layer mapping helpers (layer_slider_y_to_index /
# layer_slider_index_to_y) live in the GUI translation unit
# src/slic3r/GUI/DrawLayerSlider.cpp, which #includes wx headers for the
# widget class. libslic3r_tests normally does not link the GUI library, so to
# exercise the REAL (un-reimplemented) helper definitions we compile that one
# production .cpp into the test target and link the wx core/base libraries it
# needs. Production code is left untouched. Guarded on SLIC3R_GUI so non-GUI
# builds keep building the rest of the suite.
# ---------------------------------------------------------------------------
if (SLIC3R_GUI)
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(wxWidgets 3.3 REQUIRED COMPONENTS core base)
    else ()
        find_package(wxWidgets 3.3 CONFIG REQUIRED COMPONENTS core base)
    endif ()
    if (wxWidgets_USE_FILE)
        include(${wxWidgets_USE_FILE})
    endif ()

    target_sources(${_TEST_NAME}_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/DrawLayerSlider.cpp)

    # The prebuilt wx release libraries are compiled with wxDEBUG_LEVEL=0 (asserts
    # stripped), so wxOnAssert/wxFormatString::Validate are NOT exported. Match the
    # production GUI build's define so DrawLayerSlider.obj does not reference them.
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/DrawLayerSlider.cpp
        TARGET_DIRECTORY ${_TEST_NAME}_tests
        PROPERTIES COMPILE_DEFINITIONS "wxDEBUG_LEVEL=0")

    if (TARGET wx::core)
        target_link_libraries(${_TEST_NAME}_tests wx::core wx::base)
    else ()
        target_include_directories(${_TEST_NAME}_tests PRIVATE ${wxWidgets_INCLUDE_DIRS})
        target_link_libraries(${_TEST_NAME}_tests ${wxWidgets_LIBRARIES})
    endif ()

    # Ensure wx (and any transitive) runtime DLLs land next to the test exe.
    if (WIN32)
        add_custom_command(TARGET ${_TEST_NAME}_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${_TEST_NAME}_tests>
                $<TARGET_FILE_DIR:${_TEST_NAME}_tests>
            COMMAND_EXPAND_LISTS)
    endif ()
endif ()

orcaslicer_copy_test_dlls()

catch_discover_tests(${_TEST_NAME}_tests)
