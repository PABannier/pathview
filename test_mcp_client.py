#!/usr/bin/env python3
"""
PathView MCP Server Test Suite

A comprehensive test script that validates the PathView MCP server functionality
by acting as an MCP client and exercising all available tools.
"""

import sys
import json
import time
import threading
import argparse
import os
from typing import Dict, Any, Optional, List, Tuple
from urllib.parse import urljoin

try:
    import requests
except ImportError:
    print("ERROR: Missing required package 'requests'")
    print("Install with: pip install requests sseclient-py")
    sys.exit(1)

try:
    from sseclient import SSEClient
except ImportError:
    print("ERROR: Missing required package 'sseclient-py'")
    print("Install with: pip install requests sseclient-py")
    sys.exit(1)


# MCP Error Codes (JSON-RPC 2.0)
ERROR_CODES = {
    -32700: "Parse Error",
    -32600: "Invalid Request",
    -32601: "Method Not Found",
    -32602: "Invalid Params",
    -32603: "Internal Error",
    -32000: "Server Error",
}


class MCPException(Exception):
    """Exception raised when MCP operations fail"""
    def __init__(self, code: int, message: str):
        self.code = code
        self.message = message
        super().__init__(f"[{code}] {message}")


class MCPClient:
    """
    MCP Client that communicates with PathView MCP server via HTTP+SSE.
    """

    def __init__(self, server_url: str, sse_endpoint: str = '/sse', timeout: int = 30, verbose: bool = False):
        self.server_url = server_url
        self.sse_endpoint = sse_endpoint
        self.timeout = timeout
        self.verbose = verbose

        self.message_endpoint = None
        self.sse_thread = None
        self.request_id = 1
        self.pending_responses: Dict[int, Dict] = {}
        self.response_events: Dict[int, threading.Event] = {}
        self.lock = threading.Lock()
        self.running = False
        self.connected = False

    def initialize(self, client_name: str = 'PathView Test Client', client_version: str = '1.0') -> bool:
        """
        Connect to MCP server via SSE and perform handshake.

        Returns:
            True if initialization successful
        """
        if self.verbose:
            print(f"[DEBUG] Connecting to SSE endpoint: {self.server_url}{self.sse_endpoint}")

        # Start SSE listener thread
        self.running = True
        self.sse_thread = threading.Thread(target=self._sse_listener, daemon=True)
        self.sse_thread.start()

        # Wait for message endpoint from server (with timeout)
        start_time = time.time()
        while self.message_endpoint is None and time.time() - start_time < self.timeout:
            time.sleep(0.1)

        if self.message_endpoint is None:
            raise MCPException(-32000, "Failed to receive message endpoint from server")

        if self.verbose:
            print(f"[DEBUG] Received message endpoint: {self.message_endpoint}")

        # Send initialize request
        try:
            result = self._send_jsonrpc('initialize', {
                'protocolVersion': '2024-11-05',
                'capabilities': {},
                'clientInfo': {
                    'name': client_name,
                    'version': client_version
                }
            })

            if self.verbose:
                print(f"[DEBUG] Initialize response: {json.dumps(result, indent=2)}")

            # Send 'initialized' notification to complete handshake
            self._send_notification('initialized', {})

            if self.verbose:
                print(f"[DEBUG] Sent 'initialized' notification")

            self.connected = True
            return True

        except Exception as e:
            raise MCPException(-32000, f"Failed to initialize: {str(e)}")

    def call_tool(self, tool_name: str, arguments: Optional[Dict] = None) -> Any:
        """
        Call an MCP tool and return the result.

        Args:
            tool_name: Name of the tool to call
            arguments: Tool arguments (optional)

        Returns:
            Tool result
        """
        if not self.connected:
            raise MCPException(-32000, "Client not initialized")

        params = {
            'name': tool_name,
            'arguments': arguments or {}
        }

        if self.verbose:
            print(f"[DEBUG] Calling tool: {tool_name}")
            print(f"[DEBUG] Arguments: {json.dumps(arguments, indent=2)}")

        try:
            response = self._send_jsonrpc('tools/call', params)

            if self.verbose:
                print(f"[DEBUG] Tool response: {json.dumps(response, indent=2)}")

            # Extract content from tool response
            if isinstance(response, dict):
                # Check for error
                if response.get('isError', False):
                    error_msg = response.get('content', 'Unknown error')
                    raise MCPException(-32603, str(error_msg))

                # PathView MCP server format: {isError: false, content: {...}}
                if 'content' in response:
                    content = response['content']

                    # If content is a dict, return it directly
                    if isinstance(content, dict):
                        return content

                    # If content is an array (standard MCP format)
                    if isinstance(content, list) and len(content) > 0:
                        text_content = content[0].get('text', '{}')
                        return json.loads(text_content)

            # If not in expected format, return as-is
            return response

        except MCPException:
            raise
        except Exception as e:
            raise MCPException(-32603, f"Tool call failed: {str(e)}")

    def close(self):
        """Close the MCP client connection"""
        self.running = False
        if self.sse_thread and self.sse_thread.is_alive():
            # Give thread time to finish
            self.sse_thread.join(timeout=1.0)
        self.connected = False

    def _sse_listener(self):
        """Background thread to listen for SSE events"""
        try:
            sse_url = f"{self.server_url}{self.sse_endpoint}"

            if self.verbose:
                print(f"[DEBUG] SSE listener starting: {sse_url}")

            response = requests.get(sse_url, stream=True, timeout=self.timeout)
            client = SSEClient(response)

            for event in client.events():
                if not self.running:
                    break

                if self.verbose:
                    print(f"[DEBUG] SSE event: {event.event}, data: {event.data}")

                # Check for endpoint event (server sends message endpoint)
                if event.event == 'endpoint':
                    self.message_endpoint = event.data
                    continue

                # Check for message event (JSON-RPC response)
                if event.event == 'message':
                    try:
                        data = json.loads(event.data)

                        # Handle JSON-RPC response
                        if 'id' in data and data['id'] is not None:
                            req_id = data['id']
                            with self.lock:
                                self.pending_responses[req_id] = data
                                if req_id in self.response_events:
                                    self.response_events[req_id].set()

                    except json.JSONDecodeError as e:
                        if self.verbose:
                            print(f"[DEBUG] Failed to parse SSE data: {e}")

        except Exception as e:
            if self.verbose:
                print(f"[DEBUG] SSE listener error: {e}")
            if self.running:  # Only raise if we're still supposed to be running
                self.message_endpoint = None  # Signal failure

    def _send_jsonrpc(self, method: str, params: Optional[Dict] = None) -> Any:
        """
        Send a JSON-RPC 2.0 request and wait for response.

        Args:
            method: JSON-RPC method name
            params: Method parameters

        Returns:
            Response result
        """
        if self.message_endpoint is None:
            raise MCPException(-32000, "No message endpoint available")

        # Generate request ID and create event
        with self.lock:
            req_id = self.request_id
            self.request_id += 1
            self.response_events[req_id] = threading.Event()

        # Build JSON-RPC request
        request = {
            'jsonrpc': '2.0',
            'id': req_id,
            'method': method
        }

        if params is not None:
            request['params'] = params

        if self.verbose:
            print(f"[DEBUG] Sending request: {json.dumps(request, indent=2)}")

        # Send request
        try:
            endpoint_url = urljoin(self.server_url, self.message_endpoint)
            response = requests.post(
                endpoint_url,
                json=request,
                timeout=self.timeout
            )
            response.raise_for_status()

        except requests.RequestException as e:
            with self.lock:
                del self.response_events[req_id]
            raise MCPException(-32000, f"Request failed: {str(e)}")

        # Wait for response via SSE
        event = self.response_events[req_id]
        if not event.wait(timeout=self.timeout):
            with self.lock:
                del self.response_events[req_id]
            raise MCPException(-32000, "Request timeout")

        # Get response
        with self.lock:
            response_data = self.pending_responses.pop(req_id)
            del self.response_events[req_id]

        # Check for error
        if 'error' in response_data:
            error = response_data['error']
            code = error.get('code', -32000)
            message = error.get('message', 'Unknown error')
            raise MCPException(code, message)

        return response_data.get('result')

    def _send_notification(self, method: str, params: Optional[Dict] = None):
        """
        Send a JSON-RPC 2.0 notification (no response expected).

        Args:
            method: JSON-RPC method name
            params: Method parameters
        """
        if self.message_endpoint is None:
            raise MCPException(-32000, "No message endpoint available")

        # Build JSON-RPC notification (no id field)
        notification = {
            'jsonrpc': '2.0',
            'method': f'notifications/{method}'
        }

        if params is not None:
            notification['params'] = params

        if self.verbose:
            print(f"[DEBUG] Sending notification: {json.dumps(notification, indent=2)}")

        # Send notification (no response expected)
        try:
            endpoint_url = urljoin(self.server_url, self.message_endpoint)
            response = requests.post(
                endpoint_url,
                json=notification,
                timeout=self.timeout
            )
            response.raise_for_status()

        except requests.RequestException as e:
            raise MCPException(-32000, f"Notification failed: {str(e)}")


class ResultTracker:
    """
    Tracks test results and generates formatted output.
    """

    def __init__(self, json_output: bool = False):
        self.json_output = json_output
        self.tests: List[Dict] = []
        self.start_time = time.time()

    def record_test(self, name: str, status: str, message: str = '', duration: float = 0.0):
        """
        Record a test result.

        Args:
            name: Test name
            status: PASS, FAIL, or SKIP
            message: Result message
            duration: Test duration in seconds
        """
        self.tests.append({
            'name': name,
            'status': status,
            'message': message,
            'duration_seconds': duration
        })

        # Print result immediately (unless JSON mode)
        if not self.json_output:
            symbol = '✓' if status == 'PASS' else '✗' if status == 'FAIL' else '⊘'
            print(f"  {symbol} {status}: {message} ({duration:.2f}s)")

    def get_stats(self) -> Dict[str, int]:
        """Get test statistics"""
        total = len(self.tests)
        passed = sum(1 for t in self.tests if t['status'] == 'PASS')
        failed = sum(1 for t in self.tests if t['status'] == 'FAIL')
        skipped = sum(1 for t in self.tests if t['status'] == 'SKIP')

        return {
            'total': total,
            'passed': passed,
            'failed': failed,
            'skipped': skipped
        }

    def print_summary(self):
        """Print test summary"""
        duration = time.time() - self.start_time
        stats = self.get_stats()

        if self.json_output:
            # JSON output mode
            output = {
                'summary': {
                    **stats,
                    'duration_seconds': duration
                },
                'tests': self.tests
            }
            print(json.dumps(output, indent=2))
        else:
            # Human-readable output mode
            print("\n" + "=" * 40)
            print("TEST SUMMARY")
            print("=" * 40)
            print(f"Total:   {stats['total']} tests")
            print(f"Passed:  {stats['passed']} tests ({stats['passed']*100//stats['total'] if stats['total'] > 0 else 0}%)")
            print(f"Failed:  {stats['failed']} tests ({stats['failed']*100//stats['total'] if stats['total'] > 0 else 0}%)")
            if stats['skipped'] > 0:
                print(f"Skipped: {stats['skipped']} tests")
            print(f"Time:    {duration:.2f}s")

            # Show failed tests
            failed_tests = [t for t in self.tests if t['status'] == 'FAIL']
            if failed_tests:
                print("\nFailed Tests:")
                for test in failed_tests:
                    print(f"  - {test['name']}")
                    print(f"    Error: {test['message']}")

            print("=" * 40)


class TestSuite:
    """
    Comprehensive test suite for PathView MCP server.
    """

    def __init__(self, client: MCPClient, slide_path: str, polygon_path: Optional[str] = None,
                 http_port: int = 8080, verbose: bool = False):
        self.client = client
        self.slide_path = slide_path
        self.polygon_path = polygon_path
        self.http_port = http_port
        self.verbose = verbose
        self.tracker = ResultTracker(json_output=False)

        # Slide info (populated after load)
        self.slide_width = 0
        self.slide_height = 0
        self.slide_levels = 0

    def run_all_tests(self) -> ResultTracker:
        """
        Run all tests and return the result tracker.
        """
        print("\n" + "=" * 40)
        print("PathView MCP Server Test Suite")
        print("=" * 40)
        print(f"Server: {self.client.server_url}")
        print(f"Slide: {self.slide_path}")
        if self.polygon_path:
            print(f"Polygons: {self.polygon_path}")
        print("=" * 40 + "\n")

        # Run test groups
        self._test_http_endpoints()
        self._test_slide_operations()
        self._test_viewport_operations()

        if self.polygon_path:
            self._test_polygon_operations()

        self._test_snapshot_operations()

        return self.tracker

    def _run_test(self, test_num: int, total: int, name: str, test_func):
        """Helper to run a single test with timing and error handling"""
        print(f"[TEST {test_num}/{total}] {name}...")
        start = time.time()

        try:
            result = test_func()
            duration = time.time() - start
            self.tracker.record_test(name, 'PASS', result, duration)

        except MCPException as e:
            duration = time.time() - start
            error_name = ERROR_CODES.get(e.code, f"Error {e.code}")
            self.tracker.record_test(name, 'FAIL', f"{error_name}: {e.message}", duration)

        except Exception as e:
            duration = time.time() - start
            self.tracker.record_test(name, 'FAIL', str(e), duration)

    def _sample_viewport_over_time(self, operation_func, samples=4, interval_ms=100):
        """
        Execute operation and sample viewport state over time.

        Args:
            operation_func: Function that triggers viewport operation
            samples: Number of samples to take
            interval_ms: Milliseconds between samples

        Returns:
            List of (time_ms, position, zoom) tuples
        """
        results = []
        start = time.time()
        operation_func()

        for i in range(samples):
            time.sleep(interval_ms / 1000.0)
            info = self.client.call_tool('get_slide_info')
            elapsed_ms = (time.time() - start) * 1000

            results.append((
                elapsed_ms,
                (info['viewport']['position']['x'], info['viewport']['position']['y']),
                info['viewport']['zoom']
            ))

        return results

    def _test_http_endpoints(self):
        """Test HTTP endpoints"""
        def test_health():
            url = f"http://127.0.0.1:{self.http_port}/health"
            response = requests.get(url, timeout=5)
            if response.status_code != 200:
                raise Exception(f"Health check failed with status {response.status_code}")
            return f"Health check OK: {response.text}"

        self._run_test(1, 16, "HTTP health check", test_health)

    def _test_slide_operations(self):
        """Test slide loading and info"""
        def test_load_slide():
            result = self.client.call_tool('load_slide', {'path': self.slide_path})

            # Validate response
            if not isinstance(result, dict):
                raise Exception(f"Invalid response type: {type(result)}")

            for field in ['width', 'height', 'levels', 'path']:
                if field not in result:
                    raise Exception(f"Missing field: {field}")

            if result['width'] <= 0 or result['height'] <= 0 or result['levels'] <= 0:
                raise Exception(f"Invalid dimensions: {result}")

            # Store for later tests
            self.slide_width = result['width']
            self.slide_height = result['height']
            self.slide_levels = result['levels']

            return f"Loaded: {result['width']}x{result['height']}, {result['levels']} levels"

        def test_get_info():
            result = self.client.call_tool('get_slide_info')

            # Validate response
            if not isinstance(result, dict):
                raise Exception(f"Invalid response type: {type(result)}")

            # Check basic fields
            if result.get('width') != self.slide_width or result.get('height') != self.slide_height:
                raise Exception(f"Dimension mismatch: {result}")

            # Check if viewport info is included
            has_viewport = 'viewport' in result

            return f"Verified dimensions, viewport info: {has_viewport}"

        self._run_test(2, 16, "Load slide file", test_load_slide)

        if self.slide_width > 0:  # Only run if load succeeded
            self._run_test(3, 16, "Get slide info", test_get_info)

    def _test_viewport_operations(self):
        """Test viewport control operations"""
        if self.slide_width == 0:
            print("[SKIP] Viewport tests (slide not loaded)")
            return

        def test_reset():
            result = self.client.call_tool('reset_view')
            self._validate_viewport_response(result, "Reset view")
            return f"pos=({result['position']['x']:.0f}, {result['position']['y']:.0f}), zoom={result['zoom']:.3f}"

        def test_zoom_in():
            result = self.client.call_tool('zoom', {'delta': 2.0})
            self._validate_viewport_response(result, "Zoom in")
            return f"zoom={result['zoom']:.3f}, pos=({result['position']['x']:.0f}, {result['position']['y']:.0f})"

        def test_zoom_out():
            result = self.client.call_tool('zoom', {'delta': 0.5})
            self._validate_viewport_response(result, "Zoom out")
            return f"zoom={result['zoom']:.3f}, pos=({result['position']['x']:.0f}, {result['position']['y']:.0f})"

        def test_pan_right():
            result = self.client.call_tool('pan', {'dx': 1000, 'dy': 0})
            self._validate_viewport_response(result, "Pan right")
            return f"pos=({result['position']['x']:.0f}, {result['position']['y']:.0f})"

        def test_pan_down():
            result = self.client.call_tool('pan', {'dx': 0, 'dy': 1000})
            self._validate_viewport_response(result, "Pan down")
            return f"pos=({result['position']['x']:.0f}, {result['position']['y']:.0f})"

        def test_center_middle():
            x = self.slide_width / 2
            y = self.slide_height / 2
            result = self.client.call_tool('center_on', {'x': x, 'y': y})
            self._validate_viewport_response(result, "Center on middle")
            return f"Centered on ({x:.0f}, {y:.0f})"

        def test_center_quadrant1():
            x = self.slide_width * 0.25
            y = self.slide_height * 0.25
            result = self.client.call_tool('center_on', {'x': x, 'y': y})
            self._validate_viewport_response(result, "Center on top-left quadrant")
            return f"Centered on ({x:.0f}, {y:.0f})"

        def test_center_quadrant2():
            x = self.slide_width * 0.75
            y = self.slide_height * 0.75
            result = self.client.call_tool('center_on', {'x': x, 'y': y})
            self._validate_viewport_response(result, "Center on bottom-right quadrant")
            return f"Centered on ({x:.0f}, {y:.0f})"

        def test_zoom_at_point():
            result = self.client.call_tool('zoom_at_point', {
                'screen_x': 400,
                'screen_y': 300,
                'delta': 1.5
            })
            self._validate_viewport_response(result, "Zoom at point")
            return f"zoom={result['zoom']:.3f} at (400, 300)"

        def test_reset_again():
            result = self.client.call_tool('reset_view')
            self._validate_viewport_response(result, "Reset view again")
            return "Reset successful"

        def test_animation_smoothness():
            """Verify viewport animations are smooth and progressive"""
            # Reset to known state and zoom in to have room to pan
            self.client.call_tool('reset_view')
            time.sleep(0.6)  # Wait for reset animation

            # Zoom in significantly to have room to pan
            self.client.call_tool('zoom', {'delta': 5.0})
            time.sleep(0.4)  # Wait for zoom animation

            # Now pan and sample positions
            samples = self._sample_viewport_over_time(
                lambda: self.client.call_tool('pan', {'dx': 5000, 'dy': 0}),
                samples=4,
                interval_ms=75
            )

            # Verify progressive movement
            positions = [s[1][0] for s in samples]

            if len(set(positions)) < 3:
                raise Exception(f"Animation not smooth - positions: {positions}")

            # Verify monotonic increase (panning right)
            if not all(positions[i] < positions[i+1] for i in range(len(positions)-1)):
                raise Exception(f"Animation not monotonic: {positions}")

            # Verify reasonable timing
            if samples[0][0] < 50:
                raise Exception(f"Animation too fast: {samples[0][0]}ms")

            if samples[-1][0] > 500:
                raise Exception(f"Animation too slow: {samples[-1][0]}ms")

            return f"Smooth animation verified ({len(set(positions))} unique positions)"

        # Run viewport tests
        self._run_test(4, 17, "Reset view", test_reset)
        self._run_test(5, 17, "Zoom in (2x)", test_zoom_in)
        self._run_test(6, 17, "Zoom out (0.5x)", test_zoom_out)
        self._run_test(7, 17, "Pan right", test_pan_right)
        self._run_test(8, 17, "Pan down", test_pan_down)
        self._run_test(9, 17, "Center on middle", test_center_middle)
        self._run_test(10, 17, "Center on top-left quadrant", test_center_quadrant1)
        self._run_test(11, 17, "Center on bottom-right quadrant", test_center_quadrant2)
        self._run_test(12, 17, "Zoom at screen point", test_zoom_at_point)
        self._run_test(13, 17, "Reset view again", test_reset_again)
        self._run_test(14, 17, "Verify animation smoothness", test_animation_smoothness)

    def _test_polygon_operations(self):
        """Test polygon loading and control"""
        if not self.polygon_path:
            return

        def test_load_polygons():
            result = self.client.call_tool('load_polygons', {'path': self.polygon_path})

            if not isinstance(result, dict):
                raise Exception(f"Invalid response type: {type(result)}")

            if 'count' not in result or 'classes' not in result:
                raise Exception("Missing 'count' or 'classes' in response")

            if not isinstance(result['classes'], list):
                raise Exception("'classes' is not a list")

            return f"Loaded {result['count']} polygons, {len(result['classes'])} classes"

        def test_hide_polygons():
            result = self.client.call_tool('set_polygon_visibility', {'visible': False})

            if not isinstance(result, dict) or 'visible' not in result:
                raise Exception("Invalid response")

            if result['visible'] != False:
                raise Exception(f"Expected visible=false, got {result['visible']}")

            return "Polygons hidden"

        def test_show_polygons():
            result = self.client.call_tool('set_polygon_visibility', {'visible': True})

            if not isinstance(result, dict) or 'visible' not in result:
                raise Exception("Invalid response")

            if result['visible'] != True:
                raise Exception(f"Expected visible=true, got {result['visible']}")

            return "Polygons shown"

        def test_query_polygons():
            # Query center region
            x = self.slide_width * 0.4
            y = self.slide_height * 0.4
            w = self.slide_width * 0.2
            h = self.slide_height * 0.2

            result = self.client.call_tool('query_polygons', {
                'x': x, 'y': y, 'w': w, 'h': h
            })

            if not isinstance(result, dict) or 'polygons' not in result:
                raise Exception("Invalid response")

            # Currently returns empty array (TODO in Application.cpp)
            return f"Query OK (returned {len(result['polygons'])} polygons)"

        self._run_test(14, 16, "Load polygons", test_load_polygons)
        self._run_test(15, 16, "Hide polygons", test_hide_polygons)
        self._run_test(16, 16, "Show polygons", test_show_polygons)
        self._run_test(17, 16, "Query polygons in region", test_query_polygons)

    def _test_snapshot_operations(self):
        """Test snapshot capture (expect failure)"""
        def test_capture_snapshot():
            # This should fail with "not yet implemented"
            try:
                self.client.call_tool('capture_snapshot', {'width': 800, 'height': 600})
                raise Exception("Expected snapshot to fail but it succeeded")
            except MCPException as e:
                # Verify it's the expected error
                if "not yet implemented" not in e.message.lower():
                    raise Exception(f"Unexpected error message: {e.message}")
                if e.code != -32603:  # Internal error
                    raise Exception(f"Unexpected error code: {e.code}")
                return f"Failed as expected: {e.message}"

        test_num = 14 if not self.polygon_path else 18
        self._run_test(test_num, 16 if not self.polygon_path else 18,
                      "Capture snapshot (expect failure)", test_capture_snapshot)

    def _validate_viewport_response(self, result: Any, operation: str):
        """Validate a viewport operation response"""
        if not isinstance(result, dict):
            raise Exception(f"{operation}: Invalid response type: {type(result)}")

        if 'position' not in result:
            raise Exception(f"{operation}: Missing 'position' in response")

        pos = result['position']
        if 'x' not in pos or 'y' not in pos:
            raise Exception(f"{operation}: Missing 'x' or 'y' in position")

        # Some operations return zoom, some don't
        if 'zoom' in result and result['zoom'] <= 0:
            raise Exception(f"{operation}: Invalid zoom value: {result['zoom']}")


def check_prerequisites(args) -> bool:
    """
    Check prerequisites before running tests.

    Returns:
        True if all checks pass
    """
    # Check Python version
    if sys.version_info < (3, 7):
        print("ERROR: Python 3.7+ required")
        print(f"Current version: {sys.version}")
        return False

    # Check slide file exists
    if not os.path.exists(args.slide_path):
        print(f"ERROR: Slide file not found: {args.slide_path}")
        return False

    # Check polygon file exists (if specified)
    if args.polygons and not os.path.exists(args.polygons):
        print(f"ERROR: Polygon file not found: {args.polygons}")
        return False

    return True


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Test PathView MCP server functionality',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s /data/slide.svs
  %(prog)s /data/slide.svs --polygons /data/cells.pb -v
  %(prog)s /data/slide.svs --json > results.json
        '''
    )

    parser.add_argument('slide_path',
                       help='Path to slide file (.svs, .tiff, etc.)')
    parser.add_argument('--polygons',
                       help='Path to polygon file (.pb, .protobuf)')
    parser.add_argument('--server', default='http://127.0.0.1:9000',
                       help='MCP server URL (default: http://127.0.0.1:9000)')
    parser.add_argument('--http-port', type=int, default=8080,
                       help='HTTP server port for health check (default: 8080)')
    parser.add_argument('--timeout', type=int, default=30,
                       help='Request timeout in seconds (default: 30)')
    parser.add_argument('-v', '--verbose', action='store_true',
                       help='Verbose output with full request/response details')
    parser.add_argument('--json', action='store_true',
                       help='Output results as JSON')

    args = parser.parse_args()

    # Check prerequisites
    if not check_prerequisites(args):
        sys.exit(1)

    # Create MCP client
    client = MCPClient(args.server, timeout=args.timeout, verbose=args.verbose)

    try:
        # Connect and initialize
        if not args.json:
            print("[SETUP] Connecting to MCP server...")

        client.initialize()

        if not args.json:
            print("[SETUP] ✓ MCP session initialized\n")

        # Create and run test suite
        suite = TestSuite(
            client,
            args.slide_path,
            args.polygons,
            args.http_port,
            args.verbose
        )

        suite.tracker.json_output = args.json
        tracker = suite.run_all_tests()

        # Print summary
        tracker.print_summary()

        # Exit with appropriate code
        stats = tracker.get_stats()
        sys.exit(0 if stats['failed'] == 0 else 1)

    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
        sys.exit(130)

    except MCPException as e:
        error_name = ERROR_CODES.get(e.code, f"Error {e.code}")
        print(f"\nERROR: {error_name}: {e.message}")
        sys.exit(1)

    except requests.ConnectionError:
        print("\nERROR: Cannot connect to MCP server")
        print(f"Is pathview-mcp running on {args.server}?")
        sys.exit(1)

    except Exception as e:
        print(f"\nERROR: {str(e)}")
        if args.verbose:
            import traceback
            traceback.print_exc()
        sys.exit(1)

    finally:
        client.close()


if __name__ == '__main__':
    main()
