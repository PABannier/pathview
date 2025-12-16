"""Tests for clear error messages on missing resources (Step 7)."""

import pytest


@pytest.mark.integration
@pytest.mark.asyncio
async def test_clear_error_no_slide_navigation(mcp_client):
    """Test clear error when navigating without loaded slide.

    Validates that error messages mention 'load_slide' tool.
    """
    # Try to navigate without loading slide
    with pytest.raises(Exception) as exc_info:
        await mcp_client.call_tool("pan", {"dx": 100, "dy": 100})

    error = str(exc_info.value)
    assert "slide" in error.lower()
    assert "load_slide" in error.lower()


@pytest.mark.integration
@pytest.mark.asyncio
async def test_clear_error_no_slide_annotation(mcp_client):
    """Test clear error when creating annotation without slide.

    Validates that annotation commands also check for loaded slide.
    """
    with pytest.raises(Exception) as exc_info:
        await mcp_client.call_tool("create_annotation", {
            "vertices": [[0, 0], [100, 0], [100, 100], [0, 100]]
        })

    error = str(exc_info.value)
    assert "slide" in error.lower()
    assert "load_slide" in error.lower()


@pytest.mark.integration
@pytest.mark.asyncio
async def test_clear_error_no_polygons(mcp_client):
    """Test clear error when polygon commands used without polygons.

    Validates that polygon commands mention 'load_polygons' tool.
    """
    # Try to set polygon visibility without loading polygons
    with pytest.raises(Exception) as exc_info:
        await mcp_client.call_tool("set_polygon_visibility", {"visible": true})

    error = str(exc_info.value)
    assert "polygon" in error.lower()
    assert "load_polygons" in error.lower()


@pytest.mark.integration
@pytest.mark.asyncio
async def test_annotation_without_polygons_warning(mcp_client, slide_path):
    """Test warning when creating annotation without polygons.

    Validates that annotation creation succeeds but returns a warning
    when polygon data is not loaded.
    """
    await mcp_client.call_tool("load_slide", {"path": slide_path})

    # Create annotation without loading polygons
    result = await mcp_client.call_tool("create_annotation", {
        "vertices": [[1000, 1000], [2000, 1000], [2000, 2000], [1000, 2000]],
        "name": "Test ROI without polygons"
    })

    # Should succeed but with empty cell counts or warning
    assert "id" in result
    assert "cell_counts" in result

    # Check for warning or empty cell counts
    has_warning = "warning" in result
    has_empty_counts = result.get("cell_counts", {}).get("total", 0) == 0

    assert has_warning or has_empty_counts, \
        "Should have warning or empty cell counts when polygons not loaded"

    # If warning exists, verify it mentions load_polygons
    if has_warning:
        assert "polygon" in result["warning"].lower()
        assert "load_polygons" in result["warning"].lower()


@pytest.mark.integration
@pytest.mark.asyncio
async def test_compute_metrics_without_polygons_warning(mcp_client, slide_path):
    """Test warning when computing metrics without polygons.

    Validates that compute_roi_metrics also warns about missing polygons.
    """
    await mcp_client.call_tool("load_slide", {"path": slide_path})

    # Compute metrics without loading polygons
    result = await mcp_client.call_tool("compute_roi_metrics", {
        "vertices": [[1000, 1000], [2000, 1000], [2000, 2000], [1000, 2000]]
    })

    # Should succeed but with empty cell counts or warning
    assert "cell_counts" in result

    # Check for warning or empty cell counts
    has_warning = "warning" in result
    has_empty_counts = result.get("cell_counts", {}).get("total", 0) == 0

    assert has_warning or has_empty_counts, \
        "Should have warning or empty cell counts when polygons not loaded"

    # If warning exists, verify it mentions load_polygons
    if has_warning:
        assert "polygon" in result["warning"].lower()
        assert "load_polygons" in result["warning"].lower()


@pytest.mark.integration
@pytest.mark.asyncio
async def test_all_viewport_commands_have_clear_errors(mcp_client):
    """Test that all viewport commands have helpful error messages.

    Validates that each viewport command returns a clear error
    mentioning 'load_slide' when no slide is loaded.
    """
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

        error = str(exc_info.value).lower()
        assert "slide" in error, \
            f"{command_name} error should mention 'slide'"
        assert "load_slide" in error, \
            f"{command_name} error should mention 'load_slide' tool"
