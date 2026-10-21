# SPDX-License-Identifier: GPL-2.0-or-later
"""The agent client explains a socket path too long for AF_UNIX.

Needs nothing but Python: no editor is started.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'agent'))
from haumea_ipc import HaumeaClient, HaumeaError, SUN_PATH_MAX  # noqa: E402


class AgentSocketTests(unittest.TestCase):
    def test_long_path_says_what_to_do(self):
        path = '/tmp/' + 'x' * SUN_PATH_MAX + '.sock'
        with self.assertRaises(HaumeaError) as caught:
            HaumeaClient(path).command('ping')
        message = str(caught.exception)
        self.assertIn('AF_UNIX', message)
        self.assertIn('haumea.sock', message)
        self.assertIn('--agent-socket', message)

    def test_short_missing_path_is_unreachable(self):
        with self.assertRaises(HaumeaError) as caught:
            HaumeaClient('/tmp/haumea-test-no-such.sock').command('ping')
        self.assertIn('not reachable', str(caught.exception))


if __name__ == '__main__':
    unittest.main()
