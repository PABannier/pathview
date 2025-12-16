"""Integration tests for action card functionality."""

import pytest
from pathanalyze.mcp.client import MCPClient
from pathanalyze.mcp.exceptions import MCPToolError


@pytest.mark.integration
class TestActionCards:
    """Test action card creation and management via MCP."""

    @pytest.mark.asyncio
    async def test_create_action_card(self, mcp_client):
        """Test creating an action card."""
        # Create card
        result = await mcp_client.call_tool(
            "create_action_card",
            {
                "title": "Test Analysis Task",
                "summary": "Analyzing test slide region",
                "owner_uuid": "test-agent-123"
            }
        )

        assert "id" in result
        assert result["title"] == "Test Analysis Task"
        assert result["status"] == "pending"
        assert "created_at" in result

        # Store card ID for cleanup
        card_id = result["id"]

        # List cards
        list_result = await mcp_client.call_tool("list_action_cards", {})
        assert list_result["count"] >= 1

        # Find our card
        cards = list_result["cards"]
        our_card = next((c for c in cards if c["id"] == card_id), None)
        assert our_card is not None
        assert our_card["title"] == "Test Analysis Task"

        # Cleanup
        await mcp_client.call_tool("delete_action_card", {"id": card_id})

    @pytest.mark.asyncio
    async def test_update_action_card_status(self, mcp_client):
        """Test updating action card status."""
        # Create card
        create_result = await mcp_client.call_tool(
            "create_action_card",
            {"title": "Status Test Card"}
        )
        card_id = create_result["id"]

        # Update to in_progress
        update_result = await mcp_client.call_tool(
            "update_action_card",
            {"id": card_id, "status": "in_progress"}
        )
        assert update_result["status"] == "in_progress"

        # Update to completed
        update_result = await mcp_client.call_tool(
            "update_action_card",
            {"id": card_id, "status": "completed"}
        )
        assert update_result["status"] == "completed"

        # Cleanup
        await mcp_client.call_tool("delete_action_card", {"id": card_id})

    @pytest.mark.asyncio
    async def test_append_log_entries(self, mcp_client):
        """Test appending log entries to action card."""
        # Create card
        create_result = await mcp_client.call_tool(
            "create_action_card",
            {"title": "Log Test Card"}
        )
        card_id = create_result["id"]

        # Append info log
        log_result = await mcp_client.call_tool(
            "append_action_card_log",
            {
                "id": card_id,
                "message": "Starting analysis",
                "level": "info"
            }
        )
        assert log_result["log_count"] == 1

        # Append warning
        log_result = await mcp_client.call_tool(
            "append_action_card_log",
            {
                "id": card_id,
                "message": "Low quality region detected",
                "level": "warning"
            }
        )
        assert log_result["log_count"] == 2

        # Append success
        log_result = await mcp_client.call_tool(
            "append_action_card_log",
            {
                "id": card_id,
                "message": "Analysis complete",
                "level": "success"
            }
        )
        assert log_result["log_count"] == 3

        # Cleanup
        await mcp_client.call_tool("delete_action_card", {"id": card_id})

    @pytest.mark.asyncio
    async def test_action_card_with_reasoning(self, mcp_client):
        """Test action card with reasoning field."""
        # Create card with reasoning
        result = await mcp_client.call_tool(
            "create_action_card",
            {
                "title": "Complex Analysis",
                "summary": "Multi-step region analysis",
                "reasoning": "This task requires three steps: "
                            "1) Load polygons, 2) Identify high-density "
                            "regions, 3) Compute detailed metrics"
            }
        )
        card_id = result["id"]

        # Update reasoning
        await mcp_client.call_tool(
            "update_action_card",
            {
                "id": card_id,
                "reasoning": "Updated approach: Added step 4 for validation"
            }
        )

        # Cleanup
        await mcp_client.call_tool("delete_action_card", {"id": card_id})

    @pytest.mark.asyncio
    async def test_delete_action_card(self, mcp_client):
        """Test deleting action cards."""
        # Create card
        create_result = await mcp_client.call_tool(
            "create_action_card",
            {"title": "Delete Test Card"}
        )
        card_id = create_result["id"]

        # Delete card
        delete_result = await mcp_client.call_tool(
            "delete_action_card",
            {"id": card_id}
        )
        assert delete_result["success"] is True
        assert delete_result["deleted_id"] == card_id

        # Verify card is gone
        list_result = await mcp_client.call_tool("list_action_cards", {})
        cards = list_result["cards"]
        deleted_card = next((c for c in cards if c["id"] == card_id), None)
        assert deleted_card is None

    @pytest.mark.asyncio
    async def test_invalid_card_id(self, mcp_client):
        """Test error handling for invalid card ID."""
        with pytest.raises(Exception) as exc_info:
            await mcp_client.call_tool(
                "update_action_card",
                {"id": "nonexistent-id-12345", "status": "completed"}
            )

        error_msg = str(exc_info.value).lower()
        assert "not found" in error_msg or "error" in error_msg

    @pytest.mark.asyncio
    async def test_invalid_status_value(self, mcp_client):
        """Test error handling for invalid status."""
        # Create card
        create_result = await mcp_client.call_tool(
            "create_action_card",
            {"title": "Invalid Status Test"}
        )
        card_id = create_result["id"]

        try:
            with pytest.raises(Exception):
                await mcp_client.call_tool(
                    "update_action_card",
                    {"id": card_id, "status": "invalid_status"}
                )
        finally:
            # Cleanup
            await mcp_client.call_tool("delete_action_card", {"id": card_id})

    @pytest.mark.asyncio
    async def test_action_card_workflow(self, mcp_client):
        """Test complete action card workflow."""
        # 1. Create card
        card = await mcp_client.call_tool(
            "create_action_card",
            {
                "title": "Workflow Test",
                "summary": "Complete workflow test",
                "owner_uuid": "test-workflow-agent"
            }
        )
        card_id = card["id"]

        try:
            # 2. Start work
            await mcp_client.call_tool(
                "update_action_card",
                {"id": card_id, "status": "in_progress"}
            )
            await mcp_client.call_tool(
                "append_action_card_log",
                {"id": card_id, "message": "Starting task"}
            )

            # 3. Progress updates
            await mcp_client.call_tool(
                "append_action_card_log",
                {"id": card_id, "message": "Step 1 complete"}
            )
            await mcp_client.call_tool(
                "append_action_card_log",
                {"id": card_id, "message": "Step 2 complete"}
            )

            # 4. Complete
            await mcp_client.call_tool(
                "update_action_card",
                {"id": card_id, "status": "completed"}
            )
            await mcp_client.call_tool(
                "append_action_card_log",
                {"id": card_id, "message": "Task completed successfully", "level": "success"}
            )

            # 5. Verify final state
            list_result = await mcp_client.call_tool("list_action_cards", {})
            final_card = next((c for c in list_result["cards"] if c["id"] == card_id), None)
            assert final_card is not None
            assert final_card["status"] == "completed"
            assert final_card["log_entry_count"] == 4

        finally:
            # Cleanup
            await mcp_client.call_tool("delete_action_card", {"id": card_id})
