# PathView Test Suite
cmake_minimum_required(VERSION 3.20)

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()
include(GoogleTest)

# Find dependencies for tests
find_package(OpenSlide REQUIRED)
find_package(Protobuf REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(PNG REQUIRED)
find_package(simdjson CONFIG REQUIRED)

# ============================================================================
# Unit Tests (Pure C++ - No SDL dependencies)
# ============================================================================

add_executable(unit_tests
    unit/test_main.cpp
    unit/animation_test.cpp
    unit/viewport_test.cpp
    unit/tile_cache_test.cpp
    unit/polygon_index_test.cpp
    unit/polygon_triangulator_test.cpp
    unit/slide_renderer_test.cpp
    unit/navigation_lock_test.cpp
    unit/png_encoder_test.cpp
    unit/snapshot_manager_test.cpp
    unit/action_card_test.cpp
)

target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/loaders
    ${CMAKE_SOURCE_DIR}/src/api/http
    ${CMAKE_SOURCE_DIR}/protobuf
    ${CMAKE_SOURCE_DIR}/test/mocks
    ${CMAKE_SOURCE_DIR}/external/cpp-mcp/common  # For json.hpp
    ${OPENSLIDE_INCLUDE_DIRS}
    ${Protobuf_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
)

# MSVC-specific compile options
if(MSVC)
    target_compile_options(unit_tests PRIVATE
        /W4 /WX- /utf-8 /bigobj /MP
    )
    target_compile_definitions(unit_tests PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# Link only the necessary core components
target_link_libraries(unit_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    ${OPENSLIDE_LIBRARIES}
    ${Protobuf_LIBRARIES}
    absl::log
    absl::log_internal_check_op
    absl::log_internal_message
    absl::hash
    PNG::PNG
    simdjson::simdjson
)

# Link against source files directly to avoid SDL dependencies
target_sources(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/Animation.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TileCache.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TileLoadThreadPool.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PolygonIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PolygonTriangulator.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SlideRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SlideLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TextureManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PolygonOverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PolygonLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/NavigationLock.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PNGEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ActionCard.cpp
    ${CMAKE_SOURCE_DIR}/src/api/http/SnapshotManager.cpp
    ${CMAKE_SOURCE_DIR}/src/loaders/ProtobufPolygonLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/loaders/JSONPolygonLoader.cpp
    ${CMAKE_SOURCE_DIR}/protobuf/cell_polygons.pb.cc
)

# For SDL-dependent files, we need to link SDL2
find_package(SDL2 CONFIG REQUIRED)
target_link_libraries(unit_tests PRIVATE SDL2::SDL2)

# Platform-specific libraries
if(WIN32)
    # Windows: Link Winsock for socket types in NavigationLock
    target_link_libraries(unit_tests PRIVATE ws2_32)
endif()

# Register unit tests with CTest
gtest_discover_tests(unit_tests
    PROPERTIES
        LABELS "unit"
        TIMEOUT 30
)

# ============================================================================
# Integration Tests (Require MCP server and HTTP client)
# ============================================================================

# Note: Integration tests require the MCP server to be running
# These tests will be implemented in Phase 3

# add_executable(integration_tests
#     integration/mcp_animation_test.cpp
#     integration/mcp_viewport_test.cpp
#     integration/mcp_polygon_test.cpp
#     integration/http_endpoint_test.cpp
# )
#
# target_include_directories(integration_tests PRIVATE
#     ${CMAKE_SOURCE_DIR}/src/api
#     ${CMAKE_SOURCE_DIR}/src/core
# )
#
# target_link_libraries(integration_tests PRIVATE
#     GTest::gtest
#     GTest::gtest_main
#     # HTTP client library (curl or cpp-httplib)
# )
#
# gtest_discover_tests(integration_tests
#     PROPERTIES
#         LABELS "integration"
#         TIMEOUT 60
# )

message(STATUS "=== Test Configuration ===")
message(STATUS "GoogleTest version: 1.14.0")
message(STATUS "Unit tests: Enabled")
message(STATUS "Integration tests: Disabled (Phase 3)")
message(STATUS "=========================")
