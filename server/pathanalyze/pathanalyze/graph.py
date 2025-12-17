"""
LangGraph definition for PathAnalyze agent.

Implements a linear workflow for pathology slide analysis:
connect → acquire_lock → baseline_view → survey → roi_plan → draw_roi → summarize → release
"""

import asyncio
from datetime import datetime
from typing import Any

from langgraph.graph import StateGraph, END
from langgraph.checkpoint.memory import MemorySaver
from pydantic import BaseModel, Field

from pathanalyze.mcp.tools import MCPTools
from pathanalyze.mcp.types import ViewportInfo, SlideInfo
from pathanalyze.mcp.exceptions import MCPException, MCPNotImplementedError, MCPToolError
from pathanalyze.utils.logging import logger


class AnalysisState(BaseModel):
    """State for PathAnalyze LangGraph workflow."""

    # Request context
    run_id: str = Field(..., description="Unique run identifier from FastAPI")
    slide_path: str = Field(..., description="Path to slide file")
    task: str = Field(..., description="Analysis task description")
    roi_hint: dict[str, Any] | None = Field(None, description="Optional ROI hint from user")
    mcp_base_url: str = Field(..., description="MCP server base URL")

    # Lock management
    lock_token: str | None = Field(None, description="Navigation lock token")
    lock_acquired: bool = Field(False, description="Whether lock is currently held")

    # Slide & viewport state
    slide_info: SlideInfo | None = Field(None, description="Loaded slide metadata")
    current_viewport: ViewportInfo | None = Field(None, description="Current viewport state")
    baseline_snapshot_url: str | None = Field(None, description="Initial view snapshot URL")

    # Action card streaming (PathView UI)
    action_card_id: str | None = Field(None, description="PathView action card ID for streaming")

    # ROI management
    planned_roi_vertices: list[tuple[float, float]] = Field(
        default_factory=list,
        description="Planned ROI vertices for next annotation"
    )
    roi_ids: list[int] = Field(default_factory=list, description="Created ROI annotation IDs")
    roi_metrics: list[dict[str, Any]] = Field(
        default_factory=list,
        description="Metrics computed for each ROI"
    )

    # Execution tracking
    status: str = Field("pending", description="Overall run status")
    current_step: str | None = Field(None, description="Current node being executed")
    steps_log: list[dict[str, Any]] = Field(
        default_factory=list,
        description="Log of completed steps with timestamps and results"
    )
    error_message: str | None = Field(None, description="Error details if failed")

    # Results
    summary: str | None = Field(None, description="Final analysis summary")

    class Config:
        arbitrary_types_allowed = True


# ============================================================================
# Node Implementations
# ============================================================================

async def _safe_action_card_log(
    state: AnalysisState,
    mcp: MCPTools,
    message: str,
    level: str = "info",
) -> None:
    if not state.action_card_id:
        return
    try:
        await mcp.append_action_card_log(
            card_id=state.action_card_id,
            message=message,
            level=level,
        )
    except Exception as e:
        logger.debug(
            "Action card log append failed",
            extra={"run_id": state.run_id, "error": str(e)},
        )


async def _safe_action_card_update(
    state: AnalysisState,
    mcp: MCPTools,
    *,
    status: str | None = None,
    summary: str | None = None,
    reasoning: str | None = None,
) -> None:
    if not state.action_card_id:
        return
    try:
        await mcp.update_action_card(
            card_id=state.action_card_id,
            status=status,
            summary=summary,
            reasoning=reasoning,
        )
    except Exception as e:
        logger.debug(
            "Action card update failed",
            extra={"run_id": state.run_id, "error": str(e)},
        )


def _format_cell_counts(cell_counts: dict[str, Any] | None) -> str:
    if not cell_counts:
        return ""
    items: list[tuple[str, Any]] = list(cell_counts.items())
    parts = [f"{k}={v}" for k, v in items[:5]]
    if len(items) > 5:
        parts.append("...")
    return ", ".join(parts)


async def connect_node(state: AnalysisState, mcp: MCPTools) -> AnalysisState:
    """
    Connect to MCP server and load slide.

    This node initializes the analysis session by loading the slide file
    and retrieving initial viewport information.
    """
    try:
        await _safe_action_card_log(state, mcp, "Connecting to PathView and loading slide…")
        logger.info("Connecting and loading slide", extra={"run_id": state.run_id})

        # Load slide
        slide_info = await mcp.load_slide(state.slide_path)

        # Get initial viewport state
        viewport = slide_info.viewport
        if viewport is None:
            # Fallback: query slide info for viewport
            full_info = await mcp.get_slide_info()
            viewport = full_info.viewport

        # Log step
        state.steps_log.append({
            "step": "connect",
            "timestamp": datetime.utcnow().isoformat(),
            "result": {
                "slide_loaded": True,
                "dimensions": f"{slide_info.width}x{slide_info.height}",
                "levels": slide_info.levels
            }
        })

        logger.info(
            "Slide loaded successfully",
            extra={
                "run_id": state.run_id,
                "width": slide_info.width,
                "height": slide_info.height
            }
        )

        await _safe_action_card_update(
            state,
            mcp,
            summary=f"Slide loaded: {slide_info.width}×{slide_info.height}",
        )
        await _safe_action_card_log(
            state,
            mcp,
            f"Slide loaded ({slide_info.width}×{slide_info.height}, {slide_info.levels} levels).",
            level="success",
        )

        return state.model_copy(update={
            "slide_info": slide_info,
            "current_viewport": viewport,
            "current_step": "connect",
            "status": "running"
        })

    except MCPException as e:
        await _safe_action_card_log(state, mcp, f"Connect failed: {e.message}", level="error")
        await _safe_action_card_update(state, mcp, status="failed")
        logger.error(
            f"Connect failed: {e.message}",
            extra={"run_id": state.run_id, "retryable": e.retryable}
        )
        return state.model_copy(update={
            "status": "failed",
            "error_message": f"Connect failed: {e.message}",
            "current_step": "connect"
        })

    except Exception as e:
        await _safe_action_card_log(state, mcp, f"Unexpected connect error: {e}", level="error")
        await _safe_action_card_update(state, mcp, status="failed")
        logger.error(
            f"Unexpected error in connect: {e}",
            extra={"run_id": state.run_id}
        )
        return state.model_copy(update={
            "status": "failed",
            "error_message": f"Unexpected error: {str(e)}",
            "current_step": "connect"
        })


async def acquire_lock_node(state: AnalysisState, mcp: MCPTools) -> AnalysisState:
    """
    Acquire navigation lock (MOCKED - not implemented in MCP server).

    Attempts to lock navigation for exclusive control. Falls back to
    mock token if the lock tool is not implemented.
    """
    try:
        if state.status == "failed":
            await _safe_action_card_log(
                state, mcp, "Skipping navigation lock (run already failed).", level="warning"
            )
            return state.model_copy(update={"current_step": "acquire_lock"})

        await _safe_action_card_log(state, mcp, "Acquiring navigation lock…")
        logger.info("Acquiring navigation lock", extra={"run_id": state.run_id})

        try:
            # Attempt real lock
            token = await mcp.lock_navigation(owner=state.run_id, ttl_seconds=300)
            logger.info(
                "Navigation lock acquired",
                extra={"run_id": state.run_id, "token": token}
            )
            is_mocked = False

        except MCPNotImplementedError:
            # Generate mock token
            token = f"mock-lock-{state.run_id}"
            logger.warning(
                "Lock not implemented, using mock token",
                extra={"run_id": state.run_id, "token": token}
            )
            is_mocked = True

        state.steps_log.append({
            "step": "acquire_lock",
            "timestamp": datetime.utcnow().isoformat(),
            "result": {"token": token, "mocked": is_mocked}
        })

        await _safe_action_card_log(
            state,
            mcp,
            "Navigation lock acquired." if not is_mocked else "Navigation lock mocked (tool not available).",
            level="success" if not is_mocked else "warning",
        )

        return state.model_copy(update={
            "lock_token": token,
            "lock_acquired": True,
            "current_step": "acquire_lock"
        })

    except MCPException as e:
        await _safe_action_card_log(state, mcp, f"Lock acquisition failed: {e.message}", level="error")
        await _safe_action_card_update(state, mcp, status="failed")
        logger.error(
            f"Lock acquisition failed: {e.message}",
            extra={"run_id": state.run_id}
        )
        return state.model_copy(update={
            "status": "failed",
            "error_message": f"Lock failed: {e.message}",
            "current_step": "acquire_lock"
        })

    except Exception as e:
        await _safe_action_card_log(state, mcp, f"Unexpected lock error: {e}", level="error")
        await _safe_action_card_update(state, mcp, status="failed")
        logger.error(
            f"Unexpected error in acquire_lock: {e}",
            extra={"run_id": state.run_id}
        )
        return state.model_copy(update={
            "status": "failed",
            "error_message": f"Unexpected error: {str(e)}",
            "current_step": "acquire_lock"
        })


async def baseline_view_node(state: AnalysisState, mcp: MCPTools) -> AnalysisState:
    """
    Reset view to baseline and capture snapshot.

    Resets the viewport to fit the entire slide and captures a
    baseline snapshot for reference.
    """
    try:
        if state.status == "failed":
            await _safe_action_card_log(
                state, mcp, "Skipping baseline view (run already failed).", level="warning"
            )
            return state.model_copy(update={"current_step": "baseline_view"})

        await _safe_action_card_log(state, mcp, "Resetting to baseline view…")
        logger.info("Resetting to baseline view", extra={"run_id": state.run_id})

        # Reset view to fit entire slide
        viewport = await mcp.reset_view()

        # Wait for animation to settle
        await asyncio.sleep(0.5)

        # Capture snapshot (may fail if not implemented)
        snapshot_url = None
        try:
            snapshot_result = await mcp.capture_snapshot()
            snapshot_url = snapshot_result.get("url")
            logger.info(
                "Baseline snapshot captured",
                extra={"run_id": state.run_id, "url": snapshot_url}
            )
        except (MCPToolError, MCPNotImplementedError) as e:
            logger.warning(
                "Snapshot capture failed",
                extra={"run_id": state.run_id, "error": str(e)}
            )

        if snapshot_url:
            await _safe_action_card_log(
                state, mcp, f"Baseline snapshot: {snapshot_url}", level="info"
            )
        else:
            await _safe_action_card_log(
                state, mcp, "Baseline snapshot not available.", level="warning"
            )

        state.steps_log.append({
            "step": "baseline_view",
            "timestamp": datetime.utcnow().isoformat(),
            "result": {
                "viewport": viewport.model_dump() if viewport else None,
                "snapshot": snapshot_url or "not_captured"
            }
        })

        return state.model_copy(update={
            "current_viewport": viewport,
            "baseline_snapshot_url": snapshot_url,
            "current_step": "baseline_view"
        })

    except MCPException as e:
        await _safe_action_card_log(state, mcp, f"Baseline view failed: {e.message}", level="error")
        await _safe_action_card_update(state, mcp, status="failed")
        logger.error(
            f"Baseline view failed: {e.message}",
            extra={"run_id": state.run_id}
        )
        return state.model_copy(update={
            "status": "failed",
            "error_message": f"Baseline view failed: {e.message}",
            "current_step": "baseline_view"
        })

    except Exception as e:
        await _safe_action_card_log(state, mcp, f"Unexpected baseline error: {e}", level="error")
        await _safe_action_card_update(state, mcp, status="failed")
        logger.error(
            f"Unexpected error in baseline_view: {e}",
            extra={"run_id": state.run_id}
        )
        return state.model_copy(update={
            "status": "failed",
            "error_message": f"Unexpected error: {str(e)}",
            "current_step": "baseline_view"
        })


async def survey_node(state: AnalysisState, mcp: MCPTools) -> AnalysisState:
    """
    Perform quick slide survey (MVP: navigate to center at higher zoom).

    Navigates to the center of the slide with smooth animation to
    provide a closer view of the tissue.
    """
    try:
        if state.status == "failed":
            await _safe_action_card_log(
                state, mcp, "Skipping survey (run already failed).", level="warning"
            )
            return state.model_copy(update={"current_step": "survey"})

        await _safe_action_card_log(state, mcp, "Surveying slide (move to center, 2× zoom)…")
        logger.info("Surveying slide", extra={"run_id": state.run_id})

        if not state.slide_info:
            raise ValueError("Slide info not available")

        # Calculate center of slide
        center_x = state.slide_info.width / 2
        center_y = state.slide_info.height / 2

        # Move to center with smooth animation
        move_token = await mcp.move_camera(
            center_x=center_x,
            center_y=center_y,
            zoom=2.0,  # 2x zoom
            duration_ms=500
        )

        # Wait for animation to complete
        try:
            await mcp.await_move(move_token, timeout_ms=5000)
            logger.info("Survey navigation complete", extra={"run_id": state.run_id})
        except TimeoutError:
            logger.warning("Survey navigation timeout", extra={"run_id": state.run_id})

        # Get updated viewport
        slide_info = await mcp.get_slide_info()
        viewport = slide_info.viewport

        state.steps_log.append({
            "step": "survey",
            "timestamp": datetime.utcnow().isoformat(),
            "result": {
                "center": [center_x, center_y],
                "zoom": 2.0,
                "move_token": move_token
            }
        })

        await _safe_action_card_log(
            state,
            mcp,
            f"Survey complete (center=({center_x:.0f}, {center_y:.0f}), zoom=2.0).",
            level="success",
        )

        return state.model_copy(update={
            "current_viewport": viewport,
            "current_step": "survey"
        })

    except MCPException as e:
        await _safe_action_card_log(state, mcp, f"Survey failed: {e.message}", level="error")
        await _safe_action_card_update(state, mcp, status="failed")
        logger.error(
            f"Survey failed: {e.message}",
            extra={"run_id": state.run_id}
        )
        return state.model_copy(update={
            "status": "failed",
            "error_message": f"Survey failed: {e.message}",
            "current_step": "survey"
        })

    except Exception as e:
        await _safe_action_card_log(state, mcp, f"Unexpected survey error: {e}", level="error")
        await _safe_action_card_update(state, mcp, status="failed")
        logger.error(
            f"Unexpected error in survey: {e}",
            extra={"run_id": state.run_id}
        )
        return state.model_copy(update={
            "status": "failed",
            "error_message": f"Unexpected error: {str(e)}",
            "current_step": "survey"
        })


async def roi_plan_node(state: AnalysisState, mcp: MCPTools) -> AnalysisState:
    """
    Plan ROI location based on hint or heuristic.

    Determines where to place the ROI annotation based on user hints
    or a default heuristic (center of slide).
    """
    try:
        if state.status == "failed":
            await _safe_action_card_log(
                state, mcp, "Skipping ROI planning (run already failed).", level="warning"
            )
            return state.model_copy(update={"current_step": "roi_plan"})

        await _safe_action_card_log(state, mcp, "Planning ROI…")
        logger.info("Planning ROI location", extra={"run_id": state.run_id})

        if not state.slide_info:
            raise ValueError("Slide info not available")

        # Use roi_hint if provided
        if state.roi_hint and "center" in state.roi_hint:
            center_x = state.roi_hint["center"][0]
            center_y = state.roi_hint["center"][1]
            size = state.roi_hint.get("size", 1000)
            logger.info(
                "Using ROI hint",
                extra={"run_id": state.run_id, "center": [center_x, center_y]}
            )
        else:
            # Default: center of slide
            center_x = state.slide_info.width / 2
            center_y = state.slide_info.height / 2
            size = 1000  # 1000x1000 pixel ROI
            logger.info(
                "Using default ROI (slide center)",
                extra={"run_id": state.run_id, "center": [center_x, center_y]}
            )

        # Generate rectangle vertices
        half = size / 2
        vertices = [
            (center_x - half, center_y - half),  # Top-left
            (center_x + half, center_y - half),  # Top-right
            (center_x + half, center_y + half),  # Bottom-right
            (center_x - half, center_y + half),  # Bottom-left
        ]

        state.steps_log.append({
            "step": "roi_plan",
            "timestamp": datetime.utcnow().isoformat(),
            "result": {
                "center": [center_x, center_y],
                "size": size,
                "vertices_count": len(vertices)
            }
        })

        await _safe_action_card_log(
            state,
            mcp,
            f"ROI planned (center=({center_x:.0f}, {center_y:.0f}), size={size}).",
            level="info",
        )

        return state.model_copy(update={
            "planned_roi_vertices": vertices,
            "current_step": "roi_plan"
        })

    except Exception as e:
        await _safe_action_card_log(state, mcp, f"ROI planning failed: {e}", level="error")
        await _safe_action_card_update(state, mcp, status="failed")
        logger.error(
            f"Unexpected error in roi_plan: {e}",
            extra={"run_id": state.run_id}
        )
        return state.model_copy(update={
            "status": "failed",
            "error_message": f"ROI planning failed: {str(e)}",
            "current_step": "roi_plan"
        })


async def draw_roi_node(state: AnalysisState, mcp: MCPTools) -> AnalysisState:
    """
    Create ROI annotation and compute metrics.

    Creates a polygon annotation on the slide and computes cell counting
    metrics for the region of interest.
    """
    try:
        if state.status == "failed":
            await _safe_action_card_log(
                state, mcp, "Skipping ROI drawing (run already failed).", level="warning"
            )
            return state.model_copy(update={"current_step": "draw_roi"})

        await _safe_action_card_log(state, mcp, "Creating ROI annotation and computing metrics…")
        logger.info("Drawing ROI and computing metrics", extra={"run_id": state.run_id})

        if not state.planned_roi_vertices:
            raise ValueError("No planned ROI vertices available")

        vertices = state.planned_roi_vertices

        # Create ROI annotation
        roi_result = await mcp.create_roi(
            vertices=vertices,
            name=f"ROI-{state.run_id[:8]}"
        )
        roi_id = roi_result["id"]

        logger.info("ROI created", extra={"run_id": state.run_id, "roi_id": roi_id})

        # Compute metrics for this ROI
        metrics = await mcp.roi_metrics(vertices)

        logger.info(
            "ROI metrics computed",
            extra={
                "run_id": state.run_id,
                "roi_id": roi_id,
                "cell_counts": metrics.get("cell_counts", {})
            }
        )

        state.steps_log.append({
            "step": "draw_roi",
            "timestamp": datetime.utcnow().isoformat(),
            "result": {
                "roi_id": roi_id,
                "area": metrics.get("area"),
                "cell_counts": metrics.get("cell_counts", {})
            }
        })

        cell_counts_text = _format_cell_counts(metrics.get("cell_counts"))
        msg = f"ROI {roi_id} created."
        if cell_counts_text:
            msg += f" Cell counts: {cell_counts_text}"
        await _safe_action_card_log(state, mcp, msg, level="success")

        return state.model_copy(update={
            "roi_ids": state.roi_ids + [roi_id],
            "roi_metrics": state.roi_metrics + [metrics],
            "current_step": "draw_roi"
        })

    except MCPException as e:
        await _safe_action_card_log(state, mcp, f"ROI creation failed: {e.message}", level="error")
        await _safe_action_card_update(state, mcp, status="failed")
        logger.error(
            f"ROI creation failed: {e.message}",
            extra={"run_id": state.run_id}
        )
        return state.model_copy(update={
            "status": "failed",
            "error_message": f"ROI creation failed: {e.message}",
            "current_step": "draw_roi"
        })

    except Exception as e:
        await _safe_action_card_log(state, mcp, f"Unexpected ROI error: {e}", level="error")
        await _safe_action_card_update(state, mcp, status="failed")
        logger.error(
            f"Unexpected error in draw_roi: {e}",
            extra={"run_id": state.run_id}
        )
        return state.model_copy(update={
            "status": "failed",
            "error_message": f"Unexpected error: {str(e)}",
            "current_step": "draw_roi"
        })


async def summarize_node(state: AnalysisState, mcp: MCPTools) -> AnalysisState:
    """
    Generate analysis summary.

    Creates a human-readable summary of the analysis results including
    slide information, ROI details, and cell counting metrics.
    """
    try:
        if state.status == "failed":
            await _safe_action_card_log(
                state, mcp, "Skipping summary (run already failed).", level="warning"
            )
            return state.model_copy(update={"current_step": "summarize"})

        await _safe_action_card_log(state, mcp, "Generating summary…")
        logger.info("Generating summary", extra={"run_id": state.run_id})

        # Build summary from collected data
        summary_parts = [
            f"Analysis complete for slide: {state.slide_path}",
            f"Task: {state.task}",
            "",
        ]

        if state.slide_info:
            summary_parts.append(
                f"Slide dimensions: {state.slide_info.width}x{state.slide_info.height} "
                f"({state.slide_info.levels} pyramid levels)"
            )

        summary_parts.append(f"ROIs created: {len(state.roi_ids)}")

        # Add metrics for each ROI
        for i, metrics in enumerate(state.roi_metrics):
            roi_id = state.roi_ids[i]
            area = metrics.get("area", 0)
            cell_counts = metrics.get("cell_counts", {})

            summary_parts.append(f"\nROI {roi_id}:")
            summary_parts.append(f"  Area: {area:.2f} sq pixels")

            if cell_counts:
                summary_parts.append("  Cell counts:")
                for cell_type, count in cell_counts.items():
                    summary_parts.append(f"    {cell_type}: {count}")
                total_cells = sum(cell_counts.values())
                summary_parts.append(f"  Total cells: {total_cells}")

        summary = "\n".join(summary_parts)

        state.steps_log.append({
            "step": "summarize",
            "timestamp": datetime.utcnow().isoformat(),
            "result": {"summary_length": len(summary)}
        })

        logger.info("Analysis summarized", extra={"run_id": state.run_id})

        # Keep the action card summary short; attach full details to reasoning.
        short_summary = f"ROIs: {len(state.roi_ids)}"
        await _safe_action_card_update(
            state,
            mcp,
            summary=short_summary,
            reasoning=summary,
        )
        await _safe_action_card_log(state, mcp, "Summary generated.", level="success")

        return state.model_copy(update={
            "summary": summary,
            "current_step": "summarize",
            "status": "completed"
        })

    except Exception as e:
        await _safe_action_card_log(state, mcp, f"Summarization failed: {e}", level="error")
        await _safe_action_card_update(state, mcp, status="failed")
        logger.error(
            f"Unexpected error in summarize: {e}",
            extra={"run_id": state.run_id}
        )
        return state.model_copy(update={
            "status": "failed",
            "error_message": f"Summarization failed: {str(e)}",
            "current_step": "summarize"
        })


async def release_node(state: AnalysisState, mcp: MCPTools) -> AnalysisState:
    """
    Release navigation lock and cleanup resources.

    Releases the navigation lock acquired earlier, ensuring the viewer
    is available for other agents or user interaction.
    """
    try:
        await _safe_action_card_log(state, mcp, "Releasing navigation lock…")
        logger.info("Releasing navigation lock", extra={"run_id": state.run_id})

        # Only unlock if we actually acquired a lock
        if state.lock_acquired and state.lock_token:
            try:
                await mcp.unlock_navigation(state.lock_token)
                logger.info(
                    "Navigation lock released",
                    extra={"run_id": state.run_id}
                )
            except MCPNotImplementedError:
                logger.info(
                    "Unlock not implemented (mock lock)",
                    extra={"run_id": state.run_id}
                )
            except Exception as e:
                logger.error(
                    "Failed to release lock",
                    extra={"run_id": state.run_id, "error": str(e)}
                )

        state.steps_log.append({
            "step": "release",
            "timestamp": datetime.utcnow().isoformat(),
            "result": {"lock_released": state.lock_acquired}
        })

        # If we're here due to error, preserve error status
        final_status = state.status if state.status == "failed" else "completed"

        await _safe_action_card_update(state, mcp, status=final_status)
        if final_status == "failed":
            await _safe_action_card_log(
                state,
                mcp,
                state.error_message or "Run failed.",
                level="error",
            )
        else:
            await _safe_action_card_log(state, mcp, "Run completed.", level="success")

        return state.model_copy(update={
            "lock_acquired": False,
            "current_step": "release",
            "status": final_status
        })

    except Exception as e:
        logger.error(
            f"Unexpected error in release: {e}",
            extra={"run_id": state.run_id}
        )
        # Even if release fails, mark lock as released to avoid hanging
        return state.model_copy(update={
            "lock_acquired": False,
            "current_step": "release",
            "error_message": f"Release failed: {str(e)}"
        })


# ============================================================================
# Graph Assembly
# ============================================================================


def create_analysis_graph(mcp: MCPTools) -> StateGraph:
    """
    Create the PathAnalyze LangGraph workflow.

    Args:
        mcp: MCPTools instance for calling MCP server tools

    Returns:
        Compiled StateGraph ready for execution

    The graph executes a linear workflow:
    START → connect → acquire_lock → baseline_view → survey → roi_plan
          → draw_roi → summarize → release → END
    """
    # Bind mcp to nodes via closures (avoids serialization issues)
    async def connect_bound(state: AnalysisState) -> AnalysisState:
        return await connect_node(state, mcp)

    async def acquire_lock_bound(state: AnalysisState) -> AnalysisState:
        return await acquire_lock_node(state, mcp)

    async def baseline_view_bound(state: AnalysisState) -> AnalysisState:
        return await baseline_view_node(state, mcp)

    async def survey_bound(state: AnalysisState) -> AnalysisState:
        return await survey_node(state, mcp)

    async def roi_plan_bound(state: AnalysisState) -> AnalysisState:
        return await roi_plan_node(state, mcp)

    async def draw_roi_bound(state: AnalysisState) -> AnalysisState:
        return await draw_roi_node(state, mcp)

    async def summarize_bound(state: AnalysisState) -> AnalysisState:
        return await summarize_node(state, mcp)

    async def release_bound(state: AnalysisState) -> AnalysisState:
        return await release_node(state, mcp)

    # Create graph
    workflow = StateGraph(AnalysisState)

    # Add nodes
    workflow.add_node("connect", connect_bound)
    workflow.add_node("acquire_lock", acquire_lock_bound)
    workflow.add_node("baseline_view", baseline_view_bound)
    workflow.add_node("survey", survey_bound)
    workflow.add_node("roi_plan", roi_plan_bound)
    workflow.add_node("draw_roi", draw_roi_bound)
    workflow.add_node("summarize", summarize_bound)
    workflow.add_node("release", release_bound)

    # Define linear flow
    workflow.set_entry_point("connect")
    workflow.add_edge("connect", "acquire_lock")
    workflow.add_edge("acquire_lock", "baseline_view")
    workflow.add_edge("baseline_view", "survey")
    workflow.add_edge("survey", "roi_plan")
    workflow.add_edge("roi_plan", "draw_roi")
    workflow.add_edge("draw_roi", "summarize")
    workflow.add_edge("summarize", "release")
    workflow.add_edge("release", END)

    # Compile with memory checkpointer for state persistence
    memory = MemorySaver()
    return workflow.compile(checkpointer=memory)


async def run_graph_with_cleanup(
    graph: StateGraph,
    initial_state: AnalysisState,
    mcp: MCPTools
) -> AnalysisState:
    """
    Run graph with guaranteed cleanup.

    Ensures that navigation lock is always released, even if the graph
    execution fails partway through.

    Args:
        graph: Compiled StateGraph
        initial_state: Initial analysis state
        mcp: MCPTools instance for cleanup operations

    Returns:
        Final analysis state after execution
    """
    final_state = initial_state

    try:
        # Execute graph
        config = {"configurable": {"thread_id": initial_state.run_id}}
        result = await graph.ainvoke(initial_state, config=config)
        final_state = result

    except Exception as e:
        logger.error(
            "Graph execution failed",
            extra={"run_id": initial_state.run_id, "error": str(e)}
        )
        final_state = initial_state.model_copy(update={
            "status": "failed",
            "error_message": f"Graph execution failed: {str(e)}"
        })

    finally:
        # ALWAYS try to release lock
        if final_state.lock_acquired and final_state.lock_token:
            try:
                await mcp.unlock_navigation(final_state.lock_token)
                final_state = final_state.model_copy(update={"lock_acquired": False})
                logger.info(
                    "Lock released in cleanup",
                    extra={"run_id": final_state.run_id}
                )
            except Exception as e:
                logger.error(
                    "Cleanup failed",
                    extra={"run_id": final_state.run_id, "error": str(e)}
                )

        # Best-effort finalize action card if the graph failed before release_node ran.
        if final_state.status == "failed" and final_state.action_card_id:
            try:
                await mcp.update_action_card(
                    card_id=final_state.action_card_id,
                    status="failed",
                    summary="Run failed",
                    reasoning=final_state.error_message,
                )
                await mcp.append_action_card_log(
                    card_id=final_state.action_card_id,
                    message=final_state.error_message or "Run failed.",
                    level="error",
                )
            except Exception as e:
                logger.debug(
                    "Final action card update failed",
                    extra={"run_id": final_state.run_id, "error": str(e)},
                )

    return final_state


__all__ = ["AnalysisState", "create_analysis_graph", "run_graph_with_cleanup"]
