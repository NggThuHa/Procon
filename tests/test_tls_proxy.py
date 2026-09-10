import io
import unittest
from unittest import mock

from tools import tls_proxy


class _BrokenConnection:
    def __init__(self):
        self.requests = 0
        self.closed = False

    def request(self, method, path, body=None, headers=None):
        self.requests += 1
        raise TimeoutError("test timeout")

    def close(self):
        self.closed = True


class _Handler:
    def __init__(self):
        self.headers = {"Content-Length": "0"}
        self.rfile = io.BytesIO()
        self.wfile = io.BytesIO()
        self.path = "/api/v1/matches/m-test/actions"
        self.statuses = []

    def send_response(self, status):
        self.statuses.append(status)

    def send_header(self, name, value):
        pass

    def end_headers(self):
        pass


class TestTlsProxyRetrySafety(unittest.TestCase):
    def test_post_is_not_retried_after_ambiguous_network_error(self):
        handler = _Handler()
        connection = _BrokenConnection()
        handler._take_upstream = mock.Mock(return_value=connection)

        with mock.patch.object(tls_proxy, "UPSTREAM", "judge.test"):
            tls_proxy.Proxy._forward(handler, "POST")

        self.assertEqual(connection.requests, 1)
        self.assertEqual(handler._take_upstream.call_count, 1)
        self.assertEqual(handler.statuses, [502])
        self.assertTrue(connection.closed)


if __name__ == "__main__":
    unittest.main()
