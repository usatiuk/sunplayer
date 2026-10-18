vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO frankosterfeld/qtkeychain
    REF "${VERSION}"
    SHA512 b1068ae513d5eab8f300186497ddcce4075e11a2a569deddbc949177efaa27970ed7bdce0b1aff61a021144540e942f60c9259b975601a92c60b8a742754624a
)

# Qt is supplied by the existing Qt installer, never by this port's dependency
# graph. The triplet tracks the prefix in this package's ABI inputs.
file(TO_CMAKE_PATH "$ENV{SUNPLAYER_QT_PREFIX}" qt_prefix)
if(NOT EXISTS "${qt_prefix}/lib/cmake/Qt6/Qt6Config.cmake")
    message(FATAL_ERROR "Configure SunPlayer with CMAKE_PREFIX_PATH pointing to its existing Qt 6 installation.")
endif()
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_WITH_QT6=ON
        -DBUILD_TRANSLATIONS=OFF
        -DBUILD_TESTING=OFF
        -DBUILD_TEST_APPLICATION=OFF
        "-DQt6_DIR=${qt_prefix}/lib/cmake/Qt6"
        "-DCMAKE_PREFIX_PATH=${qt_prefix}"
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/Qt6Keychain PACKAGE_NAME Qt6Keychain)
vcpkg_copy_pdbs()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
