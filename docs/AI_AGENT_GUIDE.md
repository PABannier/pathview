# PathView AI Agent Integration Guide

## Overview

PathView is a cross-platform digital pathology slide viewer that supports programmatic control through the Model Context Protocol (MCP). The MCP server enables AI agents to:

- **Control the viewer** - Navigate, zoom, and position the camera smoothly
- **Capture screenshots** - Stream live viewport images for analysis
- **Analyze tissue regions** - Draw ROIs and compute cell counts automatically
- **Track progress** - Display agent reasoning and task status in the UI
- **Prevent user interference** - Lock navigation during autonomous analysis

**Use Cases:**
- Automated tissue scanning and region identification
- Cell counting and classification workflows
- Quality control and anomaly detection
- Guided pathology review with AI assistance

## Quick Start

### Connection

To connect your AI agent to PathView:

1. **Start PathView GUI:**
   ```bash
   ./build/pathview
   ```

2. **Start MCP Server:**
   ```bash
   ./build/pathview-mcp
   ```

3. **Connect via MCP:**
   - **MCP Server URL:** `http://127.0.0.1:9000`
   - **HTTP Snapshot Server:** `http://127.0.0.1:8080`
   - **Transport:** HTTP + Server-Sent Events (SSE)
   - **Protocol:** JSON-RPC 2.0 over MCP

### Essential Tool Bundle

The minimal set of tools for AI Cursor workflows (Step 7 MVP):

| Category | Tools | Purpose |
|----------|-------|---------|
| **Session** | `agent_hello` | Register agent and get session info |
| **Navigation Lock** | `nav_lock`, `nav_unlock`, `nav_lock_status` | Acquire exclusive control |
| **Camera Movement** | `move_camera`, `await_move` | Smooth animated navigation |
| **Screenshot Capture** | `capture_snapshot`, `/snapshot/{id}`, `/stream?fps=N` | Visual feedback |
| **ROI Analysis** | `create_annotation`, `compute_roi_metrics` | Draw regions, count cells |
| **Progress Tracking** | `create_action_card`, `update_action_card`, `append_action_card_log` | Display agent status |
| **Slide Management** | `load_slide`, `get_slide_info` | Load WSI files |
| **Polygon Overlays** | `load_polygons` | Load cell segmentation data |

**Total: 17 essential tools** (out of 27 available)

## Happy Path Workflow

This is the recommended workflow for AI agents performing autonomous tissue analysis.

### Step 1: Handshake

Register your agent and retrieve session information:

```python
from pathanalyze.mcp.client import MCPClient

client = MCPClient("http://127.0.0.1:9000")
await client.connect()

result = await client.call_tool("agent_hello", {
    "agent_name": "pathology-analyzer-v1",
    "agent_version": "1.0.0"
})

# Returns:
# {
#     "session_id": "550e8400-e29b-41d4-a716-446655440000",
#     "mcp_server_url": "http://127.0.0.1:9000",
#     "http_server_url": "http://127.0.0.1:8080",
#     "stream_url": "http://127.0.0.1:8080/stream?fps=10",
#     "navigation_locked": false,
#     "lock_owner": null,
#     "slide_loaded": false,
#     "polygons_loaded": false
# }
```

### Step 2: Load Resources

Load the whole-slide image and optional polygon overlay:

```python
# Load slide
slide = await client.call_tool("load_slide", {
    "path": "/path/to/slide.svs"
})
# Returns: {"width": 100000, "height": 80000, "levels": 7, "path": "..."}

# Load polygons (optional - for cell counting)
polygons = await client.call_tool("load_polygons", {
    "path": "/path/to/cells.pb"
})
# Returns: {"count": 45678, "classes": [1, 2, 3, 4]}
```

### Step 3: Acquire Navigation Lock

**CRITICAL:** All navigation tools require holding the lock!

```python
owner_uuid = "agent-uuid-12345"  # Use unique agent ID

lock = await client.call_tool("nav_lock", {
    "owner_uuid": owner_uuid,
    "ttl_seconds": 300  # 5 minutes (max: 3600)
})

# Returns:
# {
#     "success": true,
#     "lock_owner": "agent-uuid-12345",
#     "granted_at": "2025-12-16T10:30:00Z",
#     "ttl_ms": 300000
# }

# If lock acquisition fails:
# {
#     "success": false,
#     "error": "Navigation already locked by agent-xyz",
#     "lock_owner": "agent-xyz",
#     "locked_at": "2025-12-16T10:25:00Z"
# }
```

### Step 4: Create Action Card (Progress Tracking)

Display your analysis progress in the PathView UI:

```python
card = await client.call_tool("create_action_card", {
    "title": "Analyzing tumor region",
    "summary": "Scanning high-density cell area for classification",
    "reasoning": "Step 1: Navigate to ROI. Step 2: Capture snapshots. Step 3: Compute metrics.",
    "owner_uuid": owner_uuid
})

card_id = card["id"]
# Returns: {"id": "card-uuid", "status": "pending", "created_at": "..."}

# Update status to in_progress
await client.call_tool("update_action_card", {
    "id": card_id,
    "status": "in_progress"
})
```

### Step 5: Start Snapshot Streaming (Optional)

Subscribe to live viewport updates in a background task:

```python
import httpx
import asyncio

async def stream_snapshots(stream_url):
    async with httpx.AsyncClient(timeout=30.0) as http_client:
        async with http_client.stream("GET", stream_url) as response:
            async for chunk in response.aiter_bytes():
                # Process MJPEG frames
                # Parse multipart/x-mixed-replace boundary
                # Extract PNG images
                pass

# Start streaming in background
stream_task = asyncio.create_task(
    stream_snapshots("http://127.0.0.1:8080/stream?fps=10")
)
```

### Step 6: Navigate Camera with Completion Tracking

Move the camera smoothly and wait for animation to complete:

```python
# Move to target location
move_result = await client.call_tool("move_camera", {
    "center_x": 50000,  # Slide coordinates (level 0)
    "center_y": 30000,
    "zoom": 2.0,        # Zoom level (1.0 = fit to window)
    "duration_ms": 500  # Animation duration (50-5000ms)
})

token = move_result["token"]

# Poll for completion
max_polls = 40  # 2 seconds at 50ms intervals
for _ in range(max_polls):
    await asyncio.sleep(0.05)
    status = await client.call_tool("await_move", {"token": token})

    if status["completed"]:
        if status["aborted"]:
            print("Animation was interrupted")
        break

# Log progress
await client.call_tool("append_action_card_log", {
    "id": card_id,
    "message": f"Camera positioned at ({status['position']['x']}, {status['position']['y']})",
    "level": "info"
})
```

### Step 7: Capture Snapshot

Take a screenshot of the current viewport:

```python
snapshot = await client.call_tool("capture_snapshot", {
    "width": 1920,   # Optional, defaults to viewport size
    "height": 1080
})

snapshot_id = snapshot["id"]
snapshot_url = snapshot["url"]  # http://127.0.0.1:8080/snapshot/{id}

# Download snapshot
async with httpx.AsyncClient() as http_client:
    response = await http_client.get(snapshot_url)
    png_data = response.content

    # Save or process PNG
    with open(f"snapshot_{snapshot_id}.png", "wb") as f:
        f.write(png_data)
```

### Step 8: Draw ROI and Get Cell Metrics

Create an annotation polygon and automatically count cells:

```python
# Define ROI vertices in slide coordinates (level 0)
roi_vertices = [
    [48000, 28000],  # Top-left
    [52000, 28000],  # Top-right
    [52000, 32000],  # Bottom-right
    [48000, 32000]   # Bottom-left
]

annotation = await client.call_tool("create_annotation", {
    "vertices": roi_vertices,
    "name": "Tumor ROI 1"
})

# Returns:
# {
#     "id": 1,
#     "name": "Tumor ROI 1",
#     "vertex_count": 4,
#     "bounding_box": {"x": 48000, "y": 28000, "width": 4000, "height": 4000},
#     "area": 16000000.0,
#     "cell_counts": {
#         "1": 234,  # Class 1: Tumor cells
#         "2": 56,   # Class 2: Lymphocytes
#         "3": 12,   # Class 3: Stromal cells
#         "total": 302
#     }
# }

# If polygons not loaded, you'll get:
# {
#     "cell_counts": {},
#     "warning": "No polygons loaded. Cell counts unavailable. Use load_polygons to enable cell counting."
# }

cell_counts = annotation["cell_counts"]
total_cells = cell_counts.get("total", 0)

await client.call_tool("append_action_card_log", {
    "id": card_id,
    "message": f"ROI analysis complete: {total_cells} cells detected",
    "level": "success"
})
```

### Step 9: Quick Probe (No Annotation Persistence)

For temporary metric computation without creating an annotation:

```python
metrics = await client.call_tool("compute_roi_metrics", {
    "vertices": [[x1, y1], [x2, y2], [x3, y3], [x4, y4]]
})

# Returns same structure as create_annotation but doesn't persist
```

### Step 10: Complete and Unlock

Finish the workflow and release the navigation lock:

```python
# Update action card to completed
await client.call_tool("update_action_card", {
    "id": card_id,
    "status": "completed",
    "summary": f"Analysis complete. Found {total_cells} cells in target region."
})

# Release navigation lock
unlock_result = await client.call_tool("nav_unlock", {
    "owner_uuid": owner_uuid
})

# Returns: {"success": true}

# Cleanup action card (optional)
await client.call_tool("delete_action_card", {"id": card_id})
```

## Guardrails and Error Handling

### Navigation Lock Enforcement

**All viewport commands require holding the navigation lock:**

- `pan` - Pan viewport by delta
- `zoom` - Zoom in/out
- `zoom_at_point` - Zoom at specific screen point
- `center_on` - Center on slide coordinates
- `reset_view` - Reset to fit entire slide
- `move_camera` - Smooth animated movement

**Error if lock not held:**

```json
{
  "error": {
    "code": -32000,
    "message": "Navigation locked by agent-xyz. Use nav_lock tool to acquire control."
  }
}
```

**Lock auto-release conditions:**

1. **TTL expiration** - Lock expires after specified seconds (max 3600s)
2. **IPC disconnect** - Client disconnection triggers immediate release
3. **Explicit unlock** - Call `nav_unlock` with owner UUID

**Checking lock status:**

```python
status = await client.call_tool("nav_lock_status", {})

# Returns:
# {
#     "locked": true,
#     "owner": "agent-uuid-12345",
#     "locked_at": "2025-12-16T10:30:00Z",
#     "ttl_ms": 150000,  # Remaining TTL
#     "expired": false
# }
```

### Missing Resources

**Slide not loaded:**

All viewport and annotation commands require a loaded slide.

```json
{
  "error": {
    "message": "No slide loaded. Use load_slide tool to load a whole-slide image first."
  }
}
```

**Polygons not loaded:**

Polygon commands (`polygons.query`, `polygons.set_visibility`) require loaded polygons.

```json
{
  "error": {
    "message": "No polygons loaded. Use load_polygons tool to load cell segmentation data first."
  }
}
```

Annotation commands work without polygons but return empty cell counts with a warning:

```json
{
  "cell_counts": {},
  "warning": "No polygons loaded. Cell counts unavailable. Use load_polygons to enable cell counting."
}
```

## Tool Reference

### Session Management

#### `agent_hello`

Register agent and get session info.

**Parameters:**
- `agent_name` (string, required) - Agent identifier
- `agent_version` (string, optional) - Agent version

**Returns:**
```json
{
  "session_id": "uuid",
  "mcp_server_url": "http://127.0.0.1:9000",
  "http_server_url": "http://127.0.0.1:8080",
  "stream_url": "http://127.0.0.1:8080/stream?fps=10",
  "navigation_locked": false,
  "lock_owner": null,
  "slide_loaded": false,
  "polygons_loaded": false
}
```

### Navigation Lock

#### `nav_lock`

Acquire exclusive navigation control.

**Parameters:**
- `owner_uuid` (string, required) - Unique agent identifier
- `ttl_seconds` (integer, required) - Lock lifetime (1-3600)

**Returns:**
```json
{
  "success": true,
  "lock_owner": "agent-uuid",
  "granted_at": "2025-12-16T10:30:00Z",
  "ttl_ms": 300000
}
```

#### `nav_unlock`

Release navigation lock.

**Parameters:**
- `owner_uuid` (string, required) - Must match lock owner

**Returns:**
```json
{
  "success": true
}
```

#### `nav_lock_status`

Query current lock status.

**Returns:**
```json
{
  "locked": true,
  "owner": "agent-uuid",
  "locked_at": "2025-12-16T10:30:00Z",
  "ttl_ms": 150000,
  "expired": false
}
```

### Camera Movement

#### `move_camera`

Smooth animated camera movement with completion tracking.

**Parameters:**
- `center_x` (number, required) - Target center X (slide coordinates)
- `center_y` (number, required) - Target center Y (slide coordinates)
- `zoom` (number, required) - Target zoom level (1.0 = fit to window)
- `duration_ms` (number, optional) - Animation duration (50-5000ms, default: 300)

**Requires:** Navigation lock

**Returns:**
```json
{
  "token": "animation-token-uuid"
}
```

#### `await_move`

Poll for animation completion.

**Parameters:**
- `token` (string, required) - Animation token from `move_camera`

**Returns:**
```json
{
  "completed": true,
  "aborted": false,
  "position": {"x": 50000, "y": 30000},
  "zoom": 2.0
}
```

#### Other Camera Tools

- **`pan`** - Pan by delta: `{"dx": 100, "dy": 50}`
- **`zoom`** - Zoom by delta: `{"delta": 1.5}`
- **`zoom_at_point`** - Zoom at screen point: `{"screen_x": 960, "screen_y": 540, "delta": 1.5}`
- **`center_on`** - Center on slide coords: `{"x": 50000, "y": 30000}`
- **`reset_view`** - Reset to fit entire slide: `{}`

All require navigation lock.

### Screenshot Capture

#### `capture_snapshot`

Capture current viewport as PNG.

**Parameters:**
- `width` (integer, optional) - Target width (default: viewport width)
- `height` (integer, optional) - Target height (default: viewport height)

**Returns:**
```json
{
  "id": "snapshot-uuid",
  "url": "http://127.0.0.1:8080/snapshot/{id}",
  "width": 1920,
  "height": 1080
}
```

#### HTTP Snapshot Endpoints

- **`GET /snapshot/{id}`** - Retrieve PNG snapshot
- **`GET /stream?fps=N`** - MJPEG stream (1-30 FPS)

### Slide Management

#### `load_slide`

Load whole-slide image.

**Parameters:**
- `path` (string, required) - Path to slide file (.svs, .tif, etc.)

**Returns:**
```json
{
  "width": 100000,
  "height": 80000,
  "levels": 7,
  "path": "/path/to/slide.svs"
}
```

#### `get_slide_info`

Get current slide info and viewport state.

**Returns:**
```json
{
  "width": 100000,
  "height": 80000,
  "levels": 7,
  "path": "/path/to/slide.svs",
  "viewport": {
    "position": {"x": 25000, "y": 20000},
    "zoom": 1.0
  }
}
```

### Polygon Overlays

#### `load_polygons`

Load cell segmentation data.

**Parameters:**
- `path` (string, required) - Path to protobuf file (.pb)

**Returns:**
```json
{
  "count": 45678,
  "classes": [1, 2, 3, 4]
}
```

#### Other Polygon Tools

- **`query_polygons`** - Query polygons in region: `{"x": 0, "y": 0, "w": 1000, "h": 1000}`
- **`set_polygon_visibility`** - Show/hide overlay: `{"visible": true}`

### Annotations/ROI

#### `create_annotation`

Create ROI annotation with automatic cell counting.

**Parameters:**
- `vertices` (array, required) - Array of [x, y] points (min 3)
- `name` (string, optional) - Annotation name

**Returns:**
```json
{
  "id": 1,
  "name": "ROI 1",
  "vertex_count": 4,
  "bounding_box": {"x": 48000, "y": 28000, "width": 4000, "height": 4000},
  "area": 16000000.0,
  "cell_counts": {"1": 234, "2": 56, "total": 290}
}
```

#### `compute_roi_metrics`

Compute metrics without creating annotation.

**Parameters:**
- `vertices` (array, required) - Array of [x, y] points

**Returns:**
```json
{
  "bounding_box": {"x": 48000, "y": 28000, "width": 4000, "height": 4000},
  "area": 16000000.0,
  "perimeter": 16000.0,
  "cell_counts": {"1": 234, "2": 56, "total": 290}
}
```

#### Other Annotation Tools

- **`list_annotations`** - List all annotations: `{}`
- **`get_annotation`** - Get annotation by ID: `{"id": 1}`
- **`delete_annotation`** - Delete annotation: `{"id": 1}`

### Action Cards (Progress Tracking)

#### `create_action_card`

Create progress tracking card.

**Parameters:**
- `title` (string, required) - Card title
- `summary` (string, optional) - Short summary
- `reasoning` (string, optional) - Detailed reasoning/plan
- `owner_uuid` (string, optional) - Agent UUID

**Returns:**
```json
{
  "id": "card-uuid",
  "title": "Analyzing tumor region",
  "status": "pending",
  "created_at": "2025-12-16T10:30:00Z"
}
```

#### `update_action_card`

Update card status or content.

**Parameters:**
- `id` (string, required) - Card ID
- `status` (string, optional) - "pending", "in_progress", "completed", "failed", "cancelled"
- `summary` (string, optional) - Updated summary
- `reasoning` (string, optional) - Updated reasoning

**Returns:**
```json
{
  "success": true
}
```

#### `append_action_card_log`

Add log entry to card.

**Parameters:**
- `id` (string, required) - Card ID
- `message` (string, required) - Log message
- `level` (string, optional) - "info", "success", "warning", "error"

**Returns:**
```json
{
  "success": true,
  "log_count": 5
}
```

#### Other Action Card Tools

- **`list_action_cards`** - List all cards: `{}`
- **`delete_action_card`** - Delete card: `{"id": "card-uuid"}`

## Coordinate Systems

PathView uses two coordinate systems:

### Slide Coordinates (Level 0)

- Origin: Top-left corner of slide
- Units: Pixels at highest resolution (level 0)
- Used by: `move_camera`, `center_on`, `create_annotation`, polygon data
- Example: `{"x": 50000, "y": 30000}` = 50,000 pixels from left, 30,000 from top

### Screen Coordinates

- Origin: Top-left corner of viewport window
- Units: Screen pixels
- Used by: `zoom_at_point`, mouse events
- Example: `{"screen_x": 960, "screen_y": 540}` = center of 1920x1080 viewport

**Conversion:** Use viewport transformations (handled internally).

## Python Client Example

Complete example using pathanalyze MCP client:

```python
import asyncio
import httpx
from pathanalyze.mcp.client import MCPClient

async def analyze_slide():
    # Connect
    client = MCPClient("http://127.0.0.1:9000")
    await client.connect()

    # Handshake
    session = await client.call_tool("agent_hello", {
        "agent_name": "tumor-analyzer",
        "agent_version": "1.0.0"
    })

    # Load resources
    await client.call_tool("load_slide", {
        "path": "/data/slide.svs"
    })
    await client.call_tool("load_polygons", {
        "path": "/data/cells.pb"
    })

    # Acquire lock
    owner = "tumor-analyzer-12345"
    await client.call_tool("nav_lock", {
        "owner_uuid": owner,
        "ttl_seconds": 300
    })

    try:
        # Create action card
        card = await client.call_tool("create_action_card", {
            "title": "Scanning for tumor regions",
            "owner_uuid": owner
        })
        card_id = card["id"]

        # Navigate to region of interest
        move = await client.call_tool("move_camera", {
            "center_x": 50000,
            "center_y": 30000,
            "zoom": 2.0,
            "duration_ms": 500
        })

        # Wait for completion
        token = move["token"]
        while True:
            await asyncio.sleep(0.05)
            status = await client.call_tool("await_move", {"token": token})
            if status["completed"]:
                break

        # Capture snapshot
        snapshot = await client.call_tool("capture_snapshot", {})

        # Download snapshot
        async with httpx.AsyncClient() as http:
            response = await http.get(snapshot["url"])
            png_data = response.content

        # Analyze region
        annotation = await client.call_tool("create_annotation", {
            "vertices": [[48000, 28000], [52000, 28000],
                        [52000, 32000], [48000, 32000]],
            "name": "High-density region"
        })

        cell_count = annotation["cell_counts"].get("total", 0)

        # Update progress
        await client.call_tool("update_action_card", {
            "id": card_id,
            "status": "completed",
            "summary": f"Found {cell_count} cells in target region"
        })

    finally:
        # Always unlock
        await client.call_tool("nav_unlock", {"owner_uuid": owner})

    await client.disconnect()

if __name__ == "__main__":
    asyncio.run(analyze_slide())
```

## Troubleshooting

### Connection Refused

**Symptom:** `Connection refused` when connecting to MCP server.

**Solutions:**
1. Ensure PathView GUI is running: `./build/pathview`
2. Ensure MCP server is running: `./build/pathview-mcp`
3. Check ports are not in use: `lsof -i :9000` and `lsof -i :8080`
4. Verify firewall settings allow localhost connections

### Lock Acquisition Fails

**Symptom:** `nav_lock` returns `{"success": false}`

**Solutions:**
1. Check if another agent holds the lock: `nav_lock_status`
2. Wait for TTL expiration or use force release (UI button)
3. Ensure unique `owner_uuid` for each agent instance
4. Check IPC connection is active

### Snapshots Not Appearing

**Symptom:** `capture_snapshot` returns URL but 404 on GET request.

**Solutions:**
1. Ensure HTTP server is running on port 8080
2. Check snapshot was actually captured (viewport visible)
3. Verify snapshot ID matches URL parameter
4. Check snapshot cache limit (max 50 snapshots)

### Cell Counts Are Empty

**Symptom:** `cell_counts` is `{}` or warning about missing polygons.

**Solutions:**
1. Load polygons with `load_polygons` before creating annotations
2. Verify polygon file format (.pb protobuf)
3. Check polygon file contains data: `load_polygons` returns count
4. Ensure ROI overlaps with polygon data (check coordinates)

### Animation Never Completes

**Symptom:** `await_move` never returns `completed: true`.

**Solutions:**
1. Check token is valid (not expired, <60s old)
2. Ensure viewport animation is enabled (not disabled in settings)
3. Verify slide is loaded (animations fail silently without slide)
4. Check for conflicting navigation commands (only one move at a time)

### IPC Timeout

**Symptom:** Commands timeout or hang indefinitely.

**Solutions:**
1. Check Unix socket exists: `ls /tmp/pathview-*.sock`
2. Verify PathView GUI is responsive (not frozen)
3. Restart MCP server to recreate IPC connection
4. Check system logs for IPC errors

---

## Additional Resources

- **PathView Repository:** https://github.com/yourusername/pathview
- **MCP Specification:** https://modelcontextprotocol.io/
- **OpenSlide Formats:** https://openslide.org/
- **Protocol Buffers:** https://protobuf.dev/

For bugs or feature requests, please file an issue on GitHub.
