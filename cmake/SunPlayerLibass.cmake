function(sunplayer_configure_libass)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(
            SUNPLAYER_LIBASS
            REQUIRED
            IMPORTED_TARGET
            libass>=0.17.4
    )

    add_library(sunplayer_libass INTERFACE)
    target_link_libraries(
            sunplayer_libass
            INTERFACE
            PkgConfig::SUNPLAYER_LIBASS
    )
endfunction()
