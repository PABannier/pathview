# PathAnalyze

LangGraph-based pathology analysis agent that connects to PathView MCP server.

## Quick Start

### Installation

```bash
# From the repo root
cd server/pathanalyze
pip install -e ".[dev]"
```

### Configuration

Copy `.env.example` to `.env` and configure:

```bash
cd server/pathanalyze
cp .env.example .env
```

Edit `.env` with your settings:

```bash
# MCP server URL (default: http://127.0.0.1:9000)
PATHANALYZE_MCP_BASE_URL=http://127.0.0.1:9000

# API server port
PATHANALYZE_API_PORT=8000

# Test data paths (for integration tests)
TEST_SLIDE_PATH=/path/to/your/test.svs
TEST_POLYGON_PATH=/path/to/your/test.pb
```

### Running

```bash
# Start PathView MCP server first (in separate terminal, from repo root)
(cd ../.. && ./build/pathview-mcp)

# Start PathAnalyze
python -m pathanalyze

# Or with reload for development
PATHANALYZE_API_RELOAD=true python -m pathanalyze
```

## Usage

### API Endpoints

#### Health Check
```bash
curl http://127.0.0.1:8000/health
```

#### Start Analysis
```bash
curl -X POST http://127.0.0.1:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{
    "slide_path": "/path/to/slide.svs",
    "task": "Analyze tissue morphology"
  }'
```

This will also create a PathView Action Card (via MCP) and stream progress logs into it while the workflow runs.

Response:
```json
{
  "run_id": "uuid-here",
  "status": "pending"
}
```

#### Check Status
```bash
curl http://127.0.0.1:8000/runs/{run_id}
```

### Python API

```python
from pathanalyze.mcp.client import MCPClient
from pathanalyze.mcp.tools import MCPTools

async with MCPClient("http://127.0.0.1:9000") as client:
    await client.initialize()
    tools = MCPTools(client)

    # Load slide
    info = await tools.load_slide("/path/to/slide.svs")
    print(f"Loaded: {info.width}x{info.height}")

    # Navigate
    await tools.reset_view()
    await tools.zoom(2.0)
    await tools.center_on(info.width/2, info.height/2)

    # Load polygons
    poly_info = await tools.load_polygons("/path/to/cells.pb")
    print(f"Loaded {poly_info.count} polygons")
```

## Testing

### Unit Tests (no server needed)
Uses mock MCP server for fast, isolated testing:

```bash
pytest -m unit
```

### Integration Tests (requires live MCP server)
Tests with actual PathView MCP server:

```bash
# Start PathView MCP server first
(cd ../.. && ./build/pathview-mcp)

# Run integration tests
cd server/pathanalyze
pytest -m integration
```

### All Tests with Coverage
```bash
pytest --cov=pathanalyze --cov-report=html
```

## Architecture

### Core Components

- **`pathanalyze/mcp/client.py`** - Async MCP client (HTTP+SSE transport)
  - Connection management with SSE listener
  - JSON-RPC 2.0 protocol implementation
  - Request/response correlation
  - Error handling with retry support

- **`pathanalyze/mcp/tools.py`** - High-level tool wrappers
  - Typed methods for all MCP tools
  - Existing tools (load_slide, pan, zoom, etc.)
  - Placeholder methods for future tools

- **`pathanalyze/main.py`** - FastAPI application
  - `/analyze` endpoint for starting analyses
  - `/runs/{id}` endpoint for status tracking
  - Background task execution

- **`pathanalyze/graph.py`** - LangGraph definition (Step 3)
  - Workflow orchestration (to be implemented)

### Design Patterns

- **Async/Await**: Native asyncio for I/O-bound operations
- **Type Safety**: Pydantic models for all data structures
- **Error Handling**: Comprehensive exception hierarchy with retry logic
- **Configuration**: Environment variable-based settings
- **Testing**: Unit tests with mocks + integration tests with live server

## Tool Status

### Implemented (PathView MCP Server)
- ✅ **load_slide** - Load whole-slide images
- ✅ **get_slide_info** - Get slide metadata
- ✅ **pan** - Pan viewport by delta
- ✅ **center_on** - Center on specific point
- ✅ **zoom** - Zoom in/out
- ✅ **zoom_at_point** - Zoom at screen point
- ✅ **reset_view** - Fit slide to window
- ✅ **load_polygons** - Load polygon overlay
- ✅ **query_polygons** - Query polygons in region
- ✅ **set_polygon_visibility** - Show/hide polygons
- ⚠️ **capture_snapshot** - Defined but not implemented
- ✅ **create_action_card / update_action_card / append_action_card_log** - Action card progress streaming
- ✅ **list_action_cards / delete_action_card** - Action card management

### Planned (Future PathView Tools)
- 🚧 **agent_hello** - Register agent with server
- 🚧 **nav.lock / nav.unlock** - Navigation locking
- 🚧 **nav.move / nav.await_move** - Smooth camera moves
- 🚧 **annotations.create** - Create ROI annotations
- 🚧 **annotations.metrics** - Compute ROI metrics

## Development

### Code Quality

```bash
# Format code
cd server/pathanalyze
ruff format pathanalyze/ tests/

# Lint
ruff check pathanalyze/ tests/

# Type checking
mypy pathanalyze/
```

### Project Structure

```
pathanalyze/
├── __init__.py          # Package exports
├── __main__.py          # CLI entrypoint
├── config.py            # Settings management
├── main.py              # FastAPI app
├── graph.py             # LangGraph (stub)
├── mcp/                 # MCP client
│   ├── client.py        # Async client
│   ├── tools.py         # Tool wrappers
│   ├── types.py         # Pydantic models
│   └── exceptions.py    # Custom exceptions
└── utils/
    └── logging.py       # Structured logging

tests/
├── conftest.py          # Pytest fixtures
├── test_mcp_client.py   # Client tests
├── test_tools.py        # Tool tests
├── test_integration.py  # E2E tests
└── mocks/
    └── mock_mcp_server.py  # Mock server
```

### Adding New Tools

When PathView MCP server implements a new tool:

1. **Update `pathanalyze/mcp/tools.py`**:
   - Remove `raise MCPNotImplementedError(...)`
   - Implement actual tool call
   - Update docstring

2. **Add tests in `tests/test_tools.py`**:
   - Move from `TestPlaceholderTools` to `TestExistingTools`
   - Add integration test

3. **Update types if needed**:
   - Add Pydantic models to `pathanalyze/mcp/types.py`

Example:
```python
# Before (placeholder)
async def lock_navigation(self, owner: str, ttl_seconds: int = 300) -> str:
    raise MCPNotImplementedError("nav.lock")

# After (implemented)
async def lock_navigation(self, owner: str, ttl_seconds: int = 300) -> str:
    """Lock navigation for exclusive agent control."""
    result = await self.client.call_tool("nav.lock", {
        "owner": owner,
        "ttl_seconds": ttl_seconds,
    })
    return result["token"]
```

## Error Handling

### Automatic Retry with Tenacity

Connection and timeout errors are automatically retried:

```python
from tenacity import retry, stop_after_attempt, wait_exponential, retry_if_exception_type
from pathanalyze.mcp.exceptions import MCPConnectionError, MCPTimeoutError

@retry(
    retry=retry_if_exception_type((MCPConnectionError, MCPTimeoutError)),
    stop=stop_after_attempt(3),
    wait=wait_exponential(multiplier=1, min=1, max=10),
)
async def reliable_call(client, tool_name, **kwargs):
    return await client.call_tool(tool_name, kwargs)
```

### Exception Hierarchy

- `MCPException` - Base exception
  - `MCPConnectionError` - Network/connection failures (retryable)
  - `MCPTimeoutError` - Request timeouts (retryable)
  - `MCPToolError` - Tool execution errors (retryable varies)
  - `MCPNotImplementedError` - Feature not implemented (not retryable)

## Troubleshooting

### MCP Server Not Found
Ensure PathView MCP server is running:
```bash
(cd ../.. && ./build/pathview-mcp)
```

Check the server URL in `.env`:
```bash
PATHANALYZE_MCP_BASE_URL=http://127.0.0.1:9000
```

### Integration Tests Failing
1. Verify MCP server is running
2. Check test data paths in `.env`:
   ```bash
   TEST_SLIDE_PATH=/actual/path/to/slide.svs
   TEST_POLYGON_PATH=/actual/path/to/cells.pb
   ```
3. Run with verbose output:
   ```bash
   pytest -m integration -v
   ```

### Import Errors
Reinstall in editable mode:
```bash
cd server/pathanalyze
pip install -e ".[dev]"
```

## License

MIT
