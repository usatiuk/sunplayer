cmake_minimum_required(VERSION 3.22)

if ((CMAKE_HOST_WIN32 OR CMAKE_HOST_APPLE)
        AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(_sunplayer_vcpkg_toolchain)

    if (DEFINED ENV{VCPKG_ROOT})
        set(_sunplayer_vcpkg_toolchain
                "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    elseif (CMAKE_HOST_WIN32 AND DEFINED ENV{VCToolsInstallDir})
        get_filename_component(
                _sunplayer_vc_root
                "$ENV{VCToolsInstallDir}/../../.."
                ABSOLUTE
        )
        set(_sunplayer_vcpkg_toolchain
                "${_sunplayer_vc_root}/vcpkg/scripts/buildsystems/vcpkg.cmake")
        unset(_sunplayer_vc_root)
    endif ()

    if (_sunplayer_vcpkg_toolchain
            AND EXISTS "${_sunplayer_vcpkg_toolchain}")
        set(
                CMAKE_TOOLCHAIN_FILE
                "${_sunplayer_vcpkg_toolchain}"
                CACHE FILEPATH
                "Toolchain used to configure SunPlayer"
        )
    endif ()

    unset(_sunplayer_vcpkg_toolchain)
endif ()

if (CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(
            VCPKG_TARGET_TRIPLET
            x64-windows-clangcl
            CACHE STRING
            "SunPlayer dependency target triplet"
    )
endif ()

if (CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_HOST_TRIPLET)
    set(
            VCPKG_HOST_TRIPLET
            x64-windows
            CACHE STRING
            "SunPlayer dependency host-tool triplet"
    )
endif ()

if (CMAKE_HOST_APPLE AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(
            VCPKG_TARGET_TRIPLET
            arm64-osx
            CACHE STRING
            "SunPlayer dependency target triplet"
    )
endif ()

if (CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(
            CMAKE_OSX_DEPLOYMENT_TARGET
            13.0
            CACHE STRING
            "Minimum supported macOS version"
    )
endif ()

project(SunPlayer VERSION 0.1.0 LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

if ((WIN32 OR APPLE) AND NOT VCPKG_MANIFEST_MODE)
    message(FATAL_ERROR
            "The Windows and macOS builds require vcpkg manifest mode. Set "
            "VCPKG_ROOT or configure with the vcpkg CMake toolchain.")
endif ()

include(cmake/SunPlayerFFmpeg.cmake)
sunplayer_configure_ffmpeg()
include(cmake/SunPlayerLibass.cmake)
sunplayer_configure_libass()

if (WIN32)
    find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS
            GuiPrivate
            Quick
            QuickControls2
            QuickDialogs2
            ShaderTools
    )
    find_package(libplacebo 7.360.1 EXACT CONFIG REQUIRED)
    find_package(cubeb CONFIG REQUIRED)
    set(sunplayer_qt_minimum_version 6.11.1)

    function(sunplayer_stage_windows_runtime target)
        add_custom_command(
                TARGET ${target}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_RUNTIME_DLLS:${target}>
                        $<TARGET_FILE_DIR:${target}>
                COMMAND_EXPAND_LISTS
                VERBATIM
        )
    endfunction()

    function(sunplayer_stage_qt_platform_plugin target plugin_target)
        add_custom_command(
                TARGET ${target}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                        "$<TARGET_FILE_DIR:${target}>/platforms"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:${plugin_target}>"
                        "$<TARGET_FILE_DIR:${target}>/platforms/"
                VERBATIM
        )
    endfunction()
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SUNPLAYER_LIBPLACEBO REQUIRED
            IMPORTED_TARGET
            GLOBAL
            libplacebo>=7.360
            libplacebo<7.361
    )
    add_library(libplacebo::libplacebo
            ALIAS PkgConfig::SUNPLAYER_LIBPLACEBO)

    find_package(Qt6 6.10...<6.11 REQUIRED COMPONENTS
            GuiPrivate
            Quick
            QuickControls2
            QuickDialogs2
            ShaderTools
            DBus
            WaylandClientPrivate
            WaylandScannerTools
    )
    find_package(Vulkan 1.3 REQUIRED)
    pkg_check_modules(SUNPLAYER_WAYLAND_PROTOCOLS REQUIRED
            wayland-protocols
    )
    pkg_get_variable(
            SUNPLAYER_WAYLAND_PROTOCOLS_DIR
            wayland-protocols
            pkgdatadir
    )
    set(SUNPLAYER_WAYLAND_COLOR_MANAGEMENT_PROTOCOL
            "${SUNPLAYER_WAYLAND_PROTOCOLS_DIR}/staging/color-management/color-management-v1.xml")
    if (NOT EXISTS "${SUNPLAYER_WAYLAND_COLOR_MANAGEMENT_PROTOCOL}")
        message(FATAL_ERROR
                "The system wayland-protocols package does not provide "
                "staging/color-management/color-management-v1.xml")
    endif ()
    pkg_check_modules(VAAPI REQUIRED
            IMPORTED_TARGET
            GLOBAL
            libva-drm
    )
    pkg_check_modules(DRM REQUIRED
            IMPORTED_TARGET
            GLOBAL
            libdrm
    )
    find_package(cubeb CONFIG REQUIRED)
    set(sunplayer_qt_minimum_version 6.10)
elseif (APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SUNPLAYER_LIBPLACEBO REQUIRED
            IMPORTED_TARGET
            GLOBAL
            libplacebo=7.360.1
    )
    add_library(libplacebo::libplacebo
            ALIAS PkgConfig::SUNPLAYER_LIBPLACEBO)

    find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS
            GuiPrivate
            Quick
            QuickControls2
            QuickDialogs2
            ShaderTools
    )
    find_package(cubeb CONFIG REQUIRED)
    find_package(MoltenVK 1.4.2 EXACT CONFIG REQUIRED)
    set(sunplayer_qt_minimum_version 6.11.1)
else ()
    message(FATAL_ERROR
            "SunPlayer currently supports Windows, Apple-Silicon macOS, and "
            "native Wayland Linux.")
endif ()

qt_standard_project_setup(REQUIRES ${sunplayer_qt_minimum_version})

add_library(sunplayer_diagnostics STATIC
        src/diagnostics/ApplicationLog.cpp
        src/diagnostics/ApplicationLog.h
        src/diagnostics/LogCategories.cpp
        src/diagnostics/LogCategories.h
)
target_include_directories(sunplayer_diagnostics
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(sunplayer_diagnostics
        PUBLIC
        Qt6::Core
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(sunplayer_linux_platform STATIC
            src/platform/linux/LinuxWaylandWindowContext.cpp
            src/platform/linux/LinuxWaylandWindowContext.h
            src/platform/linux/WaylandColorManagement.cpp
            src/platform/linux/WaylandColorManagement.h
            src/presentation/PresentationSurfaceContract.h
    )
    qt_generate_wayland_protocol_client_sources(
            sunplayer_linux_platform
            PRIVATE_CODE
            FILES
            "${SUNPLAYER_WAYLAND_COLOR_MANAGEMENT_PROTOCOL}"
    )
    target_include_directories(sunplayer_linux_platform
            PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(sunplayer_linux_platform
            PUBLIC
            Qt6::Gui
            PRIVATE
            Qt6::GuiPrivate
            Qt6::WaylandClientPrivate
            Vulkan::Vulkan
            Wayland::Client
            sunplayer_diagnostics
    )
endif ()

add_library(sunplayer_audio STATIC
        src/audio/AudioSink.h
        src/audio/AudioOutputLedger.cpp
        src/audio/AudioOutputLedger.h
        src/audio/AudioTypes.cpp
        src/audio/AudioTypes.h
        src/audio/ControlledAudioSink.cpp
        src/audio/ControlledAudioSink.h
        src/audio/CubebAudioSink.cpp
        src/audio/CubebAudioSink.h
        src/audio/RealtimePcmQueue.cpp
        src/audio/RealtimePcmQueue.h
)
target_include_directories(sunplayer_audio
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(sunplayer_audio
        PRIVATE
        Qt6::Core
        cubeb::cubeb
)

add_library(sunplayer_media STATIC
        src/media/DecodedVideoFrame.cpp
        src/media/DecodedVideoFrame.h
        src/media/FfmpegFirstFrameDecoder.cpp
        src/media/FfmpegFirstFrameDecoder.h
        src/media/FfmpegHardwareDevice.cpp
        src/media/FfmpegHardwareDevice.h
        src/media/FfmpegMediaDecoder.cpp
        src/media/FfmpegMediaDecoder.h
        src/media/MediaStreamTypes.h
        src/media/FfmpegVideoDecoder.cpp
        src/media/FfmpegVideoDecoder.h
        src/media/ffmpeg/FfmpegPacketRouter.cpp
        src/media/ffmpeg/FfmpegPacketRouter.h
        src/media/ffmpeg/FfmpegStreamMetadata.cpp
        src/media/ffmpeg/FfmpegStreamMetadata.h
        src/media/ffmpeg/FfmpegVideoPacketDecoder.cpp
        src/media/ffmpeg/FfmpegVideoPacketDecoder.h
        src/media/ffmpeg/FfmpegVideoDecodeFallback.cpp
        src/media/ffmpeg/FfmpegVideoDecodeFallback.h
        # Shared subtitle state and the session-facing track model are needed
        # by both the application and MediaSession integration tests.
        src/subtitles/SubtitleSource.cpp
        src/subtitles/SubtitleSource.h
        src/subtitles/SubtitleTrackModel.cpp
        src/subtitles/SubtitleTrackModel.h
        src/subtitles/SubtitleTypes.cpp
        src/subtitles/SubtitleTypes.h
)
target_include_directories(sunplayer_media
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(sunplayer_media
        PUBLIC
        sunplayer_audio
        PRIVATE
        Qt6::Core
        sunplayer_diagnostics
        sunplayer_ffmpeg
)

add_library(sunplayer_video_pipeline STATIC
        src/graphics/GraphicsBackendFactory.cpp
        src/graphics/GraphicsBackendFactory.h
        src/graphics/GraphicsDeviceDomain.cpp
        src/graphics/GraphicsDeviceDomain.h
        src/presentation/HdrCompositor.cpp
        src/presentation/HdrCompositor.h
        src/presentation/SubtitleRenderer.cpp
        src/presentation/SubtitleRenderer.h
        src/presentation/VideoPresentationGeometry.cpp
        src/presentation/VideoPresentationGeometry.h
        src/video/DiagnosticVideoProducer.cpp
        src/video/DiagnosticVideoProducer.h
        src/video/DiagnosticVideoSource.cpp
        src/video/DiagnosticVideoSource.h
        src/video/DecodedVideoSource.cpp
        src/video/DecodedVideoSource.h
        src/video/LibplaceboDecodedVideoProducer.cpp
        src/video/LibplaceboDecodedVideoProducer.h
        src/video/LibplaceboDiagnosticVideoProducer.cpp
        src/video/LibplaceboDiagnosticVideoProducer.h
        src/video/QrhiVideoTarget.cpp
        src/video/QrhiVideoTarget.h
        src/video/RenderedVideoProducer.h
        src/video/RenderedVideoSurface.cpp
        src/video/RenderedVideoSurface.h
        src/video/RenderedVideoSource.h
        src/video/VideoTargetInterop.cpp
        src/video/VideoTargetInterop.h
        src/video/VideoFailure.h
        src/video/libplacebo/LibplaceboFfmpegInterop.c
        src/video/libplacebo/LibplaceboColorPolicy.cpp
        src/video/libplacebo/LibplaceboColorPolicy.h
        src/video/libplacebo/LibplaceboFrameImporter.cpp
        src/video/libplacebo/LibplaceboFrameImporter.h
        src/video/libplacebo/LibplaceboRenderContext.cpp
        src/video/libplacebo/LibplaceboRenderContext.h
)
target_include_directories(sunplayer_video_pipeline
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(sunplayer_video_pipeline
        PUBLIC
        libplacebo::libplacebo
        PRIVATE
        Qt6::GuiPrivate
        Qt6::Quick
        sunplayer_diagnostics
        sunplayer_ffmpeg
        sunplayer_libass
        sunplayer_media
)

if (WIN32)
    target_sources(sunplayer_video_pipeline PRIVATE
            src/graphics/backends/D3D11GraphicsDeviceDomain.cpp
            src/graphics/backends/D3D11GraphicsDeviceDomain.h
            src/graphics/backends/D3D11LibplaceboFrameImporter.cpp
            src/graphics/backends/D3D11LibplaceboFrameImporter.h
            src/graphics/backends/D3D11LibplaceboVideoTarget.cpp
            src/graphics/backends/D3D11LibplaceboVideoTarget.h
    )
    target_link_libraries(sunplayer_video_pipeline PRIVATE
            d3d11
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(sunplayer_video_pipeline PRIVATE
            src/graphics/backends/VulkanGraphicsDeviceDomain.cpp
            src/graphics/backends/VulkanGraphicsDeviceDomain.h
            src/graphics/backends/VulkanLibplaceboVideoTarget.cpp
            src/graphics/backends/VulkanLibplaceboVideoTarget.h
    )
    target_link_libraries(sunplayer_video_pipeline PRIVATE
            Vulkan::Vulkan
    )
elseif (APPLE)
    target_sources(sunplayer_video_pipeline PRIVATE
            src/graphics/backends/MetalGraphicsDeviceDomain.h
            src/graphics/backends/MetalGraphicsDeviceDomain.mm
            src/graphics/backends/MetalLibplaceboFrameImporter.h
            src/graphics/backends/MetalLibplaceboFrameImporter.mm
            src/graphics/backends/MetalLibplaceboVideoTarget.h
            src/graphics/backends/MetalLibplaceboVideoTarget.mm
    )
    target_compile_options(sunplayer_video_pipeline PRIVATE
            $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )
    target_link_libraries(sunplayer_video_pipeline PRIVATE
            MoltenVK::MoltenVK
            "-framework CoreVideo"
    )
endif ()

qt_add_executable(sunplayer
        src/playback/CoalescedGenerationWake.h
        src/playback/MediaTrackModel.cpp
        src/playback/MediaTrackModel.h
        src/playback/MediaSession.cpp
        src/playback/MediaSession.h
        src/playback/VideoFrameQueue.cpp
        src/playback/VideoFrameQueue.h
        src/playback/VideoFrameScheduler.cpp
        src/playback/VideoFrameScheduler.h
        src/app/ApplicationSettings.cpp
        src/app/ApplicationSettings.h
        src/app/LocalMediaDrop.cpp
        src/app/LocalMediaDrop.h
        src/app/main.cpp
        src/app/PresentationSettings.cpp
        src/app/PresentationSettings.h
        src/app/PresentationWindow.cpp
        src/app/PresentationWindow.h
        src/app/WindowShortcut.h
        src/app/VideoViewportState.cpp
        src/app/VideoViewportState.h
        src/app/windowchrome/WindowChromeController.cpp
        src/app/windowchrome/WindowChromeController.h
        src/platform/ColorPrimaries.h
        src/platform/DisplayState.h
        src/platform/DisplayStateProvider.cpp
        src/platform/DisplayStateProvider.h
        src/platform/PlaybackPowerInhibitor.cpp
        src/platform/PlaybackPowerInhibitor.h
        src/presentation/PresentationOutputState.cpp
        src/presentation/PresentationOutputState.h
        src/presentation/PresentationTarget.cpp
        src/presentation/PresentationTarget.h
        src/presentation/QuickUiLayer.cpp
        src/presentation/QuickUiLayer.h
        src/presentation/RhiPresentationEngine.cpp
        src/presentation/RhiPresentationEngine.h
        src/video/ActiveVideoSource.cpp
        src/video/ActiveVideoSource.h
)

if (APPLE)
    target_sources(sunplayer PRIVATE
            src/platform/macos/MacDisplayStateProvider.h
            src/platform/macos/MacDisplayStateProvider.mm
            src/platform/macos/MacPlaybackPowerInhibitor.mm
    )
    target_compile_options(sunplayer PRIVATE
            $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )
endif ()

if (WIN32)
    set_source_files_properties(packaging/windows/SunPlayer.rc PROPERTIES
            OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/Assets/app.ico"
    )
    target_sources(sunplayer PRIVATE
            packaging/windows/SunPlayer.rc
            src/platform/windows/WindowsDisplayStateProvider.cpp
            src/platform/windows/WindowsDisplayStateProvider.h
            src/platform/windows/WindowsPlaybackPowerInhibitor.cpp
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(sunplayer PRIVATE
            src/platform/linux/LinuxPlaybackPowerInhibitor.cpp
    )
endif ()

set_source_files_properties(src/app/Main.qml PROPERTIES
        QT_RESOURCE_ALIAS Main.qml
)
set_source_files_properties(src/app/AppShell.qml PROPERTIES
        QT_RESOURCE_ALIAS AppShell.qml
)
set_source_files_properties(
        src/app/windowchrome/ClientSideWindowChrome.qml
        PROPERTIES
        QT_RESOURCE_ALIAS ClientSideWindowChrome.qml
)
set_source_files_properties(src/app/pages/HdrLabPage.qml PROPERTIES
        QT_RESOURCE_ALIAS HdrLabPage.qml
)
set_source_files_properties(src/app/pages/PlayerPage.qml PROPERTIES
        QT_RESOURCE_ALIAS PlayerPage.qml
)
set_source_files_properties(src/app/pages/VideoPage.qml PROPERTIES
        QT_RESOURCE_ALIAS VideoPage.qml
)
set_source_files_properties(src/app/icons/SunPlayer.png PROPERTIES
        QT_RESOURCE_ALIAS icons/SunPlayer.png
)
set_source_files_properties(src/app/icons/lucide/play.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/play.svg
)
set_source_files_properties(src/app/icons/lucide/pause.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/pause.svg
)
set_source_files_properties(src/app/icons/lucide/rotate-ccw.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/rotate-ccw.svg
)
set_source_files_properties(src/app/icons/lucide/rotate-cw.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/rotate-cw.svg
)
set_source_files_properties(src/app/icons/lucide/volume-2.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/volume-2.svg
)
set_source_files_properties(src/app/icons/lucide/volume-x.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/volume-x.svg
)
set_source_files_properties(src/app/icons/lucide/ellipsis.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/ellipsis.svg
)
set_source_files_properties(src/app/icons/lucide/x.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/x.svg
)
set_source_files_properties(src/app/icons/lucide/minus.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/minus.svg
)
set_source_files_properties(src/app/icons/lucide/square.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/square.svg
)
set_source_files_properties(src/app/icons/lucide/copy.svg PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/copy.svg
)
set_source_files_properties(src/app/icons/lucide/LICENSE PROPERTIES
        QT_RESOURCE_ALIAS icons/lucide/LICENSE
)

qt_add_qml_module(sunplayer
        URI SunPlayer
        VERSION 1.0
        QML_FILES
        src/app/Main.qml
        src/app/AppShell.qml
        src/app/windowchrome/ClientSideWindowChrome.qml
        src/app/pages/HdrLabPage.qml
        src/app/pages/PlayerPage.qml
        src/app/pages/VideoPage.qml
        RESOURCES
        src/app/icons/SunPlayer.png
        src/app/icons/lucide/play.svg
        src/app/icons/lucide/pause.svg
        src/app/icons/lucide/rotate-ccw.svg
        src/app/icons/lucide/rotate-cw.svg
        src/app/icons/lucide/volume-2.svg
        src/app/icons/lucide/volume-x.svg
        src/app/icons/lucide/ellipsis.svg
        src/app/icons/lucide/x.svg
        src/app/icons/lucide/minus.svg
        src/app/icons/lucide/square.svg
        src/app/icons/lucide/copy.svg
        src/app/icons/lucide/LICENSE
)

qt_generate_foreign_qml_types(sunplayer_video_pipeline sunplayer)

set(sunplayer_shader_options PRECOMPILE)
find_program(sunplayer_spirv_opt spirv-opt)
if (sunplayer_spirv_opt)
    list(APPEND sunplayer_shader_options OPTIMIZED)
else ()
    message(STATUS
            "spirv-opt was not found; skipping optional SPIR-V optimization")
endif ()

qt_add_shaders(sunplayer "sunplayer_shaders"
        ${sunplayer_shader_options}
        PREFIX
        /shaders
        BASE
        src/presentation/shaders
        FILES
        src/presentation/shaders/fullscreen.vert
        src/presentation/shaders/diagnostic_video.frag
        src/presentation/shaders/compositor.frag
)
unset(sunplayer_spirv_opt CACHE)

target_link_libraries(sunplayer
        PRIVATE
        Qt6::GuiPrivate
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickDialogs2
        sunplayer_diagnostics
        sunplayer_video_pipeline
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(sunplayer PRIVATE
            Qt6::DBus
            sunplayer_linux_platform
    )
endif ()

target_include_directories(sunplayer
        PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        # qmltyperegistrar includes declaratively registered types by basename.
        "${CMAKE_CURRENT_SOURCE_DIR}/src/app"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/playback"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/presentation"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/video"
)

target_compile_definitions(sunplayer
        PRIVATE
        SUNPLAYER_VERSION="${PROJECT_VERSION}"
)

set_target_properties(sunplayer PROPERTIES
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        MACOSX_BUNDLE TRUE
        WIN32_EXECUTABLE TRUE
)

if (WIN32)
    target_link_libraries(sunplayer PRIVATE
            CoreMessaging
            RuntimeObject
            WindowsApp
    )

    sunplayer_stage_windows_runtime(sunplayer)
    add_custom_command(
            TARGET sunplayer
            POST_BUILD
            COMMAND "$<TARGET_FILE:Qt6::windeployqt>"
                    --qmldir
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/app"
                    --verbose
                    0
                    --no-compiler-runtime
                    --no-translations
                    --no-opengl-sw
                    --no-system-d3d-compiler
                    --no-system-dxc-compiler
                    "$<TARGET_FILE:sunplayer>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/qt.conf"
                    "$<TARGET_FILE_DIR:sunplayer>/qt.conf"
            VERBATIM
    )

endif ()

if (BUILD_TESTING)
    if (WIN32 OR APPLE)
        find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Test)
    else ()
        find_package(Qt6 6.10...<6.11 REQUIRED COMPONENTS Test)
    endif ()
    add_subdirectory(tests)
endif ()

include(GNUInstallDirs)

if (WIN32)
    # MSIX needs the compiler runtime app-locally. Select the runtime from the
    # toolset used for this build and keep packaging out of dependency closure.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    include(InstallRequiredSystemLibraries)
endif ()

install(TARGETS sunplayer
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES src/app/icons/lucide/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/sunplayer/licenses
        RENAME Lucide.txt
)

if (WIN32)
    # The vcpkg toolchain already deploys the executable's recursively imported
    # DLLs beside build-tree targets. Use the matching install helper so pkg-
    # config libraries such as libass and their dependencies are not missed.
    x_vcpkg_install_local_dependencies(
            TARGETS sunplayer
            DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    qt_generate_deploy_qml_app_script(
            TARGET sunplayer
            OUTPUT_SCRIPT deploy_script
            NO_COMPILER_RUNTIME
            NO_TRANSLATIONS
            NO_UNSUPPORTED_PLATFORM_ERROR
            DEPLOY_TOOL_OPTIONS
            --no-system-d3d-compiler
            --no-system-dxc-compiler
            --skip-plugin-types qmltooling
    )
    install(SCRIPT ${deploy_script})

    set(sunplayer_development_root
            "${CMAKE_CURRENT_BINARY_DIR}/sunplayer-development")
    add_custom_target(sunplayer_run_with_identity
            COMMAND ${CMAKE_COMMAND} -E rm -rf
                    "${sunplayer_development_root}/install"
            COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
                    --config "$<CONFIG>"
                    --prefix "${sunplayer_development_root}/install"
            COMMAND pwsh -NoLogo -NoProfile -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/Run-WindowsDevelopment.ps1"
                    -InstallPrefix "${sunplayer_development_root}/install"
                    -OutputAppxDirectory "${sunplayer_development_root}/AppX"
            DEPENDS sunplayer
            USES_TERMINAL
            VERBATIM
    )
endif ()
