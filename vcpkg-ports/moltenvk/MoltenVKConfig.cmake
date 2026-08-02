if(NOT TARGET MoltenVK::MoltenVK)
    get_filename_component(_moltenvk_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    add_library(MoltenVK::MoltenVK STATIC IMPORTED)
    set_target_properties(MoltenVK::MoltenVK PROPERTIES
        IMPORTED_CONFIGURATIONS "Debug;Release"
        IMPORTED_LOCATION_DEBUG "${_moltenvk_prefix}/debug/lib/libMoltenVK.a"
        IMPORTED_LOCATION_RELEASE "${_moltenvk_prefix}/lib/libMoltenVK.a"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        INTERFACE_INCLUDE_DIRECTORIES "${_moltenvk_prefix}/include"
        INTERFACE_LINK_LIBRARIES "-framework Metal;-framework Foundation;-framework QuartzCore;-framework CoreGraphics;-framework IOSurface;-framework IOKit;-framework AppKit"
    )
    unset(_moltenvk_prefix)
endif()
