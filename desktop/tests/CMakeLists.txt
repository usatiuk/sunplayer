qt_add_executable(sunroom_presentation_target_tests
        tst_PresentationTarget.cpp
        ../src/PresentationTarget.cpp
        ../src/PresentationTarget.h
        ../src/DisplayState.h
)

target_include_directories(sunroom_presentation_target_tests
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(sunroom_presentation_target_tests
        PRIVATE
        Qt6::Test
)

if (WIN32)
    add_custom_command(
            TARGET sunroom_presentation_target_tests
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:sunroom_presentation_target_tests>
                    $<TARGET_FILE_DIR:sunroom_presentation_target_tests>
            COMMAND_EXPAND_LISTS
    )
endif ()

add_test(
        NAME presentation-target
        COMMAND sunroom_presentation_target_tests
)
