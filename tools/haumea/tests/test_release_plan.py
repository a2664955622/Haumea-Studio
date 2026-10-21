"""tools/release/release_plan.py: what each kind of CI run releases, and what it refuses."""
from pathlib import Path
import subprocess
import sys
import unittest

PLAN = Path(__file__).resolve().parents[3] / 'tools' / 'release' / 'release_plan.py'


def plan(**kw):
    args = [sys.executable, str(PLAN)]
    for key, value in kw.items():
        args += ['--' + key.replace('_', '-'), value]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        return result.stderr.strip()
    return dict(line.split('=', 1) for line in result.stdout.splitlines())


class ReleasePlanTests(unittest.TestCase):
    def test_pull_requests_and_branch_pushes_only_build(self):
        self.assertEqual(plan(event='pull_request', ref_name='12/merge', version='1.0.0.7', release='publish'),
                         {'channel': 'pr', 'release': 'build-only', 'tag': 'v1.0.0.7-pr',
                          'prerelease': 'false', 'latest': 'false'})
        self.assertEqual(plan(event='push', ref_name='main', version='1.0.0.7')['release'], 'build-only')
        self.assertEqual(plan(event='push', ref_name='main', version='1.0.0.7')['channel'], 'dev')

    def test_manual_runs_default_to_a_beta_build_only(self):
        p = plan(event='workflow_dispatch', ref_name='main', version='1.0.0.7')
        self.assertEqual((p['channel'], p['release']), ('beta', 'build-only'))

    def test_manual_publish_and_draft(self):
        p = plan(event='workflow_dispatch', ref_name='main', version='1.0.0.7', channel='production',
                 release='publish', on_main='yes')
        self.assertEqual((p['tag'], p['prerelease'], p['latest']), ('v1.0.0.7-production', 'false', 'true'))
        p = plan(event='workflow_dispatch', ref_name='feature', version='1.0.0.7', channel='beta', release='draft')
        self.assertEqual((p['tag'], p['release'], p['prerelease'], p['latest']),
                         ('v1.0.0.7-beta', 'draft', 'true', 'false'))

    def test_refusals(self):
        refused = [
            dict(event='workflow_dispatch', ref_name='feature', version='1.0.0.7', channel='production',
                 release='publish', on_main='no'),
            dict(event='workflow_dispatch', ref_name='main', version='1.0.0.7', channel='beta', release='publish',
                 tag_exists='yes'),
            dict(event='workflow_dispatch', ref_name='main', version='1.0.0.7', channel='dev', release='publish'),
            dict(event='push', ref_type='tag', ref_name='v1.0.0.6-beta', version='1.0.0.7'),
            dict(event='push', ref_type='tag', ref_name='v1.0.0.7-master', version='1.0.0.7'),
            dict(event='push', ref_type='tag', ref_name='v1.0.0.7-production', version='1.0.0.7', on_main='no'),
            dict(event='workflow_dispatch', ref_name='main', version='1.0.0.7', release='ship it'),
        ]
        for kw in refused:
            self.assertIsInstance(plan(**kw), str, kw)

    def test_a_pushed_tag_publishes_its_channel(self):
        p = plan(event='push', ref_type='tag', ref_name='v1.0.0.7-beta', version='1.0.0.7', tag_exists='yes')
        self.assertEqual((p['channel'], p['release'], p['prerelease']), ('beta', 'publish', 'true'))
        p = plan(event='push', ref_type='tag', ref_name='v1.0.0.7-production', version='1.0.0.7', on_main='yes',
                 tag_exists='yes')
        self.assertEqual((p['channel'], p['latest']), ('production', 'true'))


if __name__ == '__main__':
    unittest.main()
