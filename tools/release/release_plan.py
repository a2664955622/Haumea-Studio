#!/usr/bin/env python3
"""Decide what a CI run releases, and refuse what it mustn't.

    release_plan.py --event E --ref-type T --ref-name R --version V
                    [--channel C] [--release build-only|draft|publish]
                    [--on-main yes|no] [--tag-exists yes|no]

Prints GitHub step outputs (key=value lines): channel, release (build-only,
draft or publish), tag, prerelease and latest. Exits 1 with a message for a
run that must not release.

- A pull request: channel pr, build only, never released.
- A pushed v<version>-beta / -production tag: that channel, publish.
- A manual run (workflow_dispatch): the chosen channel (beta by default) and
  release (build only by default).
- Any other push (main): channel dev, build only.
Only beta and production are ever released (the updater follows nothing
else); production only from main; never over a tag that already exists
(a pushed tag is the release's own, so it is allowed).
"""
import argparse
import sys

p = argparse.ArgumentParser()
p.add_argument('--event', required=True)
p.add_argument('--ref-type', default='branch')
p.add_argument('--ref-name', required=True)
p.add_argument('--version', required=True, help='1.0.0.<build>')
p.add_argument('--channel', default='')
p.add_argument('--release', default='')
p.add_argument('--on-main', default='no', help='the commit is on main')
p.add_argument('--tag-exists', default='no', help='the tag this run would use already exists')
a = p.parse_args()


def refuse(message):
    print(f'::error::{message}', file=sys.stderr)
    sys.exit(1)


release = (a.release or 'build only').replace(' ', '-')
if release not in ('build-only', 'draft', 'publish'):
    refuse(f'release must be "build only", draft or publish, not "{a.release}"')

if a.event == 'pull_request':
    channel, release = 'pr', 'build-only'
elif a.event == 'workflow_dispatch':
    channel = a.channel or 'beta'
elif a.ref_type == 'tag':
    if not a.ref_name.startswith('v') or '-' not in a.ref_name:
        refuse(f'{a.ref_name} is not a release tag (v<version>-beta or -production)')
    channel = a.ref_name.split('-', 1)[1]
    release = 'publish'
else:
    channel, release = 'dev', 'build-only'

tag = f'v{a.version}-{channel}'
if release != 'build-only':
    if channel not in ('beta', 'production'):
        refuse(f'only beta and production are released, not "{channel}"')
    if a.ref_type == 'tag' and a.ref_name != tag:
        refuse(f'the tag {a.ref_name} does not match this build ({tag})')
    if channel == 'production' and a.on_main != 'yes':
        refuse('production is released only from main')
    if a.tag_exists == 'yes' and a.ref_type != 'tag':
        refuse(f'{tag} already exists: a release is never made over an existing tag')

print(f'channel={channel}')
print(f'release={release}')
print(f'tag={tag}')
print(f'prerelease={"true" if channel == "beta" else "false"}')
print(f'latest={"true" if channel == "production" and release == "publish" else "false"}')
