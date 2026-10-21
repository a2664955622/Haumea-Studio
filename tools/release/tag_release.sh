#!/bin/sh
# tag_release.sh [--create] [channel]: the release tag for HEAD,
# v1.41.0.<build>-<channel> (channel: master unless given), as open.mp tags
# its releases. Prints it; --create makes it as a local tag. Never pushes.
#
# Refuses a dirty tree, and a version whose 1.41.0 is not newer than the
# newest v* tag of that channel: installs from before four-part versions
# compare only the first three parts, and the build number restarted when
# main was squashed, so every release bumps PATCH (docs/RELEASE_CHECKLIST.md).
set -eu
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
create=0
channel=master
for arg in "$@"; do
	case $arg in
	--create) create=1 ;;
	-*) echo "usage: tag_release.sh [--create] [channel]" >&2; exit 2 ;;
	*) channel=$arg ;;
	esac
done
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
mine = parse_tag(tag)[:3]
older = [parse_tag(t) for t in existing]
newest = max((p[:3] for p in older if p and p[4] == channel), default=None)
if newest is not None and mine <= newest:
	sys.exit(f"{tag}: {'.'.join(map(str, mine))} is not newer than {'.'.join(map(str, newest))}, "
	         f"the newest {channel} tag: bump HAUMEA_VERSION_PATCH in tools/haumea/version.h")
EOF
if [ "$create" = 1 ]; then
	git tag -a "$tag" -m "Haumea Studio $1 ($channel)"
	echo "created $tag (not pushed)"
else
	echo "$tag"
fi
