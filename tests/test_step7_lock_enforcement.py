"""Tests for navigation lock enforcement guardrails (Step 7)."""

import asyncio
import pytest


@pytest.mark.integration
@pytest.mark.asyncio
async def test_navigation_requires_lock(mcp_client, slide_path):
    """Test that navigation tools fail when lock not held.

    Validates that viewport commands (pan, zoom, etc.) are properly
    guarded by navigation lock enforcement.
    """
    # Load slide first
    await mcp_client.call_tool("load_slide", {"path": slide_path})

    # Try to pan without lock - should fail
    with pytest.raises(Exception) as exc_info:
        await mcp_client.call_tool("pan", {"dx": 100, "dy": 100})

    error_msg = str(exc_info.value).lower()
    assert "lock" in error_msg or "navigation" in error_msg

    # Try zoom without lock - should also fail
    with pytest.raises(Exception) as exc_info:
        await mcp_client.call_tool("zoom", {"delta": 1.5})

    error_msg = str(exc_info.value).lower()
    assert "lock" in error_msg

    # Try move_camera without lock - should also fail
    with pytest.raises(Exception) as exc_info:
        await mcp_client.call_tool("move_camera", {
            "center_x": 1000,
            "center_y": 1000,
            "zoom": 2.0
        })

    error_msg = str(exc_info.value).lower()
    assert "lock" in error_msg


@pytest.mark.integration
@pytest.mark.asyncio
async def test_lock_holder_can_navigate(mcp_client, slide_path):
    """Test that lock holder CAN navigate successfully.

    Validates that after acquiring lock, all navigation commands work.
    """
    await mcp_client.call_tool("load_slide", {"path": slide_path})

    owner_uuid = "test-agent-lock-holder"

    # Acquire lock
    lock_result = await mcp_client.call_tool("nav_lock", {
        "owner_uuid": owner_uuid,
        "ttl_seconds": 60
    })
    assert lock_result["success"] is True

    try:
        # Should succeed - pan
        pan_result = await mcp_client.call_tool("pan", {"dx": 100, "dy": 100})
        assert "position" in pan_result

        # Should succeed - zoom
        zoom_result = await mcp_client.call_tool("zoom", {"delta": 1.5})
        assert "zoom" in zoom_result

        # Should succeed - center_on
        center_result = await mcp_client.call_tool("center_on", {"x": 5000, "y": 5000})
        assert "position" in center_result

        # Should succeed - move_camera
        move_result = await mcp_client.call_tool("move_camera", {
            "center_x": 10000,
            "center_y": 10000,
            "zoom": 1.5,
            "duration_ms": 200
        })
        assert "token" in move_result

        # Should succeed - reset_view
        reset_result = await mcp_client.call_tool("reset_view", {})
        assert "position" in reset_result

    finally:
        await mcp_client.call_tool("nav_unlock", {"owner_uuid": owner_uuid})


@pytest.mark.integration
@pytest.mark.asyncio
async def test_lock_auto_expires(mcp_client, slide_path):
    """Test that lock auto-expires after TTL.

    Validates that locks are automatically released after the
    configured TTL, allowing other agents to acquire control.
    """
    await mcp_client.call_tool("load_slide", {"path": slide_path})

    owner_uuid = "test-agent-short-ttl"

    # Acquire lock with 1 second TTL
    lock_result = await mcp_client.call_tool("nav_lock", {
        "owner_uuid": owner_uuid,
        "ttl_seconds": 1
    })
    assert lock_result["success"] is True

    # Verify lock is active
    status = await mcp_client.call_tool("nav_lock_status", {})
    assert status["locked"] is True
    assert status["owner"] == owner_uuid

    # Wait for expiry
    await asyncio.sleep(1.5)

    # Check lock status - should be unlocked
    status = await mcp_client.call_tool("nav_lock_status", {})
    assert status["locked"] is False

    # Should be able to navigate now (lock expired)
    # Note: This assumes no lock is held, so navigation should be allowed
    pan_result = await mcp_client.call_tool("pan", {"dx": 50, "dy": 50})
    assert "position" in pan_result


@pytest.mark.integration
@pytest.mark.asyncio
async def test_lock_prevents_second_acquisition(mcp_client, slide_path):
    """Test that a second agent cannot acquire lock while held.

    Validates that lock exclusivity is enforced.
    """
    await mcp_client.call_tool("load_slide", {"path": slide_path})

    owner1 = "test-agent-first"
    owner2 = "test-agent-second"

    # Agent 1 acquires lock
    lock1 = await mcp_client.call_tool("nav_lock", {
        "owner_uuid": owner1,
        "ttl_seconds": 60
    })
    assert lock1["success"] is True

    try:
        # Agent 2 tries to acquire lock - should fail
        lock2 = await mcp_client.call_tool("nav_lock", {
            "owner_uuid": owner2,
            "ttl_seconds": 60
        })
        assert lock2["success"] is False
        assert "error" in lock2

    finally:
        # Cleanup
        await mcp_client.call_tool("nav_unlock", {"owner_uuid": owner1})


@pytest.mark.integration
@pytest.mark.asyncio
async def test_all_viewport_commands_require_lock(mcp_client, slide_path):
    """Test that ALL viewport commands enforce lock requirement.

    Validates that pan, zoom, zoom_at_point, center_on, reset_view,
    and move_camera all require navigation lock.
    """
    await mcp_client.call_tool("load_slide", {"path": slide_path})

    # Test each viewport command without lock
    viewport_commands = [
        ("pan", {"dx": 100, "dy": 100}),
        ("zoom", {"delta": 1.5}),
        ("zoom_at_point", {"screen_x": 960, "screen_y": 540, "delta": 1.5}),
        ("center_on", {"x": 5000, "y": 5000}),
        ("reset_view", {}),
        ("move_camera", {"center_x": 1000, "center_y": 1000, "zoom": 2.0})
    ]

    for command_name, params in viewport_commands:
        with pytest.raises(Exception) as exc_info:
            await mcp_client.call_tool(command_name, params)

        error_msg = str(exc_info.value).lower()
        assert "lock" in error_msg or "navigation" in error_msg, \
            f"{command_name} should require navigation lock"
