find_program(
    SUNROOM_CLANG_CL
    NAMES clang-cl
    HINTS
        "$ENV{LLVMInstallDir}/bin"
        "$ENV{VCINSTALLDIR}/Tools/Llvm/x64/bin"
    REQUIRED
)

set(CMAKE_C_COMPILER "${SUNROOM_CLANG_CL}" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "${SUNROOM_CLANG_CL}" CACHE FILEPATH "")

if (DEFINED Z_VCPKG_ROOT_DIR)
    set(_sunroom_vcpkg_root "${Z_VCPKG_ROOT_DIR}")
elseif (DEFINED _VCPKG_ROOT_DIR)
    set(_sunroom_vcpkg_root "${_VCPKG_ROOT_DIR}")
elseif (DEFINED ENV{VCPKG_ROOT})
    set(_sunroom_vcpkg_root "$ENV{VCPKG_ROOT}")
else ()
    message(FATAL_ERROR "The clang-cl dependency toolchain requires vcpkg")
endif ()

include("${_sunroom_vcpkg_root}/scripts/toolchains/windows.cmake")

unset(_sunroom_vcpkg_root)
