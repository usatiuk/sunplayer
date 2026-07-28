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
        src/main.cpp
        src/DisplayState.h
        src/DisplayStateProvider.cpp
        src/DisplayStateProvider.h
        src/HdrCompositor.cpp
        src/HdrCompositor.h
        src/PresentationOutputState.cpp
        src/PresentationOutputState.h
        src/PresentationTarget.cpp
        src/PresentationTarget.h
        src/PresentationSettings.cpp
        src/PresentationSettings.h
        src/PresentationWindow.cpp
        src/PresentationWindow.h
        src/QuickUiLayer.cpp
        src/QuickUiLayer.h
        src/RhiPresentationEngine.cpp
        src/RhiPresentationEngine.h
)

set_source_files_properties(src/Main.qml PROPERTIES
        QT_RESOURCE_ALIAS Main.qml
)

qt_add_qml_module(sunroom
        URI Sunroom
        VERSION 1.0
        QML_FILES
        src/Main.qml
)

qt_add_shaders(sunroom "sunroom_shaders"
        PRECOMPILE
        OPTIMIZED
        PREFIX
        /shaders
        BASE
        src/shaders
        FILES
        src/shaders/fullscreen.vert
        src/shaders/compositor.frag
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
