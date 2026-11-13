cmake_minimum_required(VERSION 3.20)

# GoldSrcRenderCore: backend-neutral CPU reading of GoldSrc BSP and MDL assets and the asset types of the GoldSrcRender
# contract. It depends on the C++17 standard library only, and builds standalone or as a subproject; a project that
# already defines the targets keeps its own.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # Formatting check only: needs Python and clang-format, no compiler.
  option(FORMAT_VALIDATION_ONLY "Configure only the clang-format targets" OFF)
  if(FORMAT_VALIDATION_ONLY)
    project(GoldSrcRenderCoreFormatValidation LANGUAGES NONE)
    include(cmake/FormatValidation.cmake)
    return()
  endif()
  project(GoldSrcRenderCore LANGUAGES CXX)
  option(GOLDSRCRENDERCORE_BUILD_TESTS "Build the core tests" ON)
else()
  option(GOLDSRCRENDERCORE_BUILD_TESTS "Build the core tests" OFF)
endif()

if(TARGET GoldSrcRenderCore)
  return()
endif()

add_library(GoldSrcRenderCore STATIC
  src/GoldSrcRenderAssetUtil.cpp
  src/GoldSrcRenderBSP.cpp
  src/GoldSrcRenderMDL.cpp)
target_compile_features(GoldSrcRenderCore PUBLIC cxx_std_17)
target_include_directories(GoldSrcRenderCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
set_target_properties(GoldSrcRenderCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

file(GLOB_RECURSE core_headers CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.inl")
target_sources(GoldSrcRenderCore PRIVATE ${core_headers})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Header Files" FILES ${core_headers})

# Generated BSP and MDL test files and the helpers that compare what they build, for the tests of every project
# reading these assets.
add_library(GoldSrcRenderCoreTestFiles INTERFACE)
target_include_directories(GoldSrcRenderCoreTestFiles INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(GoldSrcRenderCoreTestFiles INTERFACE GoldSrcRenderCore)

# Private sources make this header-only target visible in IDEs without propagating files to consumers.
file(GLOB_RECURSE test_headers CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.inl")
target_sources(GoldSrcRenderCoreTestFiles PRIVATE ${test_headers})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Header Files" FILES ${test_headers})

if(GOLDSRCRENDERCORE_BUILD_TESTS)
  enable_testing()
  add_executable(goldsrc_render_core_tests tests/GoldSrcRenderCoreTests.cpp)
  target_link_libraries(goldsrc_render_core_tests PRIVATE GoldSrcRenderCoreTestFiles)
  add_test(NAME goldsrc_render_core COMMAND goldsrc_render_core_tests)
endif()
