"""Unit tests for Step 4: action card streaming."""

import pytest

from pathanalyze.graph import AnalysisState, connect_node, release_node
from pathanalyze.mcp.tools import MCPTools


pytestmark = pytest.mark.unit


def _tool_calls(mock_server, tool_name: str) -> list[dict]:
    calls: list[dict] = []
    for req in mock_server.requests:
        if req.get("method") != "tools/call":
            continue
        params = req.get("params") or {}
        if params.get("name") == tool_name:
            calls.append(req)
    return calls


@pytest.mark.asyncio
async def test_connect_node_appends_action_card_logs(mock_client, mock_server):
    tools = MCPTools(mock_client)

    mock_server.add_tool_response(
        "load_slide",
        {
            "path": "/test/slide.svs",
            "width": 10000,
            "height": 8000,
            "levels": 3,
            "vendor": "aperio",
            "viewport": {"position": {"x": 0, "y": 0}, "zoom": 1.0},
        },
    )
    mock_server.add_tool_response("append_action_card_log", {"log_count": 1})
    mock_server.add_tool_response("update_action_card", {"status": "in_progress"})

    state = AnalysisState(
        run_id="test-run-1",
        slide_path="/test/slide.svs",
        task="Test task",
        mcp_base_url="http://127.0.0.1:9999",
        action_card_id="card-123",
    )

    await connect_node(state, tools)

    assert _tool_calls(mock_server, "append_action_card_log")
    assert _tool_calls(mock_server, "update_action_card")


@pytest.mark.asyncio
async def test_release_node_updates_action_card_status(mock_client, mock_server):
    tools = MCPTools(mock_client)

    mock_server.add_tool_response("append_action_card_log", {"log_count": 1})
    mock_server.add_tool_response("update_action_card", {"status": "failed"})

    state = AnalysisState(
        run_id="test-run-2",
        slide_path="/test/slide.svs",
        task="Test task",
        mcp_base_url="http://127.0.0.1:9999",
        action_card_id="card-456",
        status="failed",
        error_message="boom",
    )

    result = await release_node(state, tools)

    assert result.status == "failed"
    update_calls = _tool_calls(mock_server, "update_action_card")
    assert update_calls
    assert any((call.get("params") or {}).get("arguments", {}).get("status") == "failed" for call in update_calls)

