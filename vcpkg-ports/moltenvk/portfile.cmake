vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

if(NOT VCPKG_TARGET_IS_OSX OR NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    message(FATAL_ERROR "The Sunroom MoltenVK package supports arm64 macOS only")
endif()

vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/KhronosGroup/MoltenVK/releases/download/v${VERSION}/MoltenVK-macos.tar"
    FILENAME "MoltenVK-macos-${VERSION}.tar"
    SHA512 985f483e832ae3605b62b78798989f79c8833c13b568ff3615fb9c1f2780c3e9960cb13f79f2560b01563694249520d23d4f93d284580c13f6c842eff0c6b99d
)

vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

file(INSTALL "${SOURCE_PATH}/MoltenVK/include/MoltenVK"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/MoltenVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${SOURCE_PATH}/MoltenVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a"
    DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/MoltenVKConfig.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/MoltenVKConfigVersion.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
