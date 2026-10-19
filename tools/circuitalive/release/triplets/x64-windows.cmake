set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PLATFORM_TOOLSET v143)
# Keep compiler tracking enabled so a different compiler cannot reuse this ABI cache.
set(VCPKG_DISABLE_COMPILER_TRACKING OFF)
# Distribution profile: retain every feature, build each dependency once.
set(VCPKG_BUILD_TYPE release)
# _CL_ is appended by cl.exe, including when a port requests /MP itself.
# Preserve it through vcpkg's clean environment and track it in the ABI.
set(VCPKG_ENV_PASSTHROUGH _CL_)
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED CMAKE_BUILD_PARALLEL_LEVEL)
# The pinned legacy helper is bounded during setup; track that customization.
set(VCPKG_HASH_ADDITIONAL_FILES "${CMAKE_CURRENT_LIST_DIR}/../limit-windows-toolchain.ps1")
