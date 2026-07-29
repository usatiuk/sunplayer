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

qt_add_executable(sunroom_decoded_video_frame_tests
        unit/media/tst_DecodedVideoFrame.cpp
)

qt_add_executable(sunroom_qml_shell_tests
        integration/ui/tst_AppShell.cpp
        integration/ui/QmlShellTestTypes.h
        ../src/app/VideoViewportState.cpp
        ../src/app/VideoViewportState.h
)

if (WIN32)
    qt_add_executable(sunroom_ffmpeg_dependency_tests
            integration/video/tst_FfmpegDependency.cpp
    )

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

target_include_directories(sunroom_decoded_video_frame_tests
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

target_link_libraries(sunroom_decoded_video_frame_tests
        PRIVATE
        Qt6::Test
        sunroom_media
        sunroom_ffmpeg
)

target_link_libraries(sunroom_qml_shell_tests
        PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Test
)

if (WIN32)
    target_link_libraries(sunroom_ffmpeg_dependency_tests
            PRIVATE
            Qt6::Test
            sunroom_ffmpeg
    )

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
        sunroom_stage_windows_runtime(${test_target})
    endforeach ()
    sunroom_stage_windows_runtime(
            sunroom_decoded_video_frame_tests
    )
    sunroom_stage_windows_runtime(
            sunroom_ffmpeg_dependency_tests
    )
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
        NAME decoded-video-frame
        COMMAND sunroom_decoded_video_frame_tests
)

add_test(
        NAME qml-shell
        COMMAND sunroom_qml_shell_tests
)

set_tests_properties(
        presentation-target
        rendered-video-surface
        video-viewport
        decoded-video-frame
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
            NAME ffmpeg-dependency
            COMMAND sunroom_ffmpeg_dependency_tests
    )
    set_tests_properties(
            ffmpeg-dependency
            PROPERTIES
            LABELS "dependency;integration;windows"
            TIMEOUT 30
    )

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
            sunroom_video_pipeline
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

    sunroom_stage_windows_runtime(
            sunroom_qrhi_compositor_tests
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

    qt_add_executable(sunroom_ffmpeg_first_frame_tests
            integration/video/tst_FfmpegFirstFrame.cpp
    )
    target_include_directories(
            sunroom_ffmpeg_first_frame_tests
            PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    )
    target_compile_definitions(
            sunroom_ffmpeg_first_frame_tests
            PRIVATE
            SUNROOM_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    target_link_libraries(
            sunroom_ffmpeg_first_frame_tests
            PRIVATE
            Qt6::GuiPrivate
            Qt6::Quick
            Qt6::Test
            sunroom_video_pipeline
    )
    qt_add_shaders(
            sunroom_ffmpeg_first_frame_tests
            "sunroom_ffmpeg_first_frame_test_shaders"
            PRECOMPILE
            OPTIMIZED
            PREFIX
            /shaders
            BASE
            ../src/presentation/shaders
            FILES
            ../src/presentation/shaders/fullscreen.vert
            ../src/presentation/shaders/compositor.frag
    )
    sunroom_stage_windows_runtime(
            sunroom_ffmpeg_first_frame_tests
    )
    add_test(
            NAME ffmpeg-first-frame
            COMMAND sunroom_ffmpeg_first_frame_tests
    )
    set_tests_properties(
            ffmpeg-first-frame
            PROPERTIES
            LABELS "ffmpeg;gpu;integration;windows"
            TIMEOUT 30
    )
endif ()
