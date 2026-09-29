"""Tests for the zoom/report service (app.py) without the real ESP32 or database.

Run from this folder:  python3 -m unittest -v test_app
The ESP32 is replaced by local stand-ins: a closed port (camera off), a port that accepts
but never answers (camera hung) and a small WebSocket server (camera working).
"""
import asyncio
import contextlib
import io
import socket
import threading
import time
import unittest

import websockets

import app as service

CLOSED_PORT_URL = "ws://127.0.0.1:9"   # nothing listens here: connection refused at once


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class SilentServer:
    """Accepts TCP connections and never answers (a hung camera)."""

    def __init__(self):
        self.sock = socket.socket()
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(50)
        self.port = self.sock.getsockname()[1]
        self.conns = []
        self.thread = threading.Thread(target=self._accept, daemon=True)
        self.thread.start()

    def _accept(self):
        while True:
            try:
                conn, _ = self.sock.accept()
                self.conns.append(conn)
            except OSError:
                return

    def close(self):
        self.sock.close()
        for c in self.conns:
            c.close()


class FakeEsp32:
    """WebSocket server that records the commands it receives (a working camera)."""

    def __init__(self):
        self.port = free_port()
        self.received = []
        self.connections = 0
        self.loop = asyncio.new_event_loop()
        self.ready = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()
        assert self.ready.wait(5)

    def _run(self):
        asyncio.set_event_loop(self.loop)

        async def handler(ws):
            self.connections += 1
            async for message in ws:
                self.received.append(message)

        async def main():
            async with websockets.serve(handler, "127.0.0.1", self.port):
                self.ready.set()
                await asyncio.Future()

        with contextlib.suppress(Exception):
            self.loop.run_until_complete(main())


class ServiceTest(unittest.TestCase):
    def setUp(self):
        service.app.testing = True
        self.client = service.app.test_client()
        self.original_url = service.ESP_WS_URL
        service.ws = None

    def tearDown(self):
        service.ESP_WS_URL = self.original_url
        service.ws = None

    def zoom(self, direction="zoom_in"):
        start = time.monotonic()
        response = self.client.post("/move-camera", json={"direction": direction, "model": "ESP32"})
        return response, time.monotonic() - start

    # ------------------------------------------------------------------ basic routes
    def test_status_answers(self):
        response = self.client.get("/status")
        self.assertEqual(response.status_code, 200)
        self.assertIn("websocket_connected", response.get_json())

    def test_invalid_direction_rejected(self):
        response = self.client.post("/move-camera", json={"direction": "sideways"})
        self.assertEqual(response.status_code, 400)

    def test_generate_pdf_requires_ids(self):
        response = self.client.post("/generate-pdf", json={"patient_id": "P1"})
        self.assertEqual(response.status_code, 400)
        self.assertIn("Missing", response.get_json()["error"])

    # ------------------------------------------------------------------ camera off / hung
    def test_zoom_with_camera_off_fails_fast(self):
        service.ESP_WS_URL = CLOSED_PORT_URL
        response, seconds = self.zoom()
        self.assertEqual(response.status_code, 500)
        self.assertIn("not reachable", response.get_json()["message"])
        self.assertLess(seconds, 2)

    def test_zoom_with_hung_camera_times_out(self):
        silent = SilentServer()
        try:
            service.ESP_WS_URL = f"ws://127.0.0.1:{silent.port}"
            response, seconds = self.zoom()
            self.assertIn(response.status_code, (500, 504))
            self.assertLess(seconds, service.ZOOM_REQUEST_TIMEOUT + 1.5)
        finally:
            silent.close()

    def test_zoom_request_never_waits_past_the_limit(self):
        original = service.send_command_to_esp

        async def never_finishes(action):
            await asyncio.sleep(60)

        service.send_command_to_esp = never_finishes
        try:
            response, seconds = self.zoom()
        finally:
            service.send_command_to_esp = original
        self.assertEqual(response.status_code, 504)
        self.assertAlmostEqual(seconds, service.ZOOM_REQUEST_TIMEOUT, delta=1)

    def test_many_zoom_requests_leave_no_stuck_threads(self):
        """The old service kept one blocked thread per zoom tap while the camera was off."""
        service.ESP_WS_URL = CLOSED_PORT_URL
        before = threading.active_count()
        results = []

        def tap():
            client = service.app.test_client()
            results.append(client.post("/move-camera", json={"direction": "zoom_out"}).status_code)

        threads = [threading.Thread(target=tap) for _ in range(20)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(10)
        self.assertTrue(all(not t.is_alive() for t in threads), "a zoom request never returned")
        self.assertEqual(results, [500] * 20)
        time.sleep(0.2)
        self.assertLessEqual(threading.active_count(), before)

    def test_camera_off_logged_once_not_every_retry(self):
        """Background retries used to print a line every 5 s, which filled the terminal.
        Runs the real retry loop for ~11 s: three attempts, one line."""
        # The service's own startup connection loop reports once, after its first 3 s attempt;
        # let that happen before capturing so only this loop's output is counted
        time.sleep(service.ESP_CONNECT_TIMEOUT + 0.5)
        service.ESP_WS_URL = CLOSED_PORT_URL
        output = io.StringIO()
        loop = asyncio.new_event_loop()
        try:
            with contextlib.redirect_stdout(output):
                with self.assertRaises(asyncio.TimeoutError):
                    loop.run_until_complete(asyncio.wait_for(service.connect_to_esp(), timeout=11))
        finally:
            loop.close()
        lines = [l for l in output.getvalue().splitlines() if l.strip()]
        self.assertEqual(len(lines), 1, lines)
        self.assertIn("not reachable", lines[0])

    # ------------------------------------------------------------------ camera working
    def test_zoom_reaches_working_camera(self):
        esp = FakeEsp32()
        service.ESP_WS_URL = f"ws://127.0.0.1:{esp.port}"
        response, seconds = self.zoom("zoom_in")
        self.assertEqual(response.status_code, 200, response.get_json())
        response, _ = self.zoom("zoom_out")
        self.assertEqual(response.status_code, 200, response.get_json())
        deadline = time.monotonic() + 3
        while len(esp.received) < 2 and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertEqual(esp.received, ["$Z_P#", "$Z_M#"])
        self.assertLess(seconds, 2)
        print(f"\n  working camera: {esp.connections} WebSocket connection(s) for 2 zoom commands")


if __name__ == "__main__":
    unittest.main(verbosity=2)
