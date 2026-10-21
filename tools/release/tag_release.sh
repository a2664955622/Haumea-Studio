#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# tag_release.sh [--create] [production|beta]: the release tag for HEAD,
# v1.0.0.<build>-<channel> (production unless given), as open.mp tags its
# releases. Prints it; --create makes it as a local tag. Never pushes:
# pushing the tag is what releases it (docs/RELEASE_CHECKLIST.md).
#
# Refuses a dirty tree, another channel, and a version (all four parts) that
# isn't newer than the newest tag of that channel.
set -eu
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
create=0
channel=production
for arg in "$@"; do
	case $arg in
	--create) create=1 ;;
	-*) echo "usage: tag_release.sh [--create] [channel]" >&2; exit 2 ;;
	*) channel=$arg ;;
	esac
done
case $channel in
production|beta) ;;
*) echo "the channel is production or beta, not $channel" >&2; exit 2 ;;
esac
cd "$HERE/../.."
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
	echo "the tree has uncommitted changes: commit them first" >&2
	exit 1
fi
set -- $(python3 "$HERE/gen_buildinfo.py" --print)
tag="v$1-$channel"
python3 - "$tag" "$channel" $(git tag --list 'v*') <<'EOF'
import sys
sys.path.insert(0, 'tools/release')
from check_tag import parse_tag
tag, channel, existing = sys.argv[1], sys.argv[2], sys.argv[3:]
def key(p):
	return p[:3] + (p[3] or 0,)
mine = key(parse_tag(tag))
older = [parse_tag(t) for t in existing]
newest = max((key(p) for p in older if p and p[4] == channel), default=None)
if newest is not None and mine <= newest:
	sys.exit(f"{tag}: {'.'.join(map(str, mine))} is not newer than {'.'.join(map(str, newest))}, "
	         f"the newest {channel} tag")
EOF
if [ "$create" = 1 ]; then
	git tag -a "$tag" -m "Haumea Studio $1 ($channel)"
	echo "created $tag (not pushed)"
else
	echo "$tag"
fi
