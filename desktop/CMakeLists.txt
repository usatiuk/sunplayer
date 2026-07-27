cmake_minimum_required(VERSION 3.21)

project(Sunroom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(sunroom
        src/main.cpp
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

target_link_libraries(sunroom
        PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
)

set_target_properties(sunroom PROPERTIES
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        MACOSX_BUNDLE TRUE
        WIN32_EXECUTABLE TRUE
)

if (WIN32)
    add_custom_command(TARGET sunroom POST_BUILD
            COMMAND Qt6::windeployqt
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/src"
            --no-compiler-runtime
            --no-translations
            "$<TARGET_FILE:sunroom>"
            COMMENT "Deploying Qt runtime dependencies"
            VERBATIM
    )
endif ()

include(GNUInstallDirs)

install(TARGETS sunroom
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
