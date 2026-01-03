# PathView

[![CICD](https://github.com/PABannier/PathView/actions/workflows/CICD.yml/badge.svg)](https://github.com/PABannier/PathView/actions/workflows/CICD.yml/badge.svg)

PathView is a whole-slide image (WSI) viewer for digital pathology. It combines high-performance tiled rendering and polygon overlays for cell segmentation data.

![PathView preview](./assets/pathview.gif)

## Features

- Smooth pan/zoom WSI viewing with multiresolution tile loading
- Overview minimap with click-to-jump navigation
- Polygon overlay rendering with class-based styling (protobuf)
- Cross-platform support: macOS, Linux
- MCP server for programmatic control, screenshots, and ROI analysis

## Requirements

- CMake and a C++17 compiler
- OpenSlide (system install)
- vcpkg for third-party deps (SDL2, SDL2_image, nativefiledialog-extended, protobuf)

## Build

### macOS / Linux

```bash
cd /path/to/pathview

# Set the triplet for your platform
export VCPKG_TARGET_TRIPLET=arm64-osx   # macOS Apple Silicon
# export VCPKG_TARGET_TRIPLET=x64-osx   # macOS Intel
# export VCPKG_TARGET_TRIPLET=x64-linux # Linux

# Use Homebrew vcpkg on macOS
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$(brew --prefix vcpkg)/scripts/buildsystems/vcpkg.cmake

# Or use vcpkg from source
# cmake -B build \
#   -DCMAKE_BUILD_TYPE=Release \
#   -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build -j$(nproc)
./build/pathview
```

## Usage

- Open a slide: `File -> Open Slide...` or `Ctrl+O`
- Zoom: mouse wheel or trackpad pinch
- Pan: left mouse drag
- Reset view: `View -> Reset View`
- Use the minimap to jump to a region

## AI Agent Integration

PathView ships an MCP (Model Context Protocol) server for programmatic control, screenshots, and ROI analysis.

```bash
# Terminal 1: GUI
./build/pathview

# Terminal 2: MCP server
./build/pathview-mcp
```

- HTTP+SSE control: `http://127.0.0.1:9000`
- Snapshot server: `http://127.0.0.1:8080`

Full documentation: `docs/AI_AGENT_GUIDE.md`

### MCP Tools

The MCP server exposes tools across these categories:

- Session: `agent_hello`
- Slide: `load_slide`, `get_slide_info`
- Navigation (requires lock): `nav_lock`, `nav_unlock`, `pan`, `zoom`, `center_on`, `move_camera`, `reset_view`
- Snapshots: `capture_snapshot`, `/snapshot/{id}`, `/stream?fps=N`
- Polygons: `load_polygons`, `query_polygons`, `set_polygon_visibility`
- Annotations/ROI: `create_annotation`, `list_annotations`, `get_annotation`, `delete_annotation`, `compute_roi_metrics`
- Progress tracking: `create_action_card`, `update_action_card`, `append_action_card_log`, `list_action_cards`, `delete_action_card`

### MCP Usage

Recommended flow for automation:

1. `agent_hello` to create a session.
2. `nav_lock` before any navigation calls.
3. Use `move_camera` + `await_move` for smooth transitions.
4. `capture_snapshot` for screenshots.
5. `create_annotation` + `compute_roi_metrics` for ROI analysis.
6. `nav_unlock` when done.

## Supported Formats

All formats supported by OpenSlide, including:
- Aperio (.svs, .tif)
- Hamamatsu (.vms, .vmu, .ndpi)
- Leica (.scn)
- MIRAX (.mrxs)
- Philips (.tiff)
- Sakura (.svslide)
- Trestle (.tif)
- Ventana (.bif, .tif)
- Generic tiled TIFF

## Contributing

Issues and pull requests are welcome. Please include platform details and repro steps for bugs.

## License

MIT
