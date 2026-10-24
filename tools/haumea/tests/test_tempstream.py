# SPDX-License-Identifier: GPL-2.0-or-later
"""Scratch code streams support seeks, binary data and concurrent generation."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class TemporaryCodeStreamTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'needs a C++ compiler')
    def test_streams(self):
        src = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='haumea-code-stream-') as tmp:
            binary = Path(tmp, 'check')
            subprocess.run(['c++', '-std=c++17', '-I', str(src),
                            str(src / 'tests/fixtures/tempstream.cpp'), '-o', str(binary)],
                           check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn('Temporary code streams: passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
