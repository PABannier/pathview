# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PathView is a cross-platform C++ digital pathology slide viewer for viewing whole-slide images (WSI). It supports OpenSlide formats and includes polygon overlay capabilities for displaying cell segmentation data.

## Build System

### Initial Setup

```bash
# macOS Apple Silicon
export VCPKG_DEFAULT_TRIPLET=arm64-osx
vcpkg install sdl2 sdl2-image

# Configure
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$(brew --prefix vcpkg)/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build -j$(nproc)
```

### Common Build Commands

```bash
# Release build
cmake --build build -j$(nproc)

# Debug build
cmake -B build-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build-debug

# Run
./build/pathview
./build-debug/pathview  # debug version
```

### Regenerating Protocol Buffers

When modifying `protobuf/cell_polygons.proto`:

```bash
cd protobuf
protoc -I=./ --cpp_out=./ ./cell_polygons.proto
```

The generated files (`cell_polygons.pb.{h,cc}`) are committed to the repository and compiled as part of the main build.

## Architecture

### Core Components

- **Application** (`Application.{h,cpp}`): Main controller, SDL/ImGui initialization, event loop, and UI integration
- **SlideLoader** (`SlideLoader.{h,cpp}`): RAII wrapper around OpenSlide C API for loading whole-slide images
- **Viewport** (`Viewport.{h,cpp}`): Camera/viewport management with coordinate transformations between screen space and slide space
- **SlideRenderer** (`SlideRenderer.{h,cpp}`): Rendering orchestration, pyramid level selection, and tile enumeration
- **TileCache** (`TileCache.{h,cpp}`): LRU cache for tile pixel data with 512MB default memory limit
- **TextureManager** (`TextureManager.{h,cpp}`): SDL texture creation and management
- **Minimap** (`Minimap.{h,cpp}`): Overview widget with click-to-jump navigation

### Polygon Overlay System

The polygon overlay system displays cell segmentation data loaded from Protocol Buffer files on top of whole-slide images.

**Components:**

- **PolygonOverlay** (`PolygonOverlay.{h,cpp}`): Main overlay renderer with level-of-detail (LOD) system
  - LOD levels: SKIP (<2px), POINT (2-4px), BOX (4-10px), SIMPLIFIED (10-30px), FULL (30+px)
  - Handles class-based coloring and opacity control
  - Batches rendering by class ID for performance

- **PolygonLoader** (`PolygonLoader.{h,cpp}`): Loads polygon data from protobuf files
  - Parses `histowmics.SlideSegmentationData` messages
  - Maps string cell types to integer class IDs
  - Generates default colors for classes

- **PolygonIndex** (`PolygonIndex.{h,cpp}`): Spatial grid-based index for efficient polygon queries
  - Accelerates viewport-based polygon culling
  - O(k) lookups where k is polygons per grid cell

- **PolygonTriangulator** (`PolygonTriangulator.{h,cpp}`): Converts polygon vertices to triangles for rendering

**Data Flow:**
1. Load `.pb`/`.protobuf` file via `PolygonLoader::Load()`
2. Build spatial index in `PolygonIndex::Build()`
3. Query visible polygons per frame via `PolygonIndex::QueryRegion()`
4. Determine LOD level based on screen size
5. Batch and render polygons by class ID

**Coordinate System:**
- Polygons use level 0 slide coordinates (highest resolution)
- Viewport transforms slide coords to screen coords for rendering
- Bounding boxes cached on polygons for culling

### Coordinate Systems

- **Slide coordinates**: Level 0 (highest resolution) pixel coordinates from OpenSlide
- **Screen coordinates**: Window pixel coordinates (ImGui/SDL)
- **Viewport transformations**: `Viewport::SlideToScreen()` and `Viewport::ScreenToSlide()`

### Rendering Pipeline

1. **Level Selection**: `SlideRenderer::SelectLevel()` chooses optimal pyramid level based on zoom
2. **Tile Enumeration**: `EnumerateVisibleTiles()` computes visible tiles for current viewport
3. **Cache Lookup**: Check `TileCache` for existing tile data
4. **Load & Decode**: Load tile via OpenSlide if cache miss
5. **Texture Creation**: `TextureManager` creates SDL texture from pixel data
6. **Render**: Draw tiles to screen with proper positioning
7. **Polygon Overlay**: Render polygons on top if loaded and visible

### Memory Management

- **Tile Cache**: LRU eviction when memory exceeds 512MB limit
- **Polygon Data**: Lazy triangulation cached on `Polygon::triangleIndices`
- **Textures**: Managed by `TextureManager`, cleaned up on shutdown

## Key Dependencies

- **OpenSlide**: Whole-slide image I/O (via Homebrew/system package manager)
- **SDL2**: Window management, rendering, input (via vcpkg)
- **SDL2_image**: Image loading utilities (via vcpkg)
- **ImGui**: Immediate mode GUI (vendored in `external/imgui/`)
- **nativefiledialog-extended**: Native file picker dialogs (via vcpkg)
- **Protocol Buffers**: Cell segmentation data serialization (via vcpkg or Homebrew)

## Development Patterns

### Adding New Features

When adding UI features:
1. Add ImGui widgets in `Application::RenderUI()`
2. Store state in `Application` member variables
3. Handle events in `Application::ProcessEvents()`

When modifying rendering:
1. Changes to tile loading go in `SlideRenderer`
2. Viewport/camera logic goes in `Viewport`
3. Caching policy changes go in `TileCache`

When extending polygon overlays:
1. New rendering modes go in `PolygonOverlay::Render*()`
2. Spatial query optimizations go in `PolygonIndex`
3. File format changes require updating protobuf schema and recompiling

### Debugging

```bash
# macOS
lldb ./build-debug/pathview

# Linux
gdb ./build-debug/pathview
```

Check cache statistics in the UI for performance debugging.

## File Formats

**Supported Slide Formats** (via OpenSlide):
- Aperio (.svs, .tif)
- Hamamatsu (.vms, .vmu, .ndpi)
- Leica (.scn)
- MIRAX (.mrxs)
- Philips (.tiff)
- Generic tiled TIFF

**Polygon Format**:
- Protocol Buffer files (`.pb`, `.protobuf`)
- Schema: `histowmics.SlideSegmentationData` (see `protobuf/cell_polygons.proto`)
- Contains cell polygons with coordinates, cell types, and confidence scores

## Common Issues

### ImGui Not Found
ImGui is vendored in `external/imgui/`. If missing, the CMake configure will fail with instructions to download it.

### OpenSlide Not Found
Ensure OpenSlide is installed via Homebrew (macOS) or apt (Linux). May need to set `CMAKE_PREFIX_PATH` to the installation directory.

### vcpkg Triplet Mismatch
Set `VCPKG_DEFAULT_TRIPLET` to match your architecture before running `vcpkg install`:
- `arm64-osx` for Apple Silicon
- `x64-osx` for Intel Mac
- `x64-linux` for Linux

### Polygon File Not Loading
Ensure the protobuf schema matches the file format. Regenerate C++ files if the schema changed using the protoc command above.
