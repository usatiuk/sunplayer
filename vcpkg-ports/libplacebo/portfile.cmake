if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY)
else()
    vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
endif()

if((NOT VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_OSX)
        OR VCPKG_TARGET_IS_UWP)
    message(FATAL_ERROR "The SunPlayer libplacebo port supports Windows desktop and macOS only")
endif()

if("d3d11" IN_LIST FEATURES)
    set(LIBPLACEBO_D3D11 enabled)
    set(LIBPLACEBO_SHADERC enabled)
else()
    set(LIBPLACEBO_D3D11 disabled)
endif()

if("vulkan" IN_LIST FEATURES)
    set(LIBPLACEBO_VULKAN enabled)
else()
    set(LIBPLACEBO_VULKAN disabled)
endif()

if("d3d11" IN_LIST FEATURES OR "vulkan" IN_LIST FEATURES)
    set(LIBPLACEBO_SHADERC enabled)
else()
    set(LIBPLACEBO_SHADERC disabled)
endif()

if(VCPKG_TARGET_IS_WINDOWS AND NOT LIBPLACEBO_D3D11 STREQUAL "enabled")
    message(FATAL_ERROR "The Windows libplacebo build requires the d3d11 feature")
endif()
if(VCPKG_TARGET_IS_OSX AND NOT LIBPLACEBO_VULKAN STREQUAL "enabled")
    message(FATAL_ERROR "The macOS libplacebo build requires the vulkan feature")
endif()

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/haasn/libplacebo.git
    REF cee9b076f2c63104ccfd497fa79c39a867293ec4
    HEAD_REF master
    PATCHES
        fix-win64-allocation-alignment.patch
)

# libplacebo carries these exact revisions as source submodules and imports
# them through PYTHONPATH during shader-source generation. Keeping them inside
# the port avoids installing Python packages into the developer environment.
vcpkg_from_git(
    OUT_SOURCE_PATH JINJA_SOURCE_PATH
    URL https://github.com/pallets/jinja.git
    REF 15206881c006c79667fe5154fe80c01c65410679
    HEAD_REF main
)
vcpkg_from_git(
    OUT_SOURCE_PATH MARKUPSAFE_SOURCE_PATH
    URL https://github.com/pallets/markupsafe.git
    REF 297fc8e356e6836a62087949245d09a28e9f1b13
    HEAD_REF main
)
vcpkg_from_git(
    OUT_SOURCE_PATH VULKAN_HEADERS_SOURCE_PATH
    URL https://github.com/KhronosGroup/Vulkan-Headers.git
    REF 450bd2232225d6c7728a4108055ac2e37cef6475
    HEAD_REF main
)
vcpkg_from_git(
    OUT_SOURCE_PATH FAST_FLOAT_SOURCE_PATH
    URL https://github.com/fastfloat/fast_float.git
    REF 97b54ca9e75f5303507699d27c6b4f4efe4641a1
    HEAD_REF main
)

file(COPY "${JINJA_SOURCE_PATH}/" DESTINATION "${SOURCE_PATH}/3rdparty/jinja")
file(COPY "${MARKUPSAFE_SOURCE_PATH}/" DESTINATION "${SOURCE_PATH}/3rdparty/markupsafe")
file(COPY
    "${VULKAN_HEADERS_SOURCE_PATH}/"
    DESTINATION "${SOURCE_PATH}/3rdparty/Vulkan-Headers"
)
file(COPY
    "${FAST_FLOAT_SOURCE_PATH}/"
    DESTINATION "${SOURCE_PATH}/3rdparty/fast_float"
)

vcpkg_configure_meson(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        "-Dd3d11=${LIBPLACEBO_D3D11}"
        "-Dshaderc=${LIBPLACEBO_SHADERC}"
        -Dglslang=disabled
        "-Dvulkan=${LIBPLACEBO_VULKAN}"
        -Dvk-proc-addr=disabled
        -Dopengl=disabled
        -Dgl-proc-addr=disabled
        -Dlcms=disabled
        -Ddovi=enabled
        -Dlibdovi=disabled
        -Ddemos=false
        -Dtests=false
        -Dbench=false
        -Dfuzz=false
        -Dunwind=disabled
        -Dxxhash=disabled
        -Ddebug-abort=false
)

vcpkg_install_meson()
vcpkg_fixup_pkgconfig()
vcpkg_copy_pdbs()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

if(VCPKG_TARGET_IS_WINDOWS)
    file(INSTALL
        "${CMAKE_CURRENT_LIST_DIR}/libplacebo-config.cmake"
        "${CMAKE_CURRENT_LIST_DIR}/libplacebo-config-version.cmake"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    )
endif()
file(INSTALL
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
