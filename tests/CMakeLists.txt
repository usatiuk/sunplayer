qt_add_executable(sunplayer_presentation_target_tests
        unit/presentation/tst_PresentationTarget.cpp
        ../src/presentation/PresentationTarget.cpp
        ../src/presentation/PresentationTarget.h
        ../src/platform/DisplayState.h
)

qt_add_executable(sunplayer_application_log_tests
        unit/diagnostics/tst_ApplicationLog.cpp
)

qt_add_executable(sunplayer_application_settings_tests
        unit/app/tst_ApplicationSettings.cpp
        ../src/app/ApplicationSettings.cpp
        ../src/app/ApplicationSettings.h
)

qt_add_executable(sunplayer_subtitle_settings_tests
        unit/subtitles/tst_SubtitleSettings.cpp
        ../src/subtitles/SubtitleSettings.cpp
        ../src/subtitles/SubtitleSettings.h
        ../src/subtitles/SubtitleAppearance.h
)

qt_add_executable(sunplayer_settings_dialog_tests
        unit/app/tst_SettingsDialog.cpp
        ../src/app/SettingsDialog.cpp
        ../src/app/SettingsDialog.h
        ../src/subtitles/SubtitleSettings.cpp
        ../src/subtitles/SubtitleSettings.h
        ../src/subtitles/SubtitleAppearance.h
)

qt_add_executable(sunplayer_support_tests
        unit/app/tst_Support.cpp
        ../src/app/ApplicationError.cpp
        ../src/app/ApplicationError.h
        ../src/app/SupportDiagnostics.cpp
        ../src/app/SupportDiagnostics.h
)

qt_add_executable(sunplayer_local_media_drop_tests
        unit/app/tst_LocalMediaDrop.cpp
        ../src/app/LocalMediaDrop.cpp
        ../src/app/LocalMediaDrop.h
)

qt_add_executable(sunplayer_window_shortcut_tests
        unit/app/tst_WindowShortcut.cpp
        ../src/app/WindowShortcut.h
)

qt_add_executable(sunplayer_controlled_audio_sink_tests
        unit/audio/tst_ControlledAudioSink.cpp
)

qt_add_executable(sunplayer_audio_output_ledger_tests
        unit/audio/tst_AudioOutputLedger.cpp
)

qt_add_executable(sunplayer_realtime_pcm_queue_tests
        unit/audio/tst_RealtimePcmQueue.cpp
)

qt_add_executable(sunplayer_ffmpeg_media_decoder_tests
        integration/media/tst_FfmpegMediaDecoder.cpp
)

qt_add_executable(sunplayer_rendered_video_surface_tests
        unit/video/tst_RenderedVideoSurface.cpp
        ../src/video/RenderedVideoSurface.cpp
        ../src/video/RenderedVideoSurface.h
        ../src/video/VideoTargetInterop.cpp
        ../src/video/VideoTargetInterop.h
)

qt_add_executable(sunplayer_video_viewport_tests
        unit/ui/tst_VideoViewportState.cpp
        ../src/app/VideoViewportState.cpp
        ../src/app/VideoViewportState.h
)

qt_add_executable(sunplayer_video_presentation_geometry_tests
        unit/presentation/tst_VideoPresentationGeometry.cpp
        ../src/presentation/VideoPresentationGeometry.cpp
        ../src/presentation/VideoPresentationGeometry.h
)

qt_add_executable(sunplayer_decoded_video_frame_tests
        unit/media/tst_DecodedVideoFrame.cpp
)

qt_add_executable(sunplayer_libplacebo_color_policy_tests
        unit/video/tst_LibplaceboColorPolicy.cpp
)

qt_add_executable(sunplayer_ffmpeg_video_timeline_tests
        unit/media/tst_FfmpegVideoTimeline.cpp
)

qt_add_executable(sunplayer_ffmpeg_packet_router_tests
        unit/media/tst_FfmpegPacketRouter.cpp
)

qt_add_executable(sunplayer_subtitle_source_tests
        unit/subtitles/tst_SubtitleSource.cpp
)

qt_add_executable(sunplayer_media_session_tests
        unit/playback/tst_MediaSession.cpp
        ../src/playback/CoalescedGenerationWake.h
        ../src/playback/MediaTrackModel.cpp
        ../src/playback/MediaTrackModel.h
        ../src/playback/MediaSession.cpp
        ../src/playback/MediaSession.h
        ../src/playback/VideoFrameQueue.cpp
        ../src/playback/VideoFrameQueue.h
        ../src/playback/VideoFrameScheduler.cpp
        ../src/playback/VideoFrameScheduler.h
        ../src/platform/PlaybackPowerInhibitor.cpp
        ../src/platform/PlaybackPowerInhibitor.h
)

qt_add_executable(sunplayer_video_frame_queue_tests
        unit/playback/tst_VideoFrameQueue.cpp
        ../src/playback/CoalescedGenerationWake.h
        ../src/playback/VideoFrameQueue.cpp
        ../src/playback/VideoFrameQueue.h
        ../src/playback/VideoFrameScheduler.cpp
        ../src/playback/VideoFrameScheduler.h
)

qt_add_executable(sunplayer_active_video_source_tests
        unit/video/tst_ActiveVideoSource.cpp
        ../src/video/ActiveVideoSource.cpp
        ../src/video/ActiveVideoSource.h
)

qt_add_executable(sunplayer_qml_shell_tests
        integration/ui/tst_AppShell.cpp
        integration/ui/QmlShellTestTypes.h
        ../src/jellyfin/JellyfinImages.cpp
        ../src/jellyfin/JellyfinImages.h
        ../src/jellyfin/JellyfinBrowser.cpp
        ../src/jellyfin/JellyfinBrowser.h
        ../src/app/VideoViewportState.cpp
        ../src/app/VideoViewportState.h
)

qt_add_executable(sunplayer_cubeb_dependency_tests
        integration/audio/tst_CubebDependency.cpp
)

qt_add_executable(sunplayer_ffmpeg_dependency_tests
        integration/video/tst_FfmpegDependency.cpp
)

qt_add_executable(sunplayer_libplacebo_dependency_tests
        integration/video/tst_LibplaceboDependency.cpp
)

qt_add_executable(sunplayer_libass_dependency_tests
        integration/subtitles/tst_LibassDependency.cpp
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    qt_add_executable(sunplayer_linux_platform_dependency_tests
            integration/platform/tst_LinuxPlatformDependencies.cpp
    )
    qt_add_executable(sunplayer_wayland_color_management_tests
            unit/platform/tst_WaylandColorManagement.cpp
    )
endif ()

qt_add_executable(sunplayer_cubeb_audio_sink_tests
        integration/audio/tst_CubebAudioSink.cpp
)

set_source_files_properties(../src/app/Main.qml PROPERTIES
        QT_RESOURCE_ALIAS Main.qml
        QT_QML_SKIP_CACHEGEN TRUE
)
set_source_files_properties(../src/app/AppScrollBar.qml PROPERTIES
        QT_RESOURCE_ALIAS AppScrollBar.qml
        QT_QML_SKIP_CACHEGEN TRUE
)
set_source_files_properties(../src/app/AppShell.qml PROPERTIES
        QT_RESOURCE_ALIAS AppShell.qml
        QT_QML_SKIP_CACHEGEN TRUE
)
set_source_files_properties(
        ../src/app/windowchrome/ClientSideWindowChrome.qml
        PROPERTIES
        QT_RESOURCE_ALIAS ClientSideWindowChrome.qml
        QT_QML_SKIP_CACHEGEN TRUE
)
set_source_files_properties(../src/app/pages/HdrLabPage.qml PROPERTIES
        QT_RESOURCE_ALIAS HdrLabPage.qml
        QT_QML_SKIP_CACHEGEN TRUE
)
set_source_files_properties(../src/app/pages/PlayerPage.qml PROPERTIES
        QT_RESOURCE_ALIAS PlayerPage.qml
        QT_QML_SKIP_CACHEGEN TRUE
)
set_source_files_properties(../src/app/pages/VideoPage.qml PROPERTIES
        QT_RESOURCE_ALIAS VideoPage.qml
        QT_QML_SKIP_CACHEGEN TRUE
)
set_source_files_properties(../src/app/icons/lucide/play.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/play.svg
)
set_source_files_properties(../src/app/icons/lucide/pause.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/pause.svg
)
set_source_files_properties(../src/app/icons/lucide/rotate-ccw.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/rotate-ccw.svg
)
set_source_files_properties(../src/app/icons/lucide/rotate-cw.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/rotate-cw.svg
)
set_source_files_properties(../src/app/icons/lucide/volume-2.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/volume-2.svg
)
set_source_files_properties(../src/app/icons/lucide/volume-x.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/volume-x.svg
)
set_source_files_properties(../src/app/icons/lucide/ellipsis.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/ellipsis.svg
)
set_source_files_properties(../src/app/icons/lucide/x.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/x.svg
)
set_source_files_properties(../src/app/icons/lucide/minus.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/minus.svg
)
set_source_files_properties(../src/app/icons/lucide/square.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/square.svg
)
set_source_files_properties(../src/app/icons/lucide/copy.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/copy.svg
)
set_source_files_properties(../src/app/icons/lucide/LICENSE PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/LICENSE
)

foreach(_jellyfin_component JellyfinPage JellyfinPoster JellyfinShelf JellyfinDetail JellyfinEpisode)
    set_source_files_properties(../src/app/pages/${_jellyfin_component}.qml PROPERTIES
            QT_RESOURCE_ALIAS ${_jellyfin_component}.qml
            QT_QML_SKIP_CACHEGEN TRUE
    )
endforeach()
qt_add_qml_module(sunplayer_qml_shell_tests
        URI SunPlayerShellTest
        VERSION 1.0
        QML_FILES
        ../src/app/Main.qml
        ../src/app/AppShell.qml
        ../src/app/windowchrome/ClientSideWindowChrome.qml
        ../src/app/pages/HdrLabPage.qml
        ../src/app/pages/JellyfinPage.qml
        ../src/app/pages/JellyfinPoster.qml
        ../src/app/AppScrollBar.qml
        ../src/app/pages/JellyfinShelf.qml
        ../src/app/pages/JellyfinDetail.qml
        ../src/app/pages/JellyfinEpisode.qml
        ../src/app/pages/PlayerPage.qml
        ../src/app/pages/VideoPage.qml
        RESOURCES
        ../src/app/icons/lucide/play.svg
        ../src/app/icons/lucide/pause.svg
        ../src/app/icons/lucide/rotate-ccw.svg
        ../src/app/icons/lucide/rotate-cw.svg
        ../src/app/icons/lucide/volume-2.svg
        ../src/app/icons/lucide/volume-x.svg
        ../src/app/icons/lucide/ellipsis.svg
        ../src/app/icons/lucide/x.svg
        ../src/app/icons/lucide/minus.svg
        ../src/app/icons/lucide/square.svg
        ../src/app/icons/lucide/copy.svg
        ../src/app/icons/lucide/LICENSE
)

target_include_directories(sunplayer_presentation_target_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_application_log_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_application_settings_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_subtitle_settings_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_settings_dialog_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_support_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_local_media_drop_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_window_shortcut_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_controlled_audio_sink_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_ffmpeg_media_decoder_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_rendered_video_surface_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_video_viewport_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_decoded_video_frame_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_ffmpeg_video_timeline_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(
        sunplayer_video_presentation_geometry_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_media_session_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_subtitle_source_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_video_frame_queue_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_active_video_source_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_include_directories(sunplayer_qml_shell_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/jellyfin"
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/app"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/ui"
)

target_compile_definitions(
        sunplayer_qml_shell_tests
        PRIVATE
        SUNPLAYER_QT_QML_IMPORT_PATH="${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}"
        SUNPLAYER_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        SUNPLAYER_UI_SNAPSHOT_DIR="${CMAKE_CURRENT_BINARY_DIR}/ui-snapshots"
)

target_link_libraries(sunplayer_presentation_target_tests
        PRIVATE
        Qt6::Test
)

target_link_libraries(sunplayer_application_log_tests
        PRIVATE
        Qt6::Test
        sunplayer_diagnostics
)

target_link_libraries(sunplayer_application_settings_tests
        PRIVATE
        sunplayer_settings
        Qt6::Gui
        Qt6::Test
        sunplayer_diagnostics
)

target_link_libraries(sunplayer_subtitle_settings_tests
        PRIVATE
        Qt6::Gui
        Qt6::Qml
        Qt6::Test
)

target_link_libraries(sunplayer_settings_dialog_tests
        PRIVATE
        Qt6::Test
        Qt6::Widgets
)

target_link_libraries(sunplayer_support_tests
        PRIVATE
        Qt6::Test
)

target_link_libraries(sunplayer_local_media_drop_tests
        PRIVATE
        Qt6::Test
)

target_link_libraries(sunplayer_window_shortcut_tests
        PRIVATE
        Qt6::Gui
        Qt6::Test
)

target_link_libraries(sunplayer_controlled_audio_sink_tests
        PRIVATE
        Qt6::Test
        sunplayer_audio
)

target_link_libraries(sunplayer_audio_output_ledger_tests
        PRIVATE
        Qt6::Test
        sunplayer_audio
)

target_link_libraries(sunplayer_realtime_pcm_queue_tests
        PRIVATE
        Qt6::Test
        sunplayer_audio
)

target_compile_definitions(
        sunplayer_ffmpeg_media_decoder_tests
        PRIVATE
        SUNPLAYER_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(sunplayer_ffmpeg_media_decoder_tests
        PRIVATE
        Qt6::Test
        sunplayer_media
        sunplayer_ffmpeg
)

target_link_libraries(sunplayer_rendered_video_surface_tests
        PRIVATE
        Qt6::Test
)

target_link_libraries(sunplayer_video_viewport_tests
        PRIVATE
        Qt6::Test
)

target_link_libraries(sunplayer_decoded_video_frame_tests
        PRIVATE
        Qt6::Test
        sunplayer_media
        sunplayer_ffmpeg
)

target_link_libraries(sunplayer_libplacebo_color_policy_tests
        PRIVATE
        Qt6::Test
        sunplayer_video_pipeline
)

target_link_libraries(sunplayer_ffmpeg_video_timeline_tests
        PRIVATE
        Qt6::Test
        sunplayer_media
        sunplayer_ffmpeg
)

target_link_libraries(sunplayer_ffmpeg_packet_router_tests
        PRIVATE
        Qt6::Test
        sunplayer_media
        sunplayer_ffmpeg
)

target_link_libraries(sunplayer_subtitle_source_tests
        PRIVATE
        Qt6::Test
        sunplayer_media
)

target_link_libraries(
        sunplayer_video_presentation_geometry_tests
        PRIVATE
        Qt6::Test
)

target_compile_definitions(
        sunplayer_media_session_tests
        PRIVATE
        SUNPLAYER_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(sunplayer_media_session_tests
        PRIVATE
        Qt6::Quick
        Qt6::Test
        sunplayer_video_pipeline
)

target_link_libraries(sunplayer_video_frame_queue_tests
        PRIVATE
        Qt6::Test
        sunplayer_media
        sunplayer_ffmpeg
)

target_link_libraries(sunplayer_active_video_source_tests
        PRIVATE
        Qt6::Quick
        Qt6::Test
        sunplayer_video_pipeline
)

target_link_libraries(sunplayer_qml_shell_tests
        PRIVATE sunplayer_jellyfin
        PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickDialogs2
        Qt6::Test
)

target_link_libraries(sunplayer_cubeb_dependency_tests
        PRIVATE
        Qt6::Test
        cubeb::cubeb
)

target_link_libraries(sunplayer_ffmpeg_dependency_tests
        PRIVATE
        Qt6::Test
        sunplayer_ffmpeg
)

target_link_libraries(sunplayer_libplacebo_dependency_tests
        PRIVATE
        Qt6::Test
        libplacebo::libplacebo
)

target_link_libraries(sunplayer_libass_dependency_tests
        PRIVATE
        Qt6::Test
        sunplayer_libass
)
target_compile_definitions(
        sunplayer_libass_dependency_tests
        PRIVATE
        SUNPLAYER_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(sunplayer_linux_platform_dependency_tests
            PRIVATE
            Qt6::Test
            Qt6::WaylandClientPrivate
            Vulkan::Vulkan
            Wayland::Client
            PkgConfig::VAAPI
            PkgConfig::DRM
            sunplayer_linux_platform
            libplacebo::libplacebo
    )
    target_link_libraries(sunplayer_wayland_color_management_tests
            PRIVATE
            Qt6::Test
            sunplayer_linux_platform
            libplacebo::libplacebo
    )
endif ()

target_link_libraries(sunplayer_cubeb_audio_sink_tests
        PRIVATE
        Qt6::Test
        sunplayer_audio
)

if (WIN32)
    foreach (test_target IN ITEMS
            sunplayer_presentation_target_tests
            sunplayer_application_log_tests
            sunplayer_application_settings_tests
            sunplayer_subtitle_settings_tests
            sunplayer_settings_dialog_tests
            sunplayer_support_tests
            sunplayer_local_media_drop_tests
            sunplayer_controlled_audio_sink_tests
            sunplayer_audio_output_ledger_tests
            sunplayer_realtime_pcm_queue_tests
            sunplayer_ffmpeg_media_decoder_tests
            sunplayer_rendered_video_surface_tests
            sunplayer_video_viewport_tests
            sunplayer_video_presentation_geometry_tests
            sunplayer_libplacebo_color_policy_tests
            sunplayer_ffmpeg_video_timeline_tests
            sunplayer_ffmpeg_packet_router_tests
            sunplayer_subtitle_source_tests
            sunplayer_media_session_tests
            sunplayer_video_frame_queue_tests
            sunplayer_active_video_source_tests
            sunplayer_cubeb_dependency_tests
            sunplayer_cubeb_audio_sink_tests
            sunplayer_qml_shell_tests
            sunplayer_libplacebo_dependency_tests
            sunplayer_libass_dependency_tests)
        sunplayer_stage_windows_runtime(${test_target})
    endforeach ()
    sunplayer_stage_windows_runtime(
            sunplayer_decoded_video_frame_tests
    )
    sunplayer_stage_windows_runtime(
            sunplayer_ffmpeg_dependency_tests
    )
    sunplayer_stage_qt_platform_plugin(
            sunplayer_qml_shell_tests
            Qt6::QOffscreenIntegrationPlugin
    )
endif ()

add_test(
        NAME presentation-target
        COMMAND sunplayer_presentation_target_tests
)

add_test(
        NAME application-log
        COMMAND sunplayer_application_log_tests
)

add_test(
        NAME application-settings
        COMMAND sunplayer_application_settings_tests
)

add_test(
        NAME subtitle-settings
        COMMAND sunplayer_subtitle_settings_tests
)

add_test(
        NAME settings-dialog
        COMMAND sunplayer_settings_dialog_tests
)

add_test(
        NAME support
        COMMAND sunplayer_support_tests
)

add_test(
        NAME local-media-drop
        COMMAND sunplayer_local_media_drop_tests
)

add_test(
        NAME window-shortcut
        COMMAND sunplayer_window_shortcut_tests
)

add_test(
        NAME controlled-audio-sink
        COMMAND sunplayer_controlled_audio_sink_tests
)

add_test(
        NAME audio-output-ledger
        COMMAND sunplayer_audio_output_ledger_tests
)

add_test(
        NAME realtime-pcm-queue
        COMMAND sunplayer_realtime_pcm_queue_tests
)

add_test(
        NAME ffmpeg-media-decoder
        COMMAND sunplayer_ffmpeg_media_decoder_tests
)

add_test(
        NAME rendered-video-surface
        COMMAND sunplayer_rendered_video_surface_tests
)

add_test(
        NAME video-viewport
        COMMAND sunplayer_video_viewport_tests
)

add_test(
        NAME decoded-video-frame
        COMMAND sunplayer_decoded_video_frame_tests
)

add_test(
        NAME libplacebo-color-policy
        COMMAND sunplayer_libplacebo_color_policy_tests
)

add_test(
        NAME ffmpeg-video-timeline
        COMMAND sunplayer_ffmpeg_video_timeline_tests
)

add_test(
        NAME ffmpeg-packet-router
        COMMAND sunplayer_ffmpeg_packet_router_tests
)

add_test(
        NAME subtitle-source
        COMMAND sunplayer_subtitle_source_tests
)

add_test(
        NAME video-presentation-geometry
        COMMAND sunplayer_video_presentation_geometry_tests
)

add_test(
        NAME media-session
        COMMAND sunplayer_media_session_tests
)

add_test(
        NAME video-frame-queue
        COMMAND sunplayer_video_frame_queue_tests
)

add_test(
        NAME active-video-source
        COMMAND sunplayer_active_video_source_tests
)

add_test(
        NAME qml-shell
        COMMAND sunplayer_qml_shell_tests
)
add_test(
        NAME qml-shell-fusion
        COMMAND sunplayer_qml_shell_tests
)

if (WIN32)
    set(sunplayer_test_platform_label windows)
elseif (APPLE)
    set(sunplayer_test_platform_label macos)
else ()
    set(sunplayer_test_platform_label linux)
endif ()

add_test(
        NAME cubeb-dependency
        COMMAND sunplayer_cubeb_dependency_tests
)
set_tests_properties(
        cubeb-dependency
        PROPERTIES
        LABELS
            "audio;dependency;integration;${sunplayer_test_platform_label}"
        TIMEOUT 30
)

add_test(
        NAME cubeb-audio-sink
        COMMAND sunplayer_cubeb_audio_sink_tests
)
set_tests_properties(
        cubeb-audio-sink
        PROPERTIES
        LABELS "audio;device;integration;${sunplayer_test_platform_label}"
        TIMEOUT 30
)

add_test(
        NAME application-audio-first-playback
        COMMAND $<TARGET_FILE:sunplayer>
                --playback-smoke
                --no-log-file
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/media/sdr-bt709-ffv1-video-late-flac.mkv"
)
set_tests_properties(
        application-audio-first-playback
        PROPERTIES
        LABELS "application;audio;device;ffmpeg;gpu;integration;${sunplayer_test_platform_label}"
        TIMEOUT 30
)

add_test(
        NAME application-rejects-multiple-media
        COMMAND $<TARGET_FILE:sunplayer>
                --no-log-file
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/media/sdr-bt709-ffv1-video-late-flac.mkv"
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/media/sdr-bt709-ffv1-video-late-flac.mkv"
)
set_tests_properties(
        application-rejects-multiple-media
        PROPERTIES
        LABELS "application;integration;${sunplayer_test_platform_label}"
        TIMEOUT 30
        PASS_REGULAR_EXPRESSION "SunPlayer accepts at most one positional media file[.]"
)

if (WIN32)
    add_test(
            NAME application-initial-background
            COMMAND $<TARGET_FILE:sunplayer>
                    --verify-initial-background
                    --no-log-file
    )
    set_tests_properties(
            application-initial-background
            PROPERTIES
            LABELS "application;ui;gpu;integration;windows"
            TIMEOUT 30
    )

    add_test(
            NAME application-fullscreen
            COMMAND $<TARGET_FILE:sunplayer>
                    --fullscreen-smoke
                    --no-log-file
                    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/media/sdr-bt709-ffv1-video-late-flac.mkv"
    )
    set_tests_properties(
            application-fullscreen
            PROPERTIES
            LABELS "application;audio;device;ui;gpu;integration;windows"
            TIMEOUT 30
    )

    add_test(
            NAME application-file-drop
            COMMAND $<TARGET_FILE:sunplayer>
                    --file-drop-smoke
                    --no-log-file
                    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/media/sdr-bt709-ffv1-video-late-flac.mkv"
    )
    set_tests_properties(
            application-file-drop
            PROPERTIES
            LABELS "application;ui;gpu;integration;windows"
            TIMEOUT 30
    )
endif ()

set_tests_properties(
        presentation-target
        application-log
        application-settings
        subtitle-settings
        settings-dialog
        local-media-drop
        rendered-video-surface
        video-viewport
        video-presentation-geometry
        decoded-video-frame
        libplacebo-color-policy
        ffmpeg-video-timeline
        video-frame-queue
        active-video-source
        subtitle-source
        PROPERTIES LABELS "unit"
)

set_tests_properties(
        controlled-audio-sink
        PROPERTIES
        LABELS "audio;concurrency;unit"
        TIMEOUT 30
)

set_tests_properties(
        audio-output-ledger
        PROPERTIES
        LABELS "audio;unit"
        TIMEOUT 30
)

set_tests_properties(
        realtime-pcm-queue
        PROPERTIES
        LABELS "audio;concurrency;unit"
        TIMEOUT 30
)

set_tests_properties(
        ffmpeg-media-decoder
        PROPERTIES
        LABELS "audio;integration;media"
        TIMEOUT 30
)

set_tests_properties(
        ffmpeg-packet-router
        PROPERTIES
        LABELS "concurrency;media;unit"
        TIMEOUT 30
)

set_tests_properties(
        media-session
        PROPERTIES
        LABELS "concurrency;integration;media"
        TIMEOUT 30
)

set_tests_properties(
        video-frame-queue
        PROPERTIES
        LABELS "concurrency;unit"
        TIMEOUT 30
)

set_tests_properties(
        qml-shell
        qml-shell-fusion
        PROPERTIES
        LABELS "component;ui"
        TIMEOUT 60
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
)
set_tests_properties(qml-shell PROPERTIES ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic")
set_tests_properties(qml-shell-fusion PROPERTIES ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Fusion")
if (WIN32)
    add_test(NAME qml-shell-windows COMMAND sunplayer_qml_shell_tests)
    set_tests_properties(qml-shell-windows PROPERTIES
        LABELS "component;ui"
        TIMEOUT 60
        ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=FluentWinUI3"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
    )
endif ()

add_test(
        NAME ffmpeg-dependency
        COMMAND sunplayer_ffmpeg_dependency_tests
)
set_tests_properties(
        ffmpeg-dependency
        PROPERTIES
        LABELS
            "dependency;integration;${sunplayer_test_platform_label}"
        TIMEOUT 30
)

add_test(
        NAME libplacebo-dependency
        COMMAND sunplayer_libplacebo_dependency_tests
)
set_tests_properties(
        libplacebo-dependency
        PROPERTIES
        LABELS
            "dependency;integration;${sunplayer_test_platform_label}"
        TIMEOUT 30
)

add_test(
        NAME libass-dependency
        COMMAND sunplayer_libass_dependency_tests
)
set_tests_properties(
        libass-dependency
        PROPERTIES
        LABELS
            "dependency;subtitles;integration;${sunplayer_test_platform_label}"
        TIMEOUT 30
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 REQUIRED COMPONENTS WaylandCompositor)
    pkg_check_modules(SUNPLAYER_WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
    qt_add_executable(sunplayer_wayland_shadow_tests
            integration/platform/tst_WaylandWindowShadow.cpp
    )
    target_link_libraries(sunplayer_wayland_shadow_tests PRIVATE
            Qt6::Test Qt6::WaylandCompositor PkgConfig::SUNPLAYER_WAYLAND_SERVER sunplayer_linux_platform)
    add_test(NAME wayland-window-shadow COMMAND sunplayer_wayland_shadow_tests)
    set_tests_properties(wayland-window-shadow PROPERTIES
            LABELS "platform;integration;linux" TIMEOUT 45)
    add_test(
            NAME wayland-color-management
            COMMAND sunplayer_wayland_color_management_tests
    )
    set_tests_properties(
            wayland-color-management
            PROPERTIES
            LABELS "color;platform;unit;linux"
            TIMEOUT 30
    )
    add_test(
            NAME linux-platform-dependencies
            COMMAND sunplayer_linux_platform_dependency_tests
    )
    set_tests_properties(
            linux-platform-dependencies
            PROPERTIES
            LABELS "dependency;integration;linux"
            TIMEOUT 30
    )
endif ()

add_test(
        NAME packaged-qml-module
        COMMAND $<TARGET_FILE:sunplayer> --verify-qml
)

set_tests_properties(
        packaged-qml-module
        PROPERTIES
        LABELS "application;integration;ui"
        TIMEOUT 30
)

if (WIN32)
    qt_add_executable(sunplayer_qrhi_compositor_tests
            integration/presentation/tst_QrhiCompositor.cpp
    )

    target_include_directories(sunplayer_qrhi_compositor_tests
            PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    )

    target_link_libraries(sunplayer_qrhi_compositor_tests
            PRIVATE
            Qt6::GuiPrivate
            Qt6::Quick
            Qt6::Test
            sunplayer_video_pipeline
    )

    sunplayer_add_compositor_shader(sunplayer_qrhi_compositor_tests)
    qt_add_shaders(
            sunplayer_qrhi_compositor_tests
            "sunplayer_qrhi_compositor_test_shaders"
            ${sunplayer_shader_options}
            PREFIX
            /shaders
            BASE
            ../src/presentation/shaders
            FILES
            ../src/presentation/shaders/fullscreen.vert
            ../src/presentation/shaders/diagnostic_video.frag
    )

    sunplayer_stage_windows_runtime(
            sunplayer_qrhi_compositor_tests
    )

    add_test(
            NAME qrhi-compositor
            COMMAND sunplayer_qrhi_compositor_tests
    )
    set_tests_properties(
            qrhi-compositor
            PROPERTIES
            LABELS "gpu;windows"
            TIMEOUT 30
    )

endif ()

if (WIN32 OR APPLE)
    qt_add_executable(sunplayer_subtitle_renderer_tests
            integration/subtitles/tst_SubtitleRenderer.cpp
    )
    target_include_directories(sunplayer_subtitle_renderer_tests
            PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    )
    target_compile_definitions(sunplayer_subtitle_renderer_tests
            PRIVATE
            SUNPLAYER_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    target_link_libraries(sunplayer_subtitle_renderer_tests
            PRIVATE
            Qt6::GuiPrivate
            Qt6::Quick
            Qt6::Test
            sunplayer_video_pipeline
    )
    if (WIN32)
        sunplayer_stage_windows_runtime(
                sunplayer_subtitle_renderer_tests
        )
    endif ()
    add_test(
            NAME subtitle-renderer
            COMMAND sunplayer_subtitle_renderer_tests
    )
    set_tests_properties(
            subtitle-renderer
            PROPERTIES
            LABELS
                "gpu;subtitles;integration;${sunplayer_test_platform_label}"
            TIMEOUT 30
    )
endif ()

if (WIN32 OR APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")

    qt_add_executable(sunplayer_ffmpeg_first_frame_tests
            integration/video/tst_FfmpegFirstFrame.cpp
    )
    target_include_directories(
            sunplayer_ffmpeg_first_frame_tests
            PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    )
    target_compile_definitions(
            sunplayer_ffmpeg_first_frame_tests
            PRIVATE
            SUNPLAYER_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    target_link_libraries(
            sunplayer_ffmpeg_first_frame_tests
            PRIVATE
            Qt6::GuiPrivate
            Qt6::Quick
            Qt6::Test
            sunplayer_video_pipeline
    )
    sunplayer_add_compositor_shader(sunplayer_ffmpeg_first_frame_tests)
    qt_add_shaders(
            sunplayer_ffmpeg_first_frame_tests
            "sunplayer_ffmpeg_first_frame_test_shaders"
            ${sunplayer_shader_options}
            PREFIX
            /shaders
            BASE
            ../src/presentation/shaders
            FILES
            ../src/presentation/shaders/fullscreen.vert
    )
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(sunplayer_ffmpeg_first_frame_tests PRIVATE ../src/platform/DisplayStateProvider.h)
        target_link_libraries(sunplayer_ffmpeg_first_frame_tests PRIVATE sunplayer_linux_platform)
    endif ()
    if (WIN32)
        sunplayer_stage_windows_runtime(
                sunplayer_ffmpeg_first_frame_tests
        )
    endif ()
    add_test(
            NAME ffmpeg-first-frame
            COMMAND sunplayer_ffmpeg_first_frame_tests
    )
    set_tests_properties(
            ffmpeg-first-frame
            PROPERTIES
            LABELS
                "ffmpeg;gpu;hardware-decode;integration;${sunplayer_test_platform_label}"
            TIMEOUT 120
    )
    if (WIN32)
        set_tests_properties(
                ffmpeg-first-frame
                PROPERTIES ENVIRONMENT "SUNPLAYER_REQUIRE_D3D11VA=1"
        )
    elseif (APPLE)
        set_tests_properties(
                ffmpeg-first-frame
                PROPERTIES ENVIRONMENT "SUNPLAYER_REQUIRE_VIDEOTOOLBOX=1"
        )
    endif ()
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Exercise the production session through real calls on an isolated D-Bus.
    get_target_property(sunplayer_mpris_session_sources sunplayer_media_session_tests SOURCES)
    list(FILTER sunplayer_mpris_session_sources EXCLUDE REGEX "tst_MediaSession[.]cpp$")
    qt_add_executable(sunplayer_linux_native_media_controls_tests
            integration/platform/tst_LinuxNativeMediaControls.cpp
            ../src/platform/linux/LinuxNativeMediaControls.cpp
            ${sunplayer_mpris_session_sources})
    target_include_directories(sunplayer_linux_native_media_controls_tests PRIVATE ../src)
    target_compile_definitions(sunplayer_linux_native_media_controls_tests PRIVATE
            SUNPLAYER_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    target_link_libraries(sunplayer_linux_native_media_controls_tests PRIVATE
            Qt6::Test Qt6::Quick Qt6::DBus sunplayer_video_pipeline)
    find_program(SUNPLAYER_DBUS_RUN_SESSION dbus-run-session REQUIRED)
    add_test(NAME linux-native-media-controls
            COMMAND "${SUNPLAYER_DBUS_RUN_SESSION}" -- $<TARGET_FILE:sunplayer_linux_native_media_controls_tests>)
    set_tests_properties(linux-native-media-controls PROPERTIES
            LABELS "platform;media;integration;linux" TIMEOUT 30
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    qt_add_executable(sunplayer_appimage_update_ui_tests
            integration/platform/tst_AppImageUpdateUi.cpp
            ../src/platform/linux/AppImageUpdateUi.cpp
            ../src/platform/linux/AppImageUpdateUi.h
            ../src/platform/linux/AppImageUpdater.cpp
            ../src/platform/linux/AppImageUpdater.h)
    target_include_directories(sunplayer_appimage_update_ui_tests PRIVATE ../src)
    target_link_libraries(sunplayer_appimage_update_ui_tests PRIVATE Qt6::Widgets Qt6::Test sunplayer_settings)
    add_test(NAME appimage-update-ui COMMAND sunplayer_appimage_update_ui_tests)
    set_tests_properties(appimage-update-ui PROPERTIES LABELS "platform;ui;integration;linux"
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    qt_add_executable(sunplayer_appimage_updater_tests
            integration/platform/tst_AppImageUpdater.cpp
            ../src/platform/linux/AppImageUpdater.cpp
            ../src/platform/linux/AppImageUpdater.h)
    target_include_directories(sunplayer_appimage_updater_tests PRIVATE ../src)
    target_link_libraries(sunplayer_appimage_updater_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME appimage-updater COMMAND sunplayer_appimage_updater_tests)
    set_tests_properties(appimage-updater PROPERTIES LABELS "platform;packaging;integration;linux" TIMEOUT 30)
endif ()

qt_add_executable(sunplayer_jellyfin_tests
        integration/jellyfin/tst_Jellyfin.cpp
        ../src/jellyfin/JellyfinBrowser.cpp
        ../src/jellyfin/JellyfinBrowser.h
        ../src/jellyfin/JellyfinPlaybackAdapter.cpp
        ../src/jellyfin/JellyfinPlaybackAdapter.h
        ../src/playback/MediaSession.cpp
        ../src/playback/MediaSession.h
        ../src/playback/MediaTrackModel.cpp
        ../src/playback/MediaTrackModel.h
        ../src/playback/VideoFrameQueue.cpp
        ../src/playback/VideoFrameScheduler.cpp
)
target_link_libraries(sunplayer_jellyfin_tests PRIVATE Qt6::Test Qt6::Quick sunplayer_jellyfin sunplayer_video_pipeline)
target_compile_definitions(sunplayer_jellyfin_tests PRIVATE SUNPLAYER_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME jellyfin COMMAND sunplayer_jellyfin_tests)
set_tests_properties(jellyfin PROPERTIES LABELS "integration;media;network" TIMEOUT 60)

if (WIN32)
    qt_add_executable(sunplayer_windows_token_store_tests integration/jellyfin/tst_WindowsTokenStore.cpp)
    target_link_libraries(sunplayer_windows_token_store_tests PRIVATE Qt6::Test sunplayer_jellyfin)
    add_test(NAME windows-token-store COMMAND sunplayer_windows_token_store_tests)
    set_tests_properties(windows-token-store PROPERTIES
        LABELS "integration;windows"
        TIMEOUT 30
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
    )
endif ()

# Keep Qt Test output in CTest's captured streams, including without a Windows console.
get_property(sunplayer_registered_tests DIRECTORY PROPERTY TESTS)
set_property(TEST ${sunplayer_registered_tests} APPEND PROPERTY ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
