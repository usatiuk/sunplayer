include_guard(GLOBAL)

function(_sunroom_find_ffmpeg_runtime
        output_variable
        library
        component)
    get_filename_component(library_directory "${library}" DIRECTORY)
    get_filename_component(
            installation_prefix
            "${library_directory}/.."
            ABSOLUTE
    )
    file(
            GLOB runtime_candidates
            LIST_DIRECTORIES FALSE
            "${installation_prefix}/bin/${component}-*.dll"
    )
    list(LENGTH runtime_candidates runtime_count)
    if (NOT runtime_count EQUAL 1)
        message(FATAL_ERROR
                "Expected one ${component} runtime beside ${library}; "
                "found ${runtime_count}: ${runtime_candidates}")
    endif ()
    set(
            ${output_variable}
            "${runtime_candidates}"
            PARENT_SCOPE
    )
endfunction()

function(sunroom_configure_ffmpeg)
    add_library(sunroom_ffmpeg INTERFACE)

    if (WIN32)
        # This must run before Qt package discovery adds Qt's case-variant
        # FindFFmpeg.cmake to CMAKE_MODULE_PATH. The vcpkg module provides
        # configuration-aware component import libraries on Windows.
        find_package(FFMPEG REQUIRED)
        target_include_directories(sunroom_ffmpeg
                INTERFACE
                ${FFMPEG_INCLUDE_DIRS}
        )

        set(runtime_targets)
        foreach (component IN ITEMS
                avutil
                swresample
                avcodec
                avformat)
            set(variable_prefix "FFMPEG_lib${component}")
            set(release_variable
                    "${variable_prefix}_LIBRARY_RELEASE")
            set(debug_variable
                    "${variable_prefix}_LIBRARY_DEBUG")
            if (NOT ${release_variable}
                    OR NOT ${debug_variable})
                message(FATAL_ERROR
                        "The vcpkg FFmpeg package did not report Debug and "
                        "Release import libraries for ${component}")
            endif ()

            _sunroom_find_ffmpeg_runtime(
                    release_runtime
                    "${${release_variable}}"
                    "${component}"
            )
            _sunroom_find_ffmpeg_runtime(
                    debug_runtime
                    "${${debug_variable}}"
                    "${component}"
            )

            set(component_target
                    "sunroom_ffmpeg_${component}")
            add_library(
                    ${component_target}
                    SHARED
                    IMPORTED
                    GLOBAL
            )
            set_target_properties(
                    ${component_target}
                    PROPERTIES
                    IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
                    IMPORTED_IMPLIB_DEBUG
                        "${${debug_variable}}"
                    IMPORTED_LOCATION_DEBUG
                        "${debug_runtime}"
                    IMPORTED_IMPLIB_RELEASE
                        "${${release_variable}}"
                    IMPORTED_LOCATION_RELEASE
                        "${release_runtime}"
                    MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE
                    MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
            )
            list(APPEND runtime_targets ${component_target})
        endforeach ()

        target_link_libraries(
                sunroom_ffmpeg
                INTERFACE
                ${runtime_targets}
        )
        set(
                SUNROOM_FFMPEG_RUNTIME_TARGETS
                "${runtime_targets}"
                PARENT_SCOPE
        )
    elseif (APPLE)
        # The vcpkg module returns configuration-aware FFmpeg libraries and
        # the Apple framework dependencies enabled by the pinned port.
        find_package(FFMPEG REQUIRED)
        target_include_directories(sunroom_ffmpeg
                INTERFACE
                ${FFMPEG_INCLUDE_DIRS}
        )
        target_link_libraries(sunroom_ffmpeg
                INTERFACE
                ${FFMPEG_LIBRARIES}
        )
        set(SUNROOM_FFMPEG_RUNTIME_TARGETS "" PARENT_SCOPE)
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SUNROOM_FFMPEG REQUIRED
                IMPORTED_TARGET
                GLOBAL
                libavutil>=60
                libavutil<61
                libswresample>=6
                libswresample<7
                libavcodec>=62
                libavcodec<63
                libavformat>=62
                libavformat<63
        )
        target_link_libraries(sunroom_ffmpeg
                INTERFACE
                PkgConfig::SUNROOM_FFMPEG
        )
        set(SUNROOM_FFMPEG_RUNTIME_TARGETS "" PARENT_SCOPE)
    else ()
        message(FATAL_ERROR
                "No FFmpeg dependency contract exists for this platform")
    endif ()
endfunction()
