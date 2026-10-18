set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_LOAD_VCVARS_ENV ON)

set(
    VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../toolchains/clang-cl.cmake"
)

if(PORT STREQUAL "qtkeychain")
    # windeployqt treats qt6keychain.dll as a Qt SDK module by its name.
    # Link this third-party library statically instead of modifying the SDK.
    set(VCPKG_LIBRARY_LINKAGE static)
    list(APPEND VCPKG_ENV_PASSTHROUGH SUNPLAYER_QT_PREFIX)
endif()
