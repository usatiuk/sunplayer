cmake_minimum_required(VERSION 3.21)

project(Sunroom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS
        GuiPrivate
        Quick
        QuickControls2
        ShaderTools
)

qt_standard_project_setup(REQUIRES 6.11.1)

qt_add_executable(sunroom
        src/app/main.cpp
        src/app/PresentationSettings.cpp
        src/app/PresentationSettings.h
        src/app/PresentationWindow.cpp
        src/app/PresentationWindow.h
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

qt_add_qml_module(sunroom
        URI Sunroom
        VERSION 1.0
        QML_FILES
        src/app/Main.qml
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
)

target_include_directories(sunroom
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
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

qt_generate_deploy_qml_app_script(
        TARGET sunroom
        OUTPUT_SCRIPT deploy_script
        NO_COMPILER_RUNTIME
        NO_TRANSLATIONS
        NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
