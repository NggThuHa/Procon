import json
import unittest
from unittest import mock

from tools import ws_bridge


class _Response:
    status = 200

    @staticmethod
    def read():
        return b'{"valid":true}'


class _Connection:
    instances = []

    def __init__(self, host, port, timeout):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.request_args = None
        self.closed = False
        type(self).instances.append(self)

    def request(self, method, path, body=None, headers=None):
        self.request_args = (method, path, body, headers)

    @staticmethod
    def getresponse():
        return _Response()

    def close(self):
        self.closed = True


class TestWsAssignmentRouting(unittest.TestCase):
    def setUp(self):
        _Connection.instances = []
        ws_bridge._upstream = "judge.test"
        ws_bridge._upstream_port = 80
        ws_bridge._match_id = "m-test"
        ws_bridge._token = "test-token"
        ws_bridge._plain = True

    def test_flat_assignment_is_posted_to_rest_endpoint(self):
        with mock.patch.object(ws_bridge.http.client, "HTTPConnection", _Connection):
            status, result = ws_bridge._post_assignment([0, 0, 1, 0])

        self.assertEqual(status, 200)
        self.assertEqual(result, {"valid": True})
        self.assertEqual(len(_Connection.instances), 1)
        connection = _Connection.instances[0]
        method, path, body, headers = connection.request_args
        self.assertEqual((connection.host, connection.port), ("judge.test", 80))
        self.assertEqual((method, path),
                         ("POST", "/api/v1/matches/m-test/assignment"))
        self.assertEqual(json.loads(body), [0, 0, 1, 0])
        self.assertEqual(headers["Authorization"], "Bearer test-token")
        self.assertTrue(connection.closed)


if __name__ == "__main__":
    unittest.main()
