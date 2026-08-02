function(sunroom_configure_libass)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(
            SUNROOM_LIBASS
            REQUIRED
            IMPORTED_TARGET
            libass
    )

    add_library(sunroom_libass INTERFACE)
    target_link_libraries(
            sunroom_libass
            INTERFACE
            PkgConfig::SUNROOM_LIBASS
    )
endfunction()
