"""Regression tests for WebSocket diagnostics and exception-safe cleanup."""

import unittest
from unittest.mock import Mock

from opksrc_signaling_test import Soup, cleanup_steps, close_websocket, wait_for_signaling


class SignalingHelpersTest(unittest.TestCase):
    def test_closed_socket_reports_code_and_reason(self):
        websocket = Mock()
        websocket.get_state.return_value = Soup.WebsocketState.CLOSED
        websocket.get_close_code.return_value = 1008
        websocket.get_close_data.return_value = "Invalid WebRTC negotiation"
        with self.assertRaisesRegex(AssertionError, "SDP accepted.*1008.*Invalid WebRTC negotiation"):
            wait_for_signaling(websocket, lambda: False, "SDP accepted")

    def test_answer_timeout_reports_open_state(self):
        websocket = Mock()
        websocket.get_state.return_value = Soup.WebsocketState.OPEN
        with self.assertRaisesRegex(AssertionError, "SDP accepted.*open"):
            wait_for_signaling(websocket, lambda: False, "SDP accepted", timeout=0)
        websocket.get_close_code.assert_not_called()

    def test_answer_succeeds(self):
        websocket = Mock()
        websocket.get_state.return_value = Soup.WebsocketState.OPEN
        wait_for_signaling(websocket, lambda: True, "SDP accepted")

    def test_close_only_open_socket(self):
        for state in (Soup.WebsocketState.OPEN, Soup.WebsocketState.CLOSING,
                      Soup.WebsocketState.CLOSED):
            websocket = Mock()
            websocket.get_state.return_value = state
            close_websocket(websocket)
            self.assertEqual(websocket.close.call_count, int(state == Soup.WebsocketState.OPEN))

    def test_cleanup_preserves_original_and_runs_remaining_steps(self):
        original = AssertionError("SDP accepted")
        failed = Mock(side_effect=RuntimeError("cleanup failure"))
        remaining = Mock()
        with self.assertRaises(AssertionError) as raised:
            try:
                raise original
            finally:
                cleanup_steps(failed, remaining)
        self.assertIs(raised.exception, original)
        self.assertIn("cleanup failure", original.__notes__[0])
        remaining.assert_called_once_with()

    def test_cleanup_failure_still_fails_successful_test(self):
        remaining = Mock()
        with self.assertRaisesRegex(RuntimeError, "cleanup failure"):
            cleanup_steps(Mock(side_effect=RuntimeError("cleanup failure")), remaining)
        remaining.assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
