# PathView - Digital Pathology Slide Viewer

A cross-platform C++ application for viewing whole-slide images (WSI) commonly used in digital pathology. Built with ImGui, SDL2, and OpenSlide.

## Features

- Load and view whole-slide images (.svs, .tiff, .ndpi, etc.)
- Smooth zoom and pan navigation
- Multi-resolution tile loading for performance
- Overview minimap with click-to-jump
- Cross-platform (Windows, macOS, and Linux)

## Prerequisites

### Windows (MSVC)

1. **Visual Studio 2019 or later** with C++ desktop development workload
2. **vcpkg** - C++ package manager
3. **OpenSlide Windows binaries** - Download from [openslide.org](https://openslide.org/download/)

```powershell
# Install vcpkg (if not already installed)
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat

# Set environment variable (add to system PATH for convenience)
set VCPKG_ROOT=C:\vcpkg
```

### macOS

```bash
# Install Homebrew if not present
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake openslide vcpkg
```

### Linux (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install cmake build-essential libopenslide-dev git curl
```

### vcpkg Setup (Unix)

If not already installed via package manager:

```bash
# Clone vcpkg
git clone https://github.com/Microsoft/vcpkg.git ~/vcpkg
cd ~/vcpkg
./bootstrap-vcpkg.sh

# Add to shell profile (optional)
export VCPKG_ROOT=~/vcpkg
export PATH=$VCPKG_ROOT:$PATH
```

## Building

### Windows (MSVC)

```powershell
cd C:\path\to\pathview

# Set triplet
set VCPKG_DEFAULT_TRIPLET=x64-windows

# Download OpenSlide Windows binaries from https://openslide.org/download/
# Extract to C:\openslide (or set OPENSLIDE_HOME environment variable)

# Configure with CMake (Visual Studio 2022)
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DCMAKE_PREFIX_PATH=C:\openslide

# CMake will install vcpkg dependencies from vcpkg.json (manifest mode).
# Optional: pre-install to warm caches
# %VCPKG_ROOT%\vcpkg install --triplet x64-windows

# Build Release
cmake --build build --config Release

# Run
.\build\Release\pathview.exe
```

### macOS / Linux

#### 1. Configure with CMake (vcpkg manifest)

```bash
cd /path/to/pathview

# For macOS Apple Silicon
export VCPKG_TARGET_TRIPLET=arm64-osx

# For macOS Intel
# export VCPKG_TARGET_TRIPLET=x64-osx

# For Linux
# export VCPKG_TARGET_TRIPLET=x64-linux
```

#### 2. Configure with CMake

**Using Homebrew vcpkg (macOS)**:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$(brew --prefix vcpkg)/scripts/buildsystems/vcpkg.cmake
```

**Using vcpkg from source**:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake
```

#### 3. Build

```bash
cmake --build build -j$(nproc)
```

#### 4. Run

```bash
./build/pathview
```

## Usage

### Opening a Slide

1. Launch PathView
2. Click "File" → "Open Slide..." or press `Ctrl+O`
3. Select a whole-slide image file (.svs, .tiff, etc.)

### Navigation

- **Zoom**: Mouse wheel or trackpad pinch
- **Pan**: Click and drag with left mouse button
- **Reset View**: "View" → "Reset View"
- **Minimap**: Click on the overview in the corner to jump to that region

## AI Agent Integration

PathView includes an MCP (Model Context Protocol) server that enables AI agents to programmatically control the viewer, capture screenshots, and analyze tissue regions.

### Starting the MCP Server

```bash
# Terminal 1: Start PathView GUI
./build/pathview

# Terminal 2: Start MCP server
./build/pathview-mcp
```

The MCP server exposes:
- HTTP+SSE transport on `http://127.0.0.1:9000`
- HTTP snapshot server on `http://127.0.0.1:8080`
- 27 tools for agent control

### Quick Start for AI Agents

See [docs/AI_AGENT_GUIDE.md](docs/AI_AGENT_GUIDE.md) for complete documentation.

**Essential workflow:**
1. `agent_hello` - Connect and get session info
2. `nav_lock` - Acquire exclusive navigation control (required!)
3. `create_action_card` - Track progress in UI
4. `move_camera` + `await_move` - Navigate smoothly
5. `capture_snapshot` - Get viewport screenshot
6. `create_annotation` - Draw ROI, get cell metrics
7. `update_action_card` - Update progress
8. `nav_unlock` - Release control

### Tool Categories

- **Session:** `agent_hello`
- **Slide:** `load_slide`, `get_slide_info`
- **Navigation (requires lock):** `nav_lock`, `nav_unlock`, `pan`, `zoom`, `center_on`, `move_camera`, `reset_view`
- **Snapshots:** `capture_snapshot`, `/snapshot/{id}`, `/stream?fps=N`
- **Polygons:** `load_polygons`, `query_polygons`, `set_polygon_visibility`
- **Annotations/ROI:** `create_annotation`, `list_annotations`, `get_annotation`, `delete_annotation`, `compute_roi_metrics`
- **Progress Tracking:** `create_action_card`, `update_action_card`, `append_action_card_log`, `list_action_cards`, `delete_action_card`

### Key Features

- **Navigation Lock:** Prevents user interference during agent analysis
- **Smooth Camera Movement:** Animated transitions with completion tracking
- **Live Screenshot Streaming:** MJPEG stream at up to 30 FPS
- **Automatic Cell Counting:** ROI annotations compute per-class cell counts
- **Progress Visualization:** Action cards show agent reasoning in UI

## Project Structure

```
pathview/
├── CMakeLists.txt           # Build configuration
├── vcpkg.json               # Dependency manifest
├── cmake/
│   ├── FindOpenSlide.cmake  # Custom OpenSlide find module
│   ├── FindSDL2_image.cmake # Custom SDL2_image find module
│   └── FindNFD.cmake        # Custom NFD (native file dialog) find module
├── src/
│   ├── main.cpp             # Entry point
│   ├── Application.{h,cpp}  # Main app controller
│   ├── SlideLoader.{h,cpp}  # OpenSlide wrapper
│   ├── Viewport.{h,cpp}     # Camera/viewport management
│   ├── SlideRenderer.{h,cpp}# Rendering orchestration
│   ├── TileCache.{h,cpp}    # LRU tile cache
│   ├── TextureManager.{h,cpp}# SDL texture management
│   └── Minimap.{h,cpp}      # Overview widget
└── external/
    └── imgui/               # ImGui library (downloaded)
```

## Development

### Debug Build

**Windows**:
```powershell
cmake -B build-debug -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake

cmake --build build-debug --config Debug
```

**macOS/Linux**:
```bash
cmake -B build-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build-debug
```

### Debugging

**Windows (Visual Studio)**:
Open `build\pathview.sln` in Visual Studio and use the integrated debugger.

**macOS**:
```bash
lldb ./build-debug/pathview
```

**Linux**:
```bash
gdb ./build-debug/pathview
```

## Troubleshooting

### OpenSlide Not Found

Ensure OpenSlide is installed:

**Windows**:
```powershell
# Download Windows binaries from https://openslide.org/download/
# Extract and set OPENSLIDE_HOME or CMAKE_PREFIX_PATH
cmake -B build ... -DCMAKE_PREFIX_PATH=C:\openslide
```

**macOS**:
```bash
brew list openslide
pkg-config --libs openslide
```

**Linux**:
```bash
dpkg -l | grep openslide
pkg-config --libs openslide
```

If installed but not found, set `CMAKE_PREFIX_PATH` (or use a vcpkg OpenSlide build if available):

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/opt/homebrew ...  # macOS Homebrew
```

### SDL2 Not Found with vcpkg

Verify vcpkg installation:

```bash
~/vcpkg/vcpkg list | grep sdl2
```

Ensure the correct triplet is set for your architecture.

### SDL2_image Not Found

The project prefers SDL2_image's vcpkg config package and falls back to a custom `FindSDL2_image.cmake` module for system installs.

First, verify SDL2_image is installed:

```bash
# Check vcpkg installation
~/vcpkg/vcpkg list | grep sdl2-image

# Check if library exists in vcpkg_installed directory
ls vcpkg_installed/*/lib/libSDL2_image*
```

If the library is installed but not found, ensure you've run `vcpkg install` with the correct triplet:

```bash
# macOS Apple Silicon
export VCPKG_DEFAULT_TRIPLET=arm64-osx
vcpkg install

# macOS Intel
export VCPKG_DEFAULT_TRIPLET=x64-osx
vcpkg install

# Linux
export VCPKG_DEFAULT_TRIPLET=x64-linux
vcpkg install
```

The custom find module (`cmake/FindSDL2_image.cmake`) searches in:
1. `vcpkg_installed/<triplet>/` directory (project-local)
2. System paths (`/opt/homebrew`, `/usr/local`, `/usr`)

### NFD (Native File Dialog) Not Found

The project prefers the vcpkg `nfd` config package and falls back to a custom `FindNFD.cmake` module if needed.

Verify installation:

```bash
# Check vcpkg installation
~/vcpkg/vcpkg list | grep nativefiledialog

# Check if library exists
ls vcpkg_installed/*/lib/libnfd*
```

The custom find module (`cmake/FindNFD.cmake`) searches in:
1. `vcpkg_installed/<triplet>/` directory (project-local)
2. System paths (`/opt/homebrew`, `/usr/local`, `/usr`)

**Note**: On macOS, NFD requires the AppKit and UniformTypeIdentifiers frameworks. On Linux, it requires GTK3 (which the find module automatically detects via pkg-config). On Windows, NFD links against COM libraries (comctl32, ole32, uuid, shell32) automatically.

### Windows-Specific Issues

**vcpkg triplet mismatch**:
Ensure you set the correct triplet before running vcpkg install:
```powershell
set VCPKG_DEFAULT_TRIPLET=x64-windows
vcpkg install
```

**DLL not found errors at runtime**:
Ensure the OpenSlide DLLs are in your PATH or copy them to the same directory as the executable:
```powershell
copy C:\openslide\bin\*.dll .\build\Release\
```

**Winsock initialization errors**:
The application initializes Winsock automatically. If you see socket-related errors, ensure no firewall is blocking localhost connections on port 9999.

### ImGui Errors

If ImGui files are missing:

```bash
cd external/imgui
git clone --depth 1 --branch v1.91.7 https://github.com/ocornut/imgui.git .
```

## Architecture

- **SlideLoader**: RAII wrapper around OpenSlide C API
- **Viewport**: Manages camera state and coordinate transformations
- **TileCache**: LRU cache for tile data (512MB default limit)
- **SlideRenderer**: Orchestrates rendering, selects pyramid levels, enumerates tiles
- **TextureManager**: Creates and manages SDL textures
- **Minimap**: Overview widget with click-to-jump navigation
- **Application**: Main controller, event handling, UI integration

## Supported Formats

PathView supports any format that OpenSlide can read:

- Aperio (.svs, .tif)
- Hamamatsu (.vms, .vmu, .ndpi)
- Leica (.scn)
- MIRAX (.mrxs)
- Philips (.tiff)
- Sakura (.svslide)
- Trestle (.tif)
- Ventana (.bif, .tif)
- Generic tiled TIFF

## License

MIT License (add your license details here)

## Contributing

Contributions welcome! Please open an issue or pull request.

## Acknowledgments

- [OpenSlide](https://openslide.org/) - Whole-slide image reading library
- [Dear ImGui](https://github.com/ocornut/imgui) - Immediate mode GUI library
- [SDL2](https://www.libsdl.org/) - Cross-platform multimedia library
- [vcpkg](https://vcpkg.io/) - C++ package manager
