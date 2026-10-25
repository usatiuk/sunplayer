set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)

if(PORT STREQUAL "qtkeychain")
    list(APPEND VCPKG_ENV_PASSTHROUGH SUNPLAYER_QT_PREFIX)
endif()

if(PORT STREQUAL "libass")
    # Use macOS fonts without auto-detecting undeclared Homebrew dependencies.
    set(VCPKG_MESON_CONFIGURE_OPTIONS
            -Dcoretext=enabled -Dfontconfig=disabled -Dlibunibreak=disabled)
endif()
