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
