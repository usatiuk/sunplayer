vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO mozilla/cubeb
    REF ef47ae581df7c2f76058d554b3edde17f9ee7cba
    SHA512 6415d55ee66bd50ec119c3c8f8c666dd2ff3bbef0498f0a0fd3b7f4ed564531b2649eb93fe45d7a89cf2f5967e3a7d031357be269ff0bba00cc3109f9f3a3cbc
    HEAD_REF master
    PATCHES
        fix-install-interface.patch
        fail-disabled-device-reconfigure.patch
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTS=OFF
        -DBUILD_TOOLS=OFF
        -DBUILD_RUST_LIBS=OFF
        -DBUNDLE_SPEEX=OFF
        -DLAZY_LOAD_LIBS=ON
        -DUSE_SANITIZERS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/cubeb)
vcpkg_fixup_pkgconfig()
vcpkg_copy_pdbs()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(
    FILE_LIST
        "${SOURCE_PATH}/LICENSE"
        "${CMAKE_CURRENT_LIST_DIR}/speex-copyright"
)
