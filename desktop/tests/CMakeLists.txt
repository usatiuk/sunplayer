qt_add_executable(sunroom_presentation_target_tests
        unit/presentation/tst_PresentationTarget.cpp
        ../src/presentation/PresentationTarget.cpp
        ../src/presentation/PresentationTarget.h
        ../src/platform/DisplayState.h
)

qt_add_executable(sunroom_rendered_video_surface_tests
        unit/presentation/tst_RenderedVideoSurface.cpp
        ../src/presentation/RenderedVideoSurface.cpp
        ../src/presentation/RenderedVideoSurface.h
)

target_include_directories(sunroom_presentation_target_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunroom_rendered_video_surface_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(sunroom_presentation_target_tests
        PRIVATE
        Qt6::Test
)

target_link_libraries(sunroom_rendered_video_surface_tests
        PRIVATE
        Qt6::Test
)

if (WIN32)
    foreach (test_target IN ITEMS
            sunroom_presentation_target_tests
            sunroom_rendered_video_surface_tests)
        add_custom_command(
                TARGET ${test_target}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_RUNTIME_DLLS:${test_target}>
                        $<TARGET_FILE_DIR:${test_target}>
                COMMAND_EXPAND_LISTS
        )
    endforeach ()
endif ()

add_test(
        NAME presentation-target
        COMMAND sunroom_presentation_target_tests
)

add_test(
        NAME rendered-video-surface
        COMMAND sunroom_rendered_video_surface_tests
)

set_tests_properties(
        presentation-target
        rendered-video-surface
        PROPERTIES LABELS "unit"
)

if (WIN32)
    qt_add_executable(sunroom_qrhi_compositor_tests
            integration/presentation/tst_QrhiCompositor.cpp
            ../src/presentation/DiagnosticVideoProducer.cpp
            ../src/presentation/DiagnosticVideoProducer.h
            ../src/presentation/HdrCompositor.cpp
            ../src/presentation/HdrCompositor.h
            ../src/presentation/RenderedVideoSurface.cpp
            ../src/presentation/RenderedVideoSurface.h
    )

    target_include_directories(sunroom_qrhi_compositor_tests
            PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    )

    target_link_libraries(sunroom_qrhi_compositor_tests
            PRIVATE
            Qt6::GuiPrivate
            Qt6::Test
    )

    qt_add_shaders(
            sunroom_qrhi_compositor_tests
            "sunroom_qrhi_compositor_test_shaders"
            PRECOMPILE
            OPTIMIZED
            PREFIX
            /shaders
            BASE
            ../src/presentation/shaders
            FILES
            ../src/presentation/shaders/fullscreen.vert
            ../src/presentation/shaders/diagnostic_video.frag
            ../src/presentation/shaders/compositor.frag
    )

    add_custom_command(
            TARGET sunroom_qrhi_compositor_tests
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:sunroom_qrhi_compositor_tests>
                    $<TARGET_FILE_DIR:sunroom_qrhi_compositor_tests>
            COMMAND_EXPAND_LISTS
    )

    add_test(
            NAME qrhi-compositor
            COMMAND sunroom_qrhi_compositor_tests
    )
    set_tests_properties(
            qrhi-compositor
            PROPERTIES
            LABELS "gpu;windows"
            TIMEOUT 30
    )
endif ()
