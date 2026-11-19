# SPDX-License-Identifier: GPL-3.0-only
# ARM64 -> ARM64 recompiler (docs/nativo.md, milestone M1): Dynarmic with its ARM64 backend and
# the dependencies that backend needs. Every source is pinned to a commit and checked by SHA-256.
# Dynarmic's own submodules are not in its archive, so each one is fetched here before Dynarmic
# looks for it. Other hosts need the x86-64 externals: point FETCHCONTENT_SOURCE_DIR_SC_DYNARMIC
# at a patched checkout with submodules.
include(FetchContent)

FetchContent_Declare(sc_fmt
  URL https://github.com/fmtlib/fmt/archive/7b273fbb541c7c9058800bc613f4291274b2d26e.tar.gz
  URL_HASH SHA256=4d5465ace5ee6aed8183b0954f9f22fffa5ca7dd550fbd5763a54e58ab958bca
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(sc_mcl
  URL https://github.com/azahar-emu/mcl/archive/5fc4beaf331037649b10625736b41365defb4f50.tar.gz
  URL_HASH SHA256=36a2200e7940838112c9967388685a5178c2e9a110233193b914f54b2e2f51fd
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(sc_oaknut
  URL https://github.com/merryhime/oaknut/archive/94c726ce0338b054eb8cb5ea91de8fe6c19f4392.tar.gz
  URL_HASH SHA256=c404db82002f9a851a412b3d55ac9be8a42a2e319fd2fc8a73e63e6ef5d4f97b
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(sc_robin_map
  URL https://github.com/Tessil/robin-map/archive/054ec5ad67440fcd65e0497e5a27ef31f53fcc7f.tar.gz
  URL_HASH SHA256=a73d3a6b98b7961244981bd89a42fcb4aef63a89d1ec135bbc90d3621b355d27
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# Header-only Boost subset (ICL and Variant) from the Azahar project, as NeXo upstream uses it.
FetchContent_Declare(sc_ext_boost
  URL https://github.com/azahar-emu/ext-boost/archive/6a85c3100499e886e11c87a5c2109eedacea0a61.tar.gz
  URL_HASH SHA256=5bff0bb6eaa1f215227310d6d34cd53983d88676b9bbf06e10e4b945bc6cd8b9
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(sc_dynarmic
  URL https://github.com/azahar-emu/dynarmic/archive/a46601580d5512d324104f985b5f0209dc980ddc.tar.gz
  URL_HASH SHA256=73c7f7fbf573c542767a4146297e4954369485077ae3880395445af787de0915
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  PATCH_COMMAND patch -p1 -N -s -i "${PROJECT_SOURCE_DIR}/third_party/dynarmic/arm64-caller-owned-code-memory.patch")

# No tests of the dependencies (oaknut follows BUILD_TESTING). Restored at the end of this file.
set(BUILD_TESTING OFF)
# Dynarmic exports its target, so the libraries it links must be installable too.
set(FMT_INSTALL ON CACHE BOOL "" FORCE)
set(MCL_INSTALL ON CACHE BOOL "" FORCE)
set(TSL_ROBIN_MAP_ENABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sc_fmt sc_mcl sc_oaknut sc_robin_map sc_ext_boost)

# ext-boost has no CMake package: CMake's FindBoost module reads its version header. Dynarmic calls
# find_package(Boost) again under its own policies, hence the defaults.
set(BOOST_ROOT "${sc_ext_boost_SOURCE_DIR}")
set(Boost_INCLUDE_DIR "${sc_ext_boost_SOURCE_DIR}")
set(Boost_NO_SYSTEM_PATHS ON)
set(Boost_NO_BOOST_CMAKE ON)
set(CMAKE_POLICY_DEFAULT_CMP0144 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0167 OLD)
if(POLICY CMP0167)
  cmake_policy(SET CMP0144 NEW)
  cmake_policy(SET CMP0167 OLD)
endif()
find_package(Boost 1.57 REQUIRED)

set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
set(DYNARMIC_USE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_FRONTENDS "A64" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(sc_dynarmic)
unset(BUILD_TESTING)

# Darwin changes x18 on every context switch, so translated code must not use it. The register
# allocator skips it (static_assert in jit_dynarmic.cpp); this rejects explicit uses in the backend.
file(GLOB sc_arm64_backend LIST_DIRECTORIES false "${sc_dynarmic_SOURCE_DIR}/src/dynarmic/backend/arm64/*")
foreach(source IN LISTS sc_arm64_backend)
  file(STRINGS "${source}" sc_x18 REGEX "[^A-Za-z0-9_][XW]18[^0-9]|[XW]Reg[{(]18[})]")
  if(sc_x18)
    message(FATAL_ERROR "Dynarmic ARM64 backend uses x18 in ${source}: ${sc_x18}")
  endif()
endforeach()

# jit_dynarmic.cpp also reads Dynarmic's IR to route instructions the ARM64 backend cannot emit to
# the interpreter, and checks the backend's register allocation, so it needs these headers too.
target_link_libraries(switchcore PRIVATE dynarmic merry::mcl merry::oaknut fmt::fmt Boost::boost)
target_compile_definitions(switchcore PRIVATE NEXO2_HAS_JIT)
