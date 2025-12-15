# Rules and Tool

## MCP Agent Mail: coordination for multi-agent workflows

What it is
- A mail-like layer that lets coding agents coordinate asynchronously via MCP tools and resources.
- Provides identities, inbox/outbox, searchable threads, and advisory file reservations, with human-auditable artifacts in Git.

Why it's useful
- Prevents agents from stepping on each other with explicit file reservations (leases) for files/globs.
- Keeps communication out of your token budget by storing messages in a per-project archive.
- Offers quick reads (`resource://inbox/...`, `resource://thread/...`) and macros that bundle common flows.

How to use effectively
1) Same repository
   - Register an identity: call `ensure_project`, then `register_agent` using this repo's absolute path as `project_key`.
   - Reserve files before you edit: `file_reservation_paths(project_key, agent_name, ["src/**"], ttl_seconds=3600, exclusive=true)` to signal intent and avoid conflict.
   - Communicate with threads: use `send_message(..., thread_id="FEAT-123")`; check inbox with `fetch_inbox` and acknowledge with `acknowledge_message`.
   - Read fast: `resource://inbox/{Agent}?project=<abs-path>&limit=20` or `resource://thread/{id}?project=<abs-path>&include_bodies=true`.
   - Tip: set `AGENT_NAME` in your environment so the pre-commit guard can block commits that conflict with others' active exclusive file reservations.

2) Across different repos in one project (e.g., Next.js frontend + FastAPI backend)
   - Option A (single project bus): register both sides under the same `project_key` (shared key/path). Keep reservation patterns specific (e.g., `frontend/**` vs `backend/**`).
   - Option B (separate projects): each repo has its own `project_key`; use `macro_contact_handshake` or `request_contact`/`respond_contact` to link agents, then message directly. Keep a shared `thread_id` (e.g., ticket key) across repos for clean summaries/audits.

Macros vs granular tools
- Prefer macros when you want speed or are on a smaller model: `macro_start_session`, `macro_prepare_thread`, `macro_file_reservation_cycle`, `macro_contact_handshake`.
- Use granular tools when you need control: `register_agent`, `file_reservation_paths`, `send_message`, `fetch_inbox`, `acknowledge_message`.

Common pitfalls
- "from_agent not registered": always `register_agent` in the correct `project_key` first.
- "FILE_RESERVATION_CONFLICT": adjust patterns, wait for expiry, or use a non-exclusive reservation when appropriate.
- Auth errors: if JWT+JWKS is enabled, include a bearer token with a `kid` that matches server JWKS; static bearer is used only when JWT is disabled.

## Integrating with Beads (dependency-aware task planning)

Beads provides a lightweight, dependency-aware issue database and a CLI (`bd`) for selecting "ready work," setting priorities, and tracking status. It complements MCP Agent Mail's messaging, audit trail, and file-reservation signals. Project: [steveyegge/beads](https://github.com/steveyegge/beads)

Recommended conventions
- **Single source of truth**: Use **Beads** for task status/priority/dependencies; use **Agent Mail** for conversation, decisions, and attachments (audit).
- **Shared identifiers**: Use the Beads issue id (e.g., `bd-123`) as the Mail `thread_id` and prefix message subjects with `[bd-123]`.
- **Reservations**: When starting a `bd-###` task, call `file_reservation_paths(...)` for the affected paths; include the issue id in the `reason` and release on completion.

Typical flow (agents)
1) **Pick ready work** (Beads)
   - `bd ready --json` → choose one item (highest priority, no blockers)
2) **Reserve edit surface** (Mail)
   - `file_reservation_paths(project_key, agent_name, ["src/**"], ttl_seconds=3600, exclusive=true, reason="bd-123")`
3) **Announce start** (Mail)
   - `send_message(..., thread_id="bd-123", subject="[bd-123] Start: <short title>", ack_required=true)`
4) **Work and update**
   - Reply in-thread with progress and attach artifacts/images; keep the discussion in one thread per issue id
5) **Complete and release**
   - `bd close bd-123 --reason "Completed"` (Beads is status authority)
   - `release_file_reservations(project_key, agent_name, paths=["src/**"])`
   - Final Mail reply: `[bd-123] Completed` with summary and links

Mapping cheat-sheet
- **Mail `thread_id`** ↔ `bd-###`
- **Mail subject**: `[bd-###] …`
- **File reservation `reason`**: `bd-###`
- **Commit messages (optional)**: include `bd-###` for traceability

Event mirroring (optional automation)
- On `bd update --status blocked`, send a high-importance Mail message in thread `bd-###` describing the blocker.
- On Mail "ACK overdue" for a critical decision, add a Beads label (e.g., `needs-ack`) or bump priority to surface it in `bd ready`.

Pitfalls to avoid
- Don't create or manage tasks in Mail; treat Beads as the single task queue.
- Always include `bd-###` in message `thread_id` to avoid ID drift across tools.

---

### ast-grep vs ripgrep (quick guidance)

**Use `ast-grep` when structure matters.** It parses code and matches AST nodes, so results ignore comments/strings, understand syntax, and can **safely rewrite** code.

* Refactors/codemods: rename APIs, change import forms, rewrite call sites or variable kinds.
* Policy checks: enforce patterns across a repo (`scan` with rules + `test`).
* Editor/automation: LSP mode; `--json` output for tooling.

**Use `ripgrep` when text is enough.** It’s the fastest way to grep literals/regex across files.

* Recon: find strings, TODOs, log lines, config values, or non-code assets.
* Pre-filter: narrow candidate files before a precise pass.

**Rule of thumb**

* Need correctness over speed, or you’ll **apply changes** → start with `ast-grep`.
* Need raw speed or you’re just **hunting text** → start with `rg`.
* Often combine: `rg` to shortlist files, then `ast-grep` to match/modify with precision.

**Snippets**

Find structured code (ignores comments/strings):

```bash
ast-grep run -l TypeScript -p 'import $X from "$P"'
```

Codemod (only real `var` declarations become `let`):

```bash
ast-grep run -l JavaScript -p 'var $A = $B' -r 'let $A = $B' -U
```

Quick textual hunt:

```bash
rg -n 'console\.log\(' -t js
```

Combine speed + precision:

```bash
rg -l -t ts 'useQuery\(' | xargs ast-grep run -l TypeScript -p 'useQuery($A)' -r 'useSuspenseQuery($A)' -U
```

**Mental model**

* Unit of match: `ast-grep` = node; `rg` = line.
* False positives: `ast-grep` low; `rg` depends on your regex.
* Rewrites: `ast-grep` first-class; `rg` requires ad-hoc sed/awk and risks collateral edits.


---


### Morph Warp Grep — AI-powered code search

**Use `mcp__morph-mcp__warp_grep` for exploratory "how does X work?" questions.** An AI search agent automatically expands your query into multiple search patterns, greps the codebase, reads relevant files, and returns precise line ranges with full context—all in one call.

**Use `ripgrep` (via Grep tool) for targeted searches.** When you know exactly what you're looking for—a specific function name, error message, or config key—ripgrep is faster and more direct.

**Use `ast-grep` for structural code patterns.** When you need to match/rewrite AST nodes while ignoring comments/strings, or enforce codebase-wide rules.

**When to use what**

| Scenario | Tool | Why |
|----------|------|-----|
| "How is authentication implemented?" | `warp_grep` | Exploratory; don't know where to start |
| "Where is the L3 Guardian appeals system?" | `warp_grep` | Need to understand architecture, find multiple related files |
| "Find all uses of `useQuery(`" | `ripgrep` | Targeted literal search |
| "Find files with `console.log`" | `ripgrep` | Simple pattern, known target |
| "Rename `getUserById` → `fetchUser`" | `ast-grep` | Structural refactor, avoid comments/strings |
| "Replace all `var` with `let`" | `ast-grep` | Codemod across codebase |

**warp_grep strengths**

* **Reduces context pollution**: Returns only relevant line ranges, not entire files.
* **Intelligent expansion**: Turns "appeals system" into searches for `appeal`, `Appeals`, `guardian`, `L3`, etc.
* **One-shot answers**: Finds the 3-5 most relevant files with precise locations vs. manual grep→read cycles.
* **Natural language**: Works well with "how", "where", "what" questions.

**warp_grep usage**

```
mcp__morph-mcp__warp_grep(
  repoPath: "/data/projects/communitai",
  query: "How is the L3 Guardian appeals system implemented?"
)
```

Returns structured results with file paths, line ranges, and extracted code snippets.

**Rule of thumb**

* **Don't know where to look** → `warp_grep` (let AI find it)
* **Know the pattern** → `ripgrep` (fastest)
* **Need AST precision** → `ast-grep` (safest for rewrites)

**Anti-patterns**

* ❌ Using `warp_grep` to find a specific function name you already know → use `ripgrep`
* ❌ Using `ripgrep` to understand "how does X work" → wastes time with manual file reads
* ❌ Using `ripgrep` for codemods → misses comments/strings, risks collateral edits

### Morph Warp Grep vs Standard Grep

Warp Grep = AI agent that greps, reads, follows connections, returns synthesized context with line numbers.
Standard Grep = Fast regex match, you interpret results.

Decision: Can you write the grep pattern?
- Yes → Grep
- No, you have a question → mcp__morph-mcp__warp_grep

#### Warp Grep Queries (natural language, unknown location)
"How does the moderation appeals flow work?"
"Where are websocket connections managed?"
"What happens when a user submits a post?"
"Where is rate limiting implemented?"
"How does the auth session get validated on API routes?"
"What services touch the moderationDecisions table?"

#### Standard Grep Queries (known pattern, specific target)
pattern="fileAppeal"                          # known function name
pattern="class.*Service"                      # structural pattern
pattern="TODO|FIXME|HACK"                     # markers
pattern="processenv" path="apps/web"      # specific string
pattern="import.*from [']@/lib/db"          # import tracing

#### What Warp Grep Does Internally
One query → 15-30 operations: greps multiple patterns → reads relevant sections → follows imports/references → returns focused line ranges (e.g., l3-guardian.ts:269-440) not whole files.

#### Anti-patterns
| Don't Use Warp Grep For | Why | Use Instead |
|------------------------|-----|-------------|
| "Find function handleSubmit" | Known name | Grep pattern="handleSubmit" |
| "Read the auth config" | Known file | Read file_path="lib/auth/..." |
| "Check if X exists" | Boolean answer | Grep + check results |
| Quick lookups mid-task | 5-10s latency | Grep is 100ms |

#### When Warp Grep Wins
- Tracing data flow across files (API → service → schema → types)
- Understanding unfamiliar subsystems before modifying
- Answering "how" questions that span 3+ files
- Finding all touching points for a cross-cutting concerns

---

# Project Overview

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
