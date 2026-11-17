"""Lossless retained protocol evidence, independent of native tool execution."""
import gzip
import json
from pathlib import Path
import tempfile
import unittest

import mcp_test_client as client


class CaptureWriterTests(unittest.TestCase):
    def check_capture(self, records):
        previous = client.CAPTURE
        client.CAPTURE = records
        try:
            with tempfile.TemporaryDirectory(prefix='forge-capture-writer-') as folder:
                # Match the original text stream's platform newline policy too.
                oracle = Path(folder) / 'oracle.json'
                with oracle.open('wt', encoding='utf-8') as output:
                    json.dump(records, output, separators=(',', ':'))
                    output.write('\n')
                expected = oracle.read_bytes()
                for suffix in ('.json', '.json.gz'):
                    path = Path(folder) / ('capture' + suffix)
                    client.write_capture(path)
                    actual = gzip.decompress(path.read_bytes()) if suffix.endswith('.gz') else path.read_bytes()
                    self.assertEqual(actual, expected)
        finally:
            client.CAPTURE = previous

    def test_empty(self):
        self.check_capture([])

    def test_order_and_nested_messages(self):
        self.check_capture([
            {'direction': 'request', 'id': 2, 'method': 'tools/list', 'params': {}},
            {'direction': 'response', 'id': 2, 'result': {'tools': [{'name': 'sample'}]}},
            {'direction': 'request', 'id': 1, 'params': [None, True, False, {}, []]},
            {'direction': 'response', 'id': 1, 'error': {'code': -32602, 'message': 'Invalid input'}},
        ])

    def test_text_and_numeric_encoding(self):
        self.check_capture([{'text': '雪 é \0\r\n\t\"\\\u2028',
                             'numbers': [0, -0.0, 2**64 - 1, -2**63, 1.5e-12, 1.2e30,
                                         float('inf'), float('-inf'), float('nan')]}])

    def test_large_repeated_schema_messages(self):
        schema = {'properties': {f'field_{i}': {'type': 'string', 'description': 'A' * 128}
                                 for i in range(160)}}
        self.check_capture([{'id': i, 'result': schema} for i in range(32)])

    def test_nested_values(self):
        value = {'leaf': 'retained'}
        for _ in range(32):
            value = {'next': [value]}
        self.check_capture([value, {'complete': True}])

    def test_serialization_failure_remains_visible(self):
        previous = client.CAPTURE
        client.CAPTURE = [{'invalid': object()}]
        try:
            with tempfile.TemporaryDirectory(prefix='forge-capture-invalid-') as folder:
                with self.assertRaises(TypeError):
                    client.write_capture(Path(folder) / 'capture.json.gz')
        finally:
            client.CAPTURE = previous


if __name__ == '__main__':
    unittest.main(argv=[__file__])
