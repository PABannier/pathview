"""FastAPI application entrypoint."""

from contextlib import asynccontextmanager
from pathlib import Path
from fastapi import FastAPI, HTTPException, BackgroundTasks
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field
import uuid

from pathanalyze.config import settings
from pathanalyze.mcp.client import MCPClient
from pathanalyze.mcp.exceptions import MCPException
from pathanalyze.utils.logging import setup_logging, logger


# Request/Response models
class AnalysisRequest(BaseModel):
    """Request to start analysis."""
    slide_path: str = Field(..., description="Path to slide file")
    task: str = Field(..., description="Analysis task description")
    roi_hint: dict | None = Field(None, description="Optional ROI hint")


class AnalysisResponse(BaseModel):
    """Response with run ID."""
    run_id: str = Field(..., description="Unique run identifier")
    status: str = Field(..., description="Initial status")


class RunStatus(BaseModel):
    """Status of an analysis run."""
    run_id: str
    status: str  # pending, running, completed, failed
    message: str | None = None


# In-memory run tracking (replace with DB in production)
runs: dict[str, RunStatus] = {}

# Detailed state storage for completed runs
runs_detailed: dict[str, "AnalysisState"] = {}  # type: ignore


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Lifecycle management."""
    setup_logging(settings.log_level, settings.log_format)
    logger.info("PathAnalyze starting", extra={"version": "0.1.0"})

    # Test MCP connection on startup
    try:
        async with MCPClient(str(settings.mcp_base_url)) as client:
            await client.initialize()
            logger.info("MCP connection verified")
    except Exception as e:
        logger.error("Failed to connect to MCP server", extra={"error": str(e)})
        # Continue anyway - connections will be per-request

    yield

    logger.info("PathAnalyze shutting down")


app = FastAPI(
    title="PathAnalyze",
    description="LangGraph-based pathology analysis agent",
    version="0.1.0",
    lifespan=lifespan,
)


@app.get("/health")
async def health_check():
    """Health check endpoint."""
    return {"status": "ok", "version": "0.1.0"}


@app.post("/analyze", response_model=AnalysisResponse)
async def start_analysis(
    request: AnalysisRequest,
    background_tasks: BackgroundTasks
) -> AnalysisResponse:
    """
    Start a new analysis task.

    This will spawn a LangGraph run in the background.
    """
    run_id = str(uuid.uuid4())

    # Create initial status
    runs[run_id] = RunStatus(
        run_id=run_id,
        status="pending",
        message="Analysis queued"
    )

    # Queue background task (placeholder for LangGraph execution)
    background_tasks.add_task(run_analysis, run_id, request)

    logger.info("Analysis started", extra={"run_id": run_id, "slide": request.slide_path})

    return AnalysisResponse(
        run_id=run_id,
        status="pending"
    )


@app.get("/runs/{run_id}", response_model=RunStatus)
async def get_run_status(run_id: str) -> RunStatus:
    """Get status of an analysis run."""
    if run_id not in runs:
        raise HTTPException(status_code=404, detail="Run not found")
    return runs[run_id]


@app.get("/runs/{run_id}/details")
async def get_run_details(run_id: str) -> dict:
    """
    Get detailed state of an analysis run.

    Returns the complete AnalysisState including steps_log, ROI metrics,
    and full execution details.
    """
    if run_id not in runs_detailed:
        raise HTTPException(
            status_code=404,
            detail="Run details not found. Details are only available after run completes."
        )
    return runs_detailed[run_id].model_dump()


async def run_analysis(run_id: str, request: AnalysisRequest):
    """
    Execute LangGraph analysis workflow.

    Creates MCP client connection, builds initial state, executes the
    LangGraph workflow, and stores detailed results.
    """
    from pathanalyze.graph import AnalysisState, create_analysis_graph, run_graph_with_cleanup
    from pathanalyze.mcp.tools import MCPTools

    mcp_client = None
    action_card_id: str | None = None

    try:
        runs[run_id].status = "running"
        runs[run_id].message = "Connecting to MCP server"

        # Connect to MCP
        mcp_client = MCPClient(str(settings.mcp_base_url))
        await mcp_client.connect()
        await mcp_client.initialize()
        mcp_tools = MCPTools(mcp_client)

        # Create a PathView action card for streaming progress updates (best-effort).
        try:
            slide_name = Path(request.slide_path).name
            card = await mcp_tools.create_action_card(
                title=f"PathAnalyze: {request.task}",
                summary=f"Slide: {slide_name}",
                reasoning=f"Run ID: {run_id}\nTask: {request.task}",
                owner_uuid=run_id,
            )
            action_card_id = str(card.get("id", ""))
            if action_card_id:
                await mcp_tools.update_action_card(action_card_id, status="in_progress")
                await mcp_tools.append_action_card_log(
                    action_card_id,
                    message="Analysis started",
                    level="info",
                )
        except Exception as e:
            logger.info(
                "Action card streaming not available",
                extra={"run_id": run_id, "error": str(e)},
            )

        runs[run_id].message = "Connected, starting analysis workflow"

        # Create initial state
        initial_state = AnalysisState(
            run_id=run_id,
            slide_path=request.slide_path,
            task=request.task,
            roi_hint=request.roi_hint,
            mcp_base_url=str(settings.mcp_base_url),
            action_card_id=action_card_id,
            status="running"
        )

        # Create and run graph
        graph = create_analysis_graph(mcp_tools)
        final_state = await run_graph_with_cleanup(graph, initial_state, mcp_tools)

        # Update run status
        runs[run_id].status = final_state.status
        runs[run_id].message = final_state.summary or final_state.error_message or "Complete"

        # Best-effort action card finalization.
        if action_card_id:
            try:
                final_status = "failed" if final_state.status == "failed" else "completed"
                await mcp_tools.update_action_card(
                    action_card_id,
                    status=final_status,
                    summary=runs[run_id].message[:200] if runs[run_id].message else None,
                    reasoning=final_state.summary or final_state.error_message,
                )
                await mcp_tools.append_action_card_log(
                    action_card_id,
                    message=runs[run_id].message or "Complete",
                    level="error" if final_state.status == "failed" else "success",
                )
            except Exception as e:
                logger.debug(
                    "Action card finalization failed",
                    extra={"run_id": run_id, "error": str(e)},
                )

        # Store detailed state
        runs_detailed[run_id] = final_state

        logger.info(
            "Analysis workflow completed",
            extra={
                "run_id": run_id,
                "status": final_state.status,
                "steps": len(final_state.steps_log)
            }
        )

    except Exception as e:
        logger.error("Analysis failed", extra={"run_id": run_id, "error": str(e)})
        runs[run_id].status = "failed"
        runs[run_id].message = str(e)
        if mcp_client and action_card_id:
            try:
                mcp_tools = MCPTools(mcp_client)
                await mcp_tools.update_action_card(
                    action_card_id,
                    status="failed",
                    summary="Run failed",
                    reasoning=str(e),
                )
                await mcp_tools.append_action_card_log(
                    action_card_id,
                    message=str(e),
                    level="error",
                )
            except Exception:
                pass

    finally:
        # Always close MCP connection
        if mcp_client:
            try:
                await mcp_client.close()
            except Exception as e:
                logger.error(
                    "Failed to close MCP client",
                    extra={"run_id": run_id, "error": str(e)}
                )


@app.exception_handler(MCPException)
async def mcp_exception_handler(request, exc: MCPException):
    """Handle MCP exceptions."""
    return JSONResponse(
        status_code=500,
        content={
            "error": "mcp_error",
            "code": exc.code,
            "message": exc.message,
            "retryable": exc.retryable,
        }
    )
