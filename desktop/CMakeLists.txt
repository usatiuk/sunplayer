cmake_minimum_required(VERSION 3.21)

if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(_sunroom_vcpkg_toolchain)

    if (DEFINED ENV{VCPKG_ROOT})
        set(_sunroom_vcpkg_toolchain
                "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    elseif (CMAKE_HOST_WIN32 AND DEFINED ENV{VCToolsInstallDir})
        get_filename_component(
                _sunroom_vc_root
                "$ENV{VCToolsInstallDir}/../../.."
                ABSOLUTE
        )
        set(_sunroom_vcpkg_toolchain
                "${_sunroom_vc_root}/vcpkg/scripts/buildsystems/vcpkg.cmake")
        unset(_sunroom_vc_root)
    endif ()

    if (_sunroom_vcpkg_toolchain
            AND EXISTS "${_sunroom_vcpkg_toolchain}")
        set(
                CMAKE_TOOLCHAIN_FILE
                "${_sunroom_vcpkg_toolchain}"
                CACHE FILEPATH
                "Toolchain used to configure Sunroom"
        )
    endif ()

    unset(_sunroom_vcpkg_toolchain)
endif ()

if (CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(
            VCPKG_TARGET_TRIPLET
            x64-windows-clangcl
            CACHE STRING
            "Sunroom dependency target triplet"
    )
endif ()

project(Sunroom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

if (NOT VCPKG_MANIFEST_MODE)
    message(FATAL_ERROR
            "Sunroom currently requires vcpkg manifest mode. Set VCPKG_ROOT "
            "or configure with the vcpkg CMake toolchain.")
endif ()

find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS
        GuiPrivate
        Quick
        QuickControls2
        ShaderTools
)
find_package(libplacebo 7.360.1 EXACT CONFIG REQUIRED)

qt_standard_project_setup(REQUIRES 6.11.1)

qt_add_executable(sunroom
        src/app/main.cpp
        src/app/PresentationSettings.cpp
        src/app/PresentationSettings.h
        src/app/PresentationWindow.cpp
        src/app/PresentationWindow.h
        src/app/VideoViewportState.cpp
        src/app/VideoViewportState.h
        src/graphics/GraphicsBackendFactory.cpp
        src/graphics/GraphicsBackendFactory.h
        src/graphics/GraphicsDeviceDomain.cpp
        src/graphics/GraphicsDeviceDomain.h
        src/platform/DisplayState.h
        src/platform/DisplayStateProvider.cpp
        src/platform/DisplayStateProvider.h
        src/presentation/HdrCompositor.cpp
        src/presentation/HdrCompositor.h
        src/presentation/PresentationOutputState.cpp
        src/presentation/PresentationOutputState.h
        src/presentation/PresentationTarget.cpp
        src/presentation/PresentationTarget.h
        src/presentation/QuickUiLayer.cpp
        src/presentation/QuickUiLayer.h
        src/presentation/RhiPresentationEngine.cpp
        src/presentation/RhiPresentationEngine.h
        src/video/DiagnosticVideoProducer.cpp
        src/video/DiagnosticVideoProducer.h
        src/video/DiagnosticVideoSource.cpp
        src/video/DiagnosticVideoSource.h
        src/video/QrhiVideoTarget.cpp
        src/video/QrhiVideoTarget.h
        src/video/RenderedVideoProducer.h
        src/video/RenderedVideoSurface.cpp
        src/video/RenderedVideoSurface.h
        src/video/RenderedVideoSource.h
        src/video/VideoTargetInterop.cpp
        src/video/VideoTargetInterop.h
)

set_source_files_properties(src/app/Main.qml PROPERTIES
        QT_RESOURCE_ALIAS Main.qml
)
set_source_files_properties(src/app/AppShell.qml PROPERTIES
        QT_RESOURCE_ALIAS AppShell.qml
)
set_source_files_properties(src/app/pages/HdrLabPage.qml PROPERTIES
        QT_RESOURCE_ALIAS HdrLabPage.qml
)

qt_add_qml_module(sunroom
        URI Sunroom
        VERSION 1.0
        QML_FILES
        src/app/Main.qml
        src/app/AppShell.qml
        src/app/pages/HdrLabPage.qml
)

qt_add_shaders(sunroom "sunroom_shaders"
        PRECOMPILE
        OPTIMIZED
        PREFIX
        /shaders
        BASE
        src/presentation/shaders
        FILES
        src/presentation/shaders/fullscreen.vert
        src/presentation/shaders/diagnostic_video.frag
        src/presentation/shaders/compositor.frag
)

target_link_libraries(sunroom
        PRIVATE
        Qt6::GuiPrivate
        Qt6::Quick
        Qt6::QuickControls2
        libplacebo::libplacebo
)

target_include_directories(sunroom
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        # qmltyperegistrar includes declaratively registered types by basename.
        "${CMAKE_CURRENT_SOURCE_DIR}/src/app"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/presentation"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/video"
)

set_target_properties(sunroom PROPERTIES
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        MACOSX_BUNDLE TRUE
        WIN32_EXECUTABLE TRUE
)

if (WIN32)
    target_sources(sunroom PRIVATE
            src/graphics/backends/D3D11GraphicsDeviceDomain.cpp
            src/graphics/backends/D3D11GraphicsDeviceDomain.h
    )

    target_link_libraries(sunroom PRIVATE
            CoreMessaging
            RuntimeObject
            WindowsApp
    )

    add_custom_command(
            TARGET sunroom
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:sunroom>
                    $<TARGET_FILE_DIR:sunroom>
            COMMAND_EXPAND_LISTS
    )

endif ()

if (BUILD_TESTING)
    find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif ()

include(GNUInstallDirs)

install(TARGETS sunroom
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if (WIN32)
    install(
            IMPORTED_RUNTIME_ARTIFACTS
            libplacebo::libplacebo
            spirv-cross-c-shared
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif ()

qt_generate_deploy_qml_app_script(
        TARGET sunroom
        OUTPUT_SCRIPT deploy_script
        NO_COMPILER_RUNTIME
        NO_TRANSLATIONS
        NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
