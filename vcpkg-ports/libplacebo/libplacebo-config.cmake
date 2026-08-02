include(CMakeFindDependencyMacro)

find_dependency(spirv_cross_c_shared CONFIG)

if(NOT TARGET libplacebo::libplacebo)
    get_filename_component(_libplacebo_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

    add_library(libplacebo::libplacebo SHARED IMPORTED)
    set_target_properties(libplacebo::libplacebo PROPERTIES
        IMPORTED_CONFIGURATIONS "Debug;Release"
        IMPORTED_LOCATION_DEBUG "${_libplacebo_prefix}/debug/bin/libplacebo-360.dll"
        IMPORTED_IMPLIB_DEBUG "${_libplacebo_prefix}/debug/lib/libplacebo.lib"
        IMPORTED_LOCATION_RELEASE "${_libplacebo_prefix}/bin/libplacebo-360.dll"
        IMPORTED_IMPLIB_RELEASE "${_libplacebo_prefix}/lib/libplacebo.lib"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        INTERFACE_INCLUDE_DIRECTORIES "${_libplacebo_prefix}/include"
        INTERFACE_LINK_LIBRARIES spirv-cross-c-shared
    )

    unset(_libplacebo_prefix)
endif()
