"""Happy path integration test for AI Cursor workflow (Step 7)."""

import asyncio
import pytest
import httpx


@pytest.mark.integration
@pytest.mark.asyncio
async def test_ai_cursor_complete_workflow(mcp_client, slide_path):
    """Test complete AI Cursor workflow from handshake to unlock.

    This test validates the essential workflow documented in the AI Agent Guide:
    1. Handshake
    2. Load resources
    3. Acquire lock
    4. Create action card
    5. Navigate camera
    6. Capture snapshot
    7. Create annotation
    8. Complete and unlock
    """
    owner_uuid = "test-agent-step7-happy-path"

    # Step 1: Handshake
    hello_result = await mcp_client.call_tool("agent_hello", {
        "agent_name": "step7-test-agent",
        "agent_version": "1.0.0"
    })
    assert "session_id" in hello_result
    assert "stream_url" in hello_result
    assert hello_result["navigation_locked"] is False

    # Step 2: Load resources
    slide_result = await mcp_client.call_tool("load_slide", {"path": slide_path})
    assert "width" in slide_result
    assert "height" in slide_result
    assert slide_result["width"] > 0
    assert slide_result["height"] > 0

    # Step 3: Acquire navigation lock
    lock_result = await mcp_client.call_tool("nav_lock", {
        "owner_uuid": owner_uuid,
        "ttl_seconds": 60
    })
    assert lock_result["success"] is True
    assert lock_result["lock_owner"] == owner_uuid

    try:
        # Step 4: Create action card
        card = await mcp_client.call_tool("create_action_card", {
            "title": "Step 7 Happy Path Test",
            "summary": "Testing complete AI cursor workflow",
            "owner_uuid": owner_uuid
        })
        card_id = card["id"]
        assert "id" in card
        assert card["status"] == "pending"

        # Update to in_progress
        await mcp_client.call_tool("update_action_card", {
            "id": card_id,
            "status": "in_progress"
        })

        # Step 5: Capture initial snapshot
        snapshot1 = await mcp_client.call_tool("capture_snapshot", {})
        assert "id" in snapshot1
        assert "url" in snapshot1

        # Verify snapshot is accessible via HTTP
        async with httpx.AsyncClient(timeout=5.0) as http_client:
            response = await http_client.get(snapshot1["url"])
            assert response.status_code == 200
            assert response.headers["content-type"] == "image/png"
            assert len(response.content) > 1000  # Reasonable PNG size

        # Log progress
        await mcp_client.call_tool("append_action_card_log", {
            "id": card_id,
            "message": "Initial snapshot captured successfully"
        })

        # Step 6: Navigate with move_camera
        slide_info = await mcp_client.call_tool("get_slide_info", {})
        center_x = slide_info["width"] / 2
        center_y = slide_info["height"] / 2

        move_result = await mcp_client.call_tool("move_camera", {
            "center_x": center_x,
            "center_y": center_y,
            "zoom": 2.0,
            "duration_ms": 300
        })
        token = move_result["token"]
        assert "token" in move_result

        # Step 7: Await camera movement completion
        max_wait = 2.0  # 2 seconds max
        start_time = asyncio.get_event_loop().time()
        completed = False

        while True:
            await asyncio.sleep(0.05)
            status = await mcp_client.call_tool("await_move", {"token": token})

            if status["completed"]:
                assert status["aborted"] is False
                assert "position" in status
                assert "zoom" in status
                completed = True
                break

            if asyncio.get_event_loop().time() - start_time > max_wait:
                pytest.fail("Camera move timed out after 2 seconds")

        assert completed, "Camera animation should complete"

        await mcp_client.call_tool("append_action_card_log", {
            "id": card_id,
            "message": "Camera positioned at center"
        })

        # Step 8: Capture snapshot after move
        snapshot2 = await mcp_client.call_tool("capture_snapshot", {})
        assert snapshot2["id"] != snapshot1["id"]

        # Step 9: Create ROI annotation
        roi_vertices = [
            [center_x - 1000, center_y - 1000],
            [center_x + 1000, center_y - 1000],
            [center_x + 1000, center_y + 1000],
            [center_x - 1000, center_y + 1000]
        ]

        annotation = await mcp_client.call_tool("create_annotation", {
            "vertices": roi_vertices,
            "name": "Step 7 Test ROI"
        })
        assert "id" in annotation
        assert "cell_counts" in annotation
        assert "bounding_box" in annotation
        assert "area" in annotation

        # Log metrics (cell counts may be empty if no polygons loaded)
        cell_counts = annotation["cell_counts"]
        total_cells = cell_counts.get("total", 0)

        await mcp_client.call_tool("append_action_card_log", {
            "id": card_id,
            "message": f"ROI created with {total_cells} cells detected",
            "level": "success"
        })

        # Step 10: Complete action card
        await mcp_client.call_tool("update_action_card", {
            "id": card_id,
            "status": "completed",
            "summary": f"Workflow complete. Analyzed ROI with {total_cells} cells."
        })

        # Cleanup action card
        delete_result = await mcp_client.call_tool("delete_action_card", {"id": card_id})
        assert delete_result["success"] is True

    finally:
        # Step 11: Unlock (always execute in finally block)
        unlock_result = await mcp_client.call_tool("nav_unlock", {
            "owner_uuid": owner_uuid
        })
        assert unlock_result["success"] is True

    # Verify lock is released
    lock_status = await mcp_client.call_tool("nav_lock_status", {})
    assert lock_status["locked"] is False
