"""
High-level tool wrappers for MCP client.

This module provides typed, domain-specific methods for calling MCP tools.
"""

from typing import Any
from pathanalyze.mcp.client import MCPClient
from pathanalyze.mcp.types import SlideInfo, PolygonInfo, ViewportInfo
from pathanalyze.mcp.exceptions import MCPNotImplementedError


class MCPTools:
    """
    High-level tool wrapper for MCPClient.

    Provides typed methods for all PathView MCP tools, including placeholders
    for future tools.
    """

    def __init__(self, client: MCPClient):
        self.client = client

    # ========================================================================
    # EXISTING TOOLS (implemented in PathView MCP server)
    # ========================================================================

    async def load_slide(self, path: str) -> SlideInfo:
        """
        Load a whole-slide image file.

        Args:
            path: Absolute path to slide file (.svs, .tiff, etc.)

        Returns:
            Slide metadata
        """
        result = await self.client.call_tool("load_slide", {"path": path})
        return SlideInfo(**result)

    async def get_slide_info(self) -> SlideInfo:
        """
        Get information about the currently loaded slide.

        Returns:
            Slide metadata including viewport state
        """
        result = await self.client.call_tool("get_slide_info")
        return SlideInfo(**result)

    async def pan(self, dx: float, dy: float) -> ViewportInfo:
        """
        Pan the viewport by delta in slide coordinates.

        Args:
            dx: X delta in pixels
            dy: Y delta in pixels

        Returns:
            Updated viewport state
        """
        result = await self.client.call_tool("pan", {"dx": dx, "dy": dy})
        return ViewportInfo(**result)

    async def center_on(self, x: float, y: float) -> ViewportInfo:
        """
        Center viewport on a specific point in slide coordinates.

        Args:
            x: X coordinate in slide space
            y: Y coordinate in slide space

        Returns:
            Updated viewport state
        """
        result = await self.client.call_tool("center_on", {"x": x, "y": y})
        return ViewportInfo(**result)

    async def zoom(self, delta: float) -> ViewportInfo:
        """
        Zoom in or out.

        Args:
            delta: Zoom factor (> 1.0 zooms in, < 1.0 zooms out)
                  Example: 1.1 = 10% in, 0.9 = 10% out

        Returns:
            Updated viewport state
        """
        result = await self.client.call_tool("zoom", {"delta": delta})
        return ViewportInfo(**result)

    async def zoom_at_point(
        self,
        screen_x: float,
        screen_y: float,
        delta: float
    ) -> ViewportInfo:
        """
        Zoom at a specific screen point.

        Args:
            screen_x: Screen X coordinate
            screen_y: Screen Y coordinate
            delta: Zoom factor

        Returns:
            Updated viewport state
        """
        result = await self.client.call_tool("zoom_at_point", {
            "screen_x": screen_x,
            "screen_y": screen_y,
            "delta": delta,
        })
        return ViewportInfo(**result)

    async def reset_view(self) -> ViewportInfo:
        """
        Reset viewport to fit entire slide in window.

        Returns:
            Updated viewport state
        """
        result = await self.client.call_tool("reset_view")
        return ViewportInfo(**result)

    async def load_polygons(self, path: str) -> PolygonInfo:
        """
        Load polygon overlay from protobuf file.

        Args:
            path: Absolute path to .pb or .protobuf file

        Returns:
            Polygon loading statistics
        """
        result = await self.client.call_tool("load_polygons", {"path": path})
        return PolygonInfo(**result)

    async def query_polygons(
        self,
        x: float,
        y: float,
        w: float,
        h: float
    ) -> dict[str, Any]:
        """
        Query polygons in a rectangular region.

        Args:
            x: Region X coordinate (slide space)
            y: Region Y coordinate (slide space)
            w: Region width
            h: Region height

        Returns:
            Query result with polygons list
        """
        result = await self.client.call_tool("query_polygons", {
            "x": x,
            "y": y,
            "w": w,
            "h": h,
        })
        return result

    async def set_polygon_visibility(self, visible: bool) -> dict[str, Any]:
        """
        Show or hide polygon overlay.

        Args:
            visible: True to show, False to hide

        Returns:
            Visibility status
        """
        result = await self.client.call_tool("set_polygon_visibility", {
            "visible": visible
        })
        return result

    async def capture_snapshot(
        self,
        width: int | None = None,
        height: int | None = None
    ) -> dict[str, Any]:
        """
        Capture current viewport as PNG image.

        NOTE: Currently NOT IMPLEMENTED in PathView MCP server.
        Will raise MCPToolError with "not yet implemented" message.

        Args:
            width: Image width (optional)
            height: Image height (optional)

        Returns:
            Snapshot metadata (URL, dimensions, etc.)

        Raises:
            MCPToolError: Not yet implemented
        """
        params = {}
        if width is not None:
            params["width"] = width
        if height is not None:
            params["height"] = height

        result = await self.client.call_tool("capture_snapshot", params)
        return result

    # ========================================================================
    # PLACEHOLDER TOOLS (to be implemented in PathView MCP server)
    # ========================================================================

    async def agent_hello(
        self,
        agent_id: str,
        capabilities: list[str]
    ) -> dict[str, Any]:
        """
        Register agent with PathView server.

        TODO: Implement in PathView MCP server as 'agent.hello' tool

        Args:
            agent_id: Unique agent identifier
            capabilities: List of agent capabilities

        Returns:
            Server acknowledgement with session info

        Raises:
            MCPNotImplementedError: Not yet implemented
        """
        raise MCPNotImplementedError("agent.hello")

    async def lock_navigation(
        self,
        owner: str,
        ttl_seconds: int = 300
    ) -> str:
        """
        Lock navigation for exclusive agent control.

        TODO: Implement in PathView MCP server as 'nav.lock' tool

        Args:
            owner: Lock owner identifier
            ttl_seconds: Lock timeout in seconds

        Returns:
            Lock token (UUID)

        Raises:
            MCPNotImplementedError: Not yet implemented
        """
        raise MCPNotImplementedError("nav.lock")

    async def unlock_navigation(self, token: str) -> dict[str, Any]:
        """
        Release navigation lock.

        TODO: Implement in PathView MCP server as 'nav.unlock' tool

        Args:
            token: Lock token from lock_navigation()

        Returns:
            Unlock confirmation

        Raises:
            MCPNotImplementedError: Not yet implemented
        """
        raise MCPNotImplementedError("nav.unlock")

    async def move_camera(
        self,
        center_x: float,
        center_y: float,
        zoom: float,
        duration_ms: int = 300
    ) -> str:
        """
        Move camera to target position with smooth animation.

        Args:
            center_x: Target X coordinate (center of viewport)
            center_y: Target Y coordinate (center of viewport)
            zoom: Target zoom level
            duration_ms: Animation duration in milliseconds (default 300)

        Returns:
            Move token (UUID) for tracking completion
        """
        result = await self.client.call_tool("move_camera", {
            "center_x": center_x,
            "center_y": center_y,
            "zoom": zoom,
            "duration_ms": duration_ms
        })
        return result["token"]

    async def await_move(self, token: str, timeout_ms: int = 5000) -> dict[str, Any]:
        """
        Wait for camera move to complete by polling.

        Args:
            token: Move token from move_camera()
            timeout_ms: Maximum wait time in milliseconds (default 5000)

        Returns:
            Final viewport state: {completed, aborted, position, zoom}

        Raises:
            TimeoutError: If animation doesn't complete within timeout
        """
        import asyncio
        import time

        start = time.time()
        poll_interval = 0.05  # 50ms between polls

        while (time.time() - start) * 1000 < timeout_ms:
            result = await self.client.call_tool("await_move", {"token": token})

            if result["completed"]:
                return result

            await asyncio.sleep(poll_interval)

        # Timeout - get final state anyway
        result = await self.client.call_tool("await_move", {"token": token})
        if not result["completed"]:
            raise TimeoutError(f"Animation did not complete within {timeout_ms}ms")
        return result

    async def create_roi(
        self,
        vertices: list[tuple[float, float]],
        name: str | None = None
    ) -> dict[str, Any]:
        """
        Create a region of interest annotation.

        Args:
            vertices: Polygon vertices [(x1, y1), (x2, y2), ...] in slide coordinates
            name: Optional custom name for the ROI

        Returns:
            Dictionary with keys:
            - id (int): Unique annotation ID
            - name (str): Annotation name
            - vertex_count (int): Number of vertices
            - bounding_box (dict): {x, y, width, height}
            - area (float): Area in square pixels
            - cell_counts (dict): Per-class counts if polygons loaded

        Raises:
            MCPToolError: If vertices invalid or no slide loaded
        """
        params = {"vertices": vertices}
        if name is not None:
            params["name"] = name

        return await self.client.call_tool("create_annotation", params)

    async def list_rois(self, include_metrics: bool = False) -> list[dict[str, Any]]:
        """
        List all annotations/ROIs.

        Args:
            include_metrics: Include full cell count metrics

        Returns:
            List of annotation dictionaries
        """
        result = await self.client.call_tool("list_annotations", {
            "include_metrics": include_metrics
        })
        return result["annotations"]

    async def get_roi(self, roi_id: int) -> dict[str, Any]:
        """
        Get detailed information about a specific ROI.

        Args:
            roi_id: Annotation ID

        Returns:
            Dictionary with full annotation details including vertices
        """
        return await self.client.call_tool("get_annotation", {"id": roi_id})

    async def delete_roi(self, roi_id: int) -> bool:
        """
        Delete an annotation by ID.

        Args:
            roi_id: Annotation ID

        Returns:
            True if successfully deleted
        """
        result = await self.client.call_tool("delete_annotation", {"id": roi_id})
        return result.get("success", False)

    async def roi_metrics(self, vertices: list[tuple[float, float]]) -> dict[str, Any]:
        """
        Compute metrics for arbitrary polygon vertices WITHOUT creating annotation.
        Useful for quick probes to test different ROI boundaries.

        Args:
            vertices: Polygon vertices [(x1, y1), (x2, y2), ...] in slide coordinates

        Returns:
            Dictionary with keys:
            - bounding_box (dict): {x, y, width, height}
            - area (float): Area in square pixels
            - perimeter (float): Perimeter in pixels
            - cell_counts (dict): Per-class counts (if polygons loaded)

        Raises:
            MCPToolError: If vertices invalid or no slide loaded
        """
        return await self.client.call_tool("compute_roi_metrics", {"vertices": vertices})

    async def action_card_create(
        self,
        title: str,
        status: str = "pending"
    ) -> str:
        """
        Create a new action card for streaming updates.

        TODO: Implement in PathView MCP server as 'action_card.create' tool

        Args:
            title: Card title
            status: Initial status

        Returns:
            Card ID (UUID)

        Raises:
            MCPNotImplementedError: Not yet implemented
        """
        raise MCPNotImplementedError("action_card.create")

    async def action_card_append(
        self,
        card_id: str,
        text: str,
        type: str = "info"
    ) -> dict[str, Any]:
        """
        Append content to an action card.

        TODO: Implement in PathView MCP server as 'action_card.append' tool

        Args:
            card_id: Card identifier
            text: Content to append
            type: Content type (info, warning, error)

        Returns:
            Update confirmation

        Raises:
            MCPNotImplementedError: Not yet implemented
        """
        raise MCPNotImplementedError("action_card.append")

    async def action_card_update(
        self,
        card_id: str,
        status: str | None = None,
        title: str | None = None
    ) -> dict[str, Any]:
        """
        Update action card status or title.

        TODO: Implement in PathView MCP server as 'action_card.update' tool

        Args:
            card_id: Card identifier
            status: New status (optional)
            title: New title (optional)

        Returns:
            Update confirmation

        Raises:
            MCPNotImplementedError: Not yet implemented
        """
        raise MCPNotImplementedError("action_card.update")
