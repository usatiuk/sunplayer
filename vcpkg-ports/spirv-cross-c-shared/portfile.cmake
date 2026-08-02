vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO KhronosGroup/SPIRV-Cross
    REF vulkan-sdk-${VERSION}
    SHA512 431cb5ea51aee2d04195cc54674343e84245ec92ca249d17599f7e16d111f2f5650cb7eb6c53c93d33c8ac7d1832ce31bfe20561e12ed00b3b2ede5944e2c890
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DSPIRV_CROSS_SHARED=ON
        -DSPIRV_CROSS_STATIC=OFF
        -DSPIRV_CROSS_CLI=OFF
        -DSPIRV_CROSS_ENABLE_TESTS=OFF
        -DSPIRV_CROSS_ENABLE_C_API=ON
        -DSPIRV_CROSS_ENABLE_GLSL=ON
        -DSPIRV_CROSS_ENABLE_HLSL=ON
        -DSPIRV_CROSS_ENABLE_MSL=OFF
        -DSPIRV_CROSS_ENABLE_CPP=OFF
        -DSPIRV_CROSS_ENABLE_REFLECT=OFF
        -DSPIRV_CROSS_ENABLE_UTIL=OFF
        -DSPIRV_CROSS_SKIP_INSTALL=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(
    PACKAGE_NAME spirv_cross_c_shared
    CONFIG_PATH share/spirv_cross_c_shared/cmake
)
vcpkg_fixup_pkgconfig()
vcpkg_replace_string(
    "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/spirv-cross-c-shared.pc"
    "-lspirv-cross-c-shared"
    "-lspirv-cross-c-sharedd"
)
vcpkg_copy_pdbs()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/tools"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
