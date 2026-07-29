qt_add_executable(sunroom_presentation_target_tests
        unit/presentation/tst_PresentationTarget.cpp
        ../src/presentation/PresentationTarget.cpp
        ../src/presentation/PresentationTarget.h
        ../src/platform/DisplayState.h
)

qt_add_executable(sunroom_rendered_video_surface_tests
        unit/video/tst_RenderedVideoSurface.cpp
        ../src/video/RenderedVideoSurface.cpp
        ../src/video/RenderedVideoSurface.h
        ../src/video/VideoTargetInterop.cpp
        ../src/video/VideoTargetInterop.h
)

qt_add_executable(sunroom_video_viewport_tests
        unit/ui/tst_VideoViewportState.cpp
        ../src/app/VideoViewportState.cpp
        ../src/app/VideoViewportState.h
)

qt_add_executable(sunroom_qml_shell_tests
        integration/ui/tst_AppShell.cpp
        integration/ui/QmlShellTestTypes.h
        ../src/app/VideoViewportState.cpp
        ../src/app/VideoViewportState.h
)

if (WIN32)
    qt_add_executable(sunroom_libplacebo_dependency_tests
            integration/video/tst_LibplaceboDependency.cpp
    )
endif ()

set_source_files_properties(../src/app/Main.qml PROPERTIES
        QT_RESOURCE_ALIAS Main.qml
        QT_QML_SKIP_CACHEGEN TRUE
)
set_source_files_properties(../src/app/AppShell.qml PROPERTIES
        QT_RESOURCE_ALIAS AppShell.qml
        QT_QML_SKIP_CACHEGEN TRUE
)
set_source_files_properties(../src/app/pages/HdrLabPage.qml PROPERTIES
        QT_RESOURCE_ALIAS HdrLabPage.qml
        QT_QML_SKIP_CACHEGEN TRUE
)

qt_add_qml_module(sunroom_qml_shell_tests
        URI SunroomShellTest
        VERSION 1.0
        QML_FILES
        ../src/app/Main.qml
        ../src/app/AppShell.qml
        ../src/app/pages/HdrLabPage.qml
)

target_include_directories(sunroom_presentation_target_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunroom_rendered_video_surface_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunroom_video_viewport_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunroom_qml_shell_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/app"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/ui"
)

target_link_libraries(sunroom_presentation_target_tests
        PRIVATE
        Qt6::Test
)

target_link_libraries(sunroom_rendered_video_surface_tests
        PRIVATE
        Qt6::Test
)

target_link_libraries(sunroom_video_viewport_tests
        PRIVATE
        Qt6::Test
)

target_link_libraries(sunroom_qml_shell_tests
        PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Test
)

if (WIN32)
    target_link_libraries(sunroom_libplacebo_dependency_tests
            PRIVATE
            Qt6::Test
            libplacebo::libplacebo
    )

    foreach (test_target IN ITEMS
            sunroom_presentation_target_tests
            sunroom_rendered_video_surface_tests
            sunroom_video_viewport_tests
            sunroom_qml_shell_tests
            sunroom_libplacebo_dependency_tests)
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

add_test(
        NAME video-viewport
        COMMAND sunroom_video_viewport_tests
)

add_test(
        NAME qml-shell
        COMMAND sunroom_qml_shell_tests
)

set_tests_properties(
        presentation-target
        rendered-video-surface
        video-viewport
        PROPERTIES LABELS "unit"
)

set_tests_properties(
        qml-shell
        PROPERTIES
        LABELS "component;ui"
        TIMEOUT 30
)

if (WIN32)
    add_test(
            NAME libplacebo-dependency
            COMMAND sunroom_libplacebo_dependency_tests
    )
    set_tests_properties(
            libplacebo-dependency
            PROPERTIES
            LABELS "dependency;integration;windows"
            TIMEOUT 30
    )

    qt_add_executable(sunroom_qrhi_compositor_tests
            integration/presentation/tst_QrhiCompositor.cpp
            ../src/graphics/GraphicsBackendFactory.cpp
            ../src/graphics/GraphicsBackendFactory.h
            ../src/graphics/GraphicsDeviceDomain.cpp
            ../src/graphics/GraphicsDeviceDomain.h
            ../src/graphics/backends/D3D11GraphicsDeviceDomain.cpp
            ../src/graphics/backends/D3D11GraphicsDeviceDomain.h
            ../src/graphics/backends/D3D11LibplaceboVideoTarget.cpp
            ../src/graphics/backends/D3D11LibplaceboVideoTarget.h
            ../src/presentation/HdrCompositor.cpp
            ../src/presentation/HdrCompositor.h
            ../src/video/DiagnosticVideoProducer.cpp
            ../src/video/DiagnosticVideoProducer.h
            ../src/video/DiagnosticVideoSource.cpp
            ../src/video/DiagnosticVideoSource.h
            ../src/video/LibplaceboDiagnosticVideoProducer.cpp
            ../src/video/LibplaceboDiagnosticVideoProducer.h
            ../src/video/QrhiVideoTarget.cpp
            ../src/video/QrhiVideoTarget.h
            ../src/video/RenderedVideoProducer.h
            ../src/video/RenderedVideoSurface.cpp
            ../src/video/RenderedVideoSurface.h
            ../src/video/RenderedVideoSource.h
            ../src/video/VideoTargetInterop.cpp
            ../src/video/VideoTargetInterop.h
    )

    target_include_directories(sunroom_qrhi_compositor_tests
            PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    )

    target_link_libraries(sunroom_qrhi_compositor_tests
            PRIVATE
            Qt6::GuiPrivate
            Qt6::Quick
            Qt6::Test
            libplacebo::libplacebo
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
