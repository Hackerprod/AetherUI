#!/usr/bin/env python3
"""Failure-path tests without invoking any renderer or child process."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import run

class RunnerTests(unittest.TestCase):
    def scenario(self, mode):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'run'
            def fake(command, **kwargs):
                stdout = b''
                code = 0
                if command[0] == 'git': stdout = b'abc123\n' if 'status' not in command else b''
                elif command[0] == 'fc-match': stdout = b'OpenAI Sans\n/usr/share/fonts/example.ttf\n'
                elif command[0] == 'dotnet':
                    if mode == 'missing-tool': raise FileNotFoundError('missing dotnet')
                    if '--version' in command: stdout = b'10.0.201\n'
                    if 'build' in command and mode == 'compile-error': code = 1
                    if any(str(c).endswith('VisualTests.dll') for c in command) and mode == 'crash': code = -6
                    if any(str(c).endswith('VisualTests.dll') for c in command) and mode == 'malformed-summary':
                        captures = output / 'captures'; captures.mkdir(); (captures / 'summary.json').write_text('{')
                return subprocess.CompletedProcess(command, code, stdout, b'')
            with patch.object(sys, 'argv', ['run.py', '--output', str(output)]), patch.object(subprocess, 'run', fake), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                code = run.main()
            manifest = json.loads((output / 'manifest.json').read_text())
            return code, manifest
    def test_compile_failure_is_fail(self):
        code, manifest = self.scenario('compile-error')
        self.assertEqual((code, manifest['linux']), (1, 'FAIL'))
    def test_missing_summary_cannot_succeed(self):
        code, manifest = self.scenario('no-summary')
        self.assertEqual((code, manifest['linux']), (1, 'FAIL'))
    def test_crash_is_fail(self):
        code, manifest = self.scenario('crash')
        self.assertEqual((code, manifest['linux']), (1, 'FAIL'))
    def test_malformed_summary_is_fail(self):
        code, manifest = self.scenario('malformed-summary')
        self.assertEqual((code, manifest['linux']), (1, 'FAIL'))
    def test_missing_tool_is_blocked(self):
        code, manifest = self.scenario('missing-tool')
        self.assertEqual((code, manifest['linux']), (2, 'BLOCKED'))

if __name__ == '__main__': unittest.main()
