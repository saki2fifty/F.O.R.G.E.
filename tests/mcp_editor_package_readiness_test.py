"""Bounded private handoff readiness; no editor or live credential needed."""
import ctypes
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest import mock

with mock.patch.object(sys, 'argv', [sys.argv[0], 'unused-test-tools']):
    import mcp_editor_package_test as package


class ReadinessTests(unittest.TestCase):
    config = {'token': 'offline-fixture-value', 'version': 1}

    def test_waits_for_missing_then_shared_file(self):
        path, process = mock.Mock(), mock.Mock()
        path.read_text.side_effect = [FileNotFoundError(), PermissionError(), json.dumps(self.config)]
        process.poll.return_value = None
        with mock.patch.object(package.time, 'sleep') as sleep:
            self.assertEqual(package.wait_connection(path, process), self.config)
        self.assertEqual(path.read_text.call_count, 3)
        path.read_text.assert_called_with(encoding='utf-8')
        self.assertEqual(sleep.call_count, 2)

    def test_permanent_denial_has_a_deadline(self):
        path, process = mock.Mock(), mock.Mock()
        path.read_text.side_effect = PermissionError()
        process.poll.return_value = None
        with mock.patch.object(package.time, 'monotonic', side_effect=[0, 0, .5, 2]), \
                mock.patch.object(package.time, 'sleep'):
            with self.assertRaisesRegex(AssertionError, 'timed out') as failure:
                package.wait_connection(path, process, seconds=1)
        self.assertNotIn(self.config['token'], str(failure.exception))
        self.assertEqual(path.read_text.call_count, 2)

    def test_exit_during_readiness_fails(self):
        path, process = mock.Mock(), mock.Mock()
        path.read_text.side_effect = PermissionError()
        process.poll.side_effect = [None, 7]
        with mock.patch.object(package.time, 'sleep'):
            with self.assertRaisesRegex(AssertionError, 'Editor exited'):
                package.wait_connection(path, process)
        self.assertEqual(path.read_text.call_count, 1)

    def test_exited_process_does_not_read_stale_descriptor(self):
        path, process = mock.Mock(), mock.Mock()
        path.read_text.return_value = json.dumps(self.config)
        process.poll.return_value = 0
        with self.assertRaisesRegex(AssertionError, 'Editor exited'):
            package.wait_connection(path, process)
        path.read_text.assert_not_called()

    def test_malformed_json_is_not_retried(self):
        path, process = mock.Mock(), mock.Mock()
        path.read_text.return_value = '{'
        process.poll.return_value = None
        with mock.patch.object(package.time, 'sleep') as sleep:
            with self.assertRaises(json.JSONDecodeError):
                package.wait_connection(path, process)
        sleep.assert_not_called()

    def test_unrelated_io_error_is_not_retried(self):
        path, process = mock.Mock(), mock.Mock()
        path.read_text.side_effect = OSError(5, 'fixture I/O failure')
        process.poll.return_value = None
        with self.assertRaises(OSError):
            package.wait_connection(path, process)
        self.assertEqual(path.read_text.call_count, 1)

    def test_read_reconnects_after_disconnect_without_replaying_edit(self):
        disconnected = dict(ok=False, error=dict(code='connection_unavailable',
            message='Editor disconnected; inspect state before repeating an edit'))
        timed_out = dict(ok=False, error=dict(code='connection_unavailable',
            message='Editor request timed out; inspect state or retrieve the same sequence receipt'))
        client, process = mock.Mock(), mock.Mock()
        process.poll.return_value = None
        client.tool.side_effect = [disconnected, timed_out, dict(ok=True, result='ready')]
        recoveries = []
        descriptor = dict(name='forge_discover', annotations=dict(readOnlyHint=True))
        with mock.patch.object(package.time, 'sleep'):
            self.assertEqual(package.read_tool(client, descriptor, recoveries, process)['result'], 'ready')
        self.assertEqual(client.tool.call_count, 3)
        self.assertEqual([r['error'] for r in recoveries], [disconnected['error'], timed_out['error']])

    def test_disconnected_read_still_has_a_deadline(self):
        client, process = mock.Mock(), mock.Mock()
        process.poll.return_value = None
        client.tool.return_value = dict(ok=False, error=dict(code='connection_unavailable',
            message='Editor disconnected; inspect state before repeating an edit'))
        descriptor = dict(name='forge_discover', annotations=dict(readOnlyHint=True))
        with mock.patch.object(package.time, 'monotonic', side_effect=[0, 0, 0, 1, 61]), \
                mock.patch.object(package.time, 'sleep'):
            with self.assertRaisesRegex(AssertionError, 'timed out'):
                package.read_tool(client, descriptor, [], process)
        self.assertEqual(client.tool.call_count, 1)

    def test_read_recovery_rejects_native_exit(self):
        client, process = mock.Mock(), mock.Mock()
        process.poll.return_value = 7
        descriptor = dict(name='forge_discover', annotations=dict(readOnlyHint=True))
        with self.assertRaisesRegex(AssertionError, 'Native editor exited'):
            package.read_tool(client, descriptor, [], process)
        client.tool.assert_not_called()

    def test_mutation_never_uses_read_recovery(self):
        client = mock.Mock()
        descriptor = dict(name='forge_entity_create', annotations=dict(readOnlyHint=False))
        with self.assertRaisesRegex(AssertionError, 'read-only'):
            package.read_tool(client, descriptor, [], mock.Mock())
        client.tool.assert_not_called()

    def test_other_read_failures_are_not_retried(self):
        descriptor = dict(name='forge_discover', annotations=dict(readOnlyHint=True))
        for code in ('unauthorized', 'stale_target', 'invalid_arguments'):
            client, process = mock.Mock(), mock.Mock()
            process.poll.return_value = None
            client.tool.return_value = dict(ok=False, error=dict(code=code, message='fixture error'))
            with self.assertRaises(AssertionError):
                package.read_tool(client, descriptor, [], process)
            self.assertEqual(client.tool.call_count, 1)

    @unittest.skipUnless(os.name == 'nt', 'Native Windows sharing contract')
    def test_native_delete_handle_releases_before_readiness(self):
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong,
                                      ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        kernel.CloseHandle.restype = ctypes.c_int
        with tempfile.TemporaryDirectory(prefix='FORGE handoff readiness ') as folder:
            path = Path(folder) / 'connection.json'
            path.write_text(json.dumps(self.config), encoding='utf-8')
            # Match the atomic writer's access/sharing at final publication.
            handle = kernel.CreateFileW(str(path), 0x40000000 | 0x10000, 1, None, 3, 0, None)
            self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
            release, closed = threading.Event(), []

            def close_writer():
                release.wait(5)
                closed.append(bool(kernel.CloseHandle(handle)))

            worker = threading.Thread(target=close_writer)
            worker.start()
            try:
                with self.assertRaises(PermissionError):
                    path.read_text(encoding='utf-8')
                observed = mock.Mock()

                def read(**arguments):
                    try:
                        return path.read_text(**arguments)
                    except PermissionError:
                        release.set()
                        raise

                observed.read_text.side_effect = read
                process = mock.Mock()
                process.poll.return_value = None
                self.assertEqual(package.wait_connection(observed, process, seconds=5), self.config)
                self.assertGreaterEqual(observed.read_text.call_count, 2)
            finally:
                release.set()
                worker.join(6)
            self.assertFalse(worker.is_alive())
            self.assertEqual(closed, [True])


if __name__ == '__main__':
    unittest.main()
