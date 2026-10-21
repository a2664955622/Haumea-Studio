#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# fetch_toolchain.sh [dir]: the Pawn compiler and include sets the generated
# maps are checked against, at pinned versions. Nothing is kept in the repo;
# the default dir is tools/haumea/tests/pawn/.toolchain (git-ignored).
set -eu
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
DIR=${1:-${PAWN_TOOLCHAIN:-$HERE/.toolchain}}

# open.mp server: qawno/pawncc 3.10.11 and the open.mp includes
OMP_URL=https://github.com/openmultiplayer/open.mp/releases/download/v1.6.7.3218/open.mp-linux-x86.tar.gz
OMP_SHA256=0f7bfe70bb68ea780fe24a910b69412046e53d10feb65a9d4a9de8ff1f7f1329
# SA-MP 0.3.7 includes: samp-stdlib with the Pawn core includes it expects
SAMP_STDLIB=8ffb055624308b25521665b60e78b5e6e6b3717f
PAWN_STDLIB=e96507d9a6ddaae5bb0f3ec31479ba805aeff964
# streamer.inc for CreateDynamic* maps, and the Linux plugin for server_check.py
STREAMER_TAG=v2.9.6
STREAMER_ZIP_URL=https://github.com/samp-incognito/samp-streamer-plugin/releases/download/$STREAMER_TAG/samp-streamer-plugin-2.9.6.zip
STREAMER_ZIP_SHA256=bf30ab50a8d44e9e0fbf0d02cacf128fc36f147d9711881928716e76288f4700

# The plugin on its own, so a toolchain fetched before it was added gets it too.
if [ ! -f "$DIR/streamer/streamer.so" ]; then
	mkdir -p "$DIR/streamer"
	TMPZIP=$(mktemp)
	curl -fsSL -o "$TMPZIP" "$STREAMER_ZIP_URL"
	echo "$STREAMER_ZIP_SHA256  $TMPZIP" | sha256sum -c - >/dev/null
	python3 -c 'import sys, zipfile; open(sys.argv[2], "wb").write(zipfile.ZipFile(sys.argv[1]).read("plugins/streamer.so"))' \
		"$TMPZIP" "$DIR/streamer/streamer.so"
	rm -f "$TMPZIP"
fi

if [ -x "$DIR/bin/pawncc" ] && [ -f "$DIR/samp037/a_samp.inc" ] && [ -f "$DIR/streamer/streamer.inc" ]; then
	exit 0
fi
mkdir -p "$DIR"
cd "$DIR"
curl -fsSL -o omp.tar.gz "$OMP_URL"
echo "$OMP_SHA256  omp.tar.gz" | sha256sum -c - >/dev/null
rm -rf omp && mkdir omp && tar -xzf omp.tar.gz -C omp && rm omp.tar.gz

rm -rf samp037 && mkdir samp037
curl -fsSL "https://github.com/pawn-lang/pawn-stdlib/archive/$PAWN_STDLIB.tar.gz" | tar -xz
curl -fsSL "https://github.com/pawn-lang/samp-stdlib/archive/$SAMP_STDLIB.tar.gz" | tar -xz
cp pawn-stdlib-$PAWN_STDLIB/*.inc samp037/
cp samp-stdlib-$SAMP_STDLIB/*.inc samp037/
rm -rf pawn-stdlib-$PAWN_STDLIB samp-stdlib-$SAMP_STDLIB

mkdir -p streamer
curl -fsSL -o streamer/streamer.inc \
	"https://raw.githubusercontent.com/samp-incognito/samp-streamer-plugin/$STREAMER_TAG/streamer.inc"
# bin/pawncc runs a copy of qawno's compiler kept apart from qawno/include,
# which pawncc would otherwise always search: a SA-MP 0.3.7 compile must see
# only the 0.3.7 includes, or #tryinclude <open.mp> finds open.mp's.
rm -rf bin && mkdir -p bin/compiler
cp omp/Server/qawno/pawncc omp/Server/qawno/libpawnc.so bin/compiler/
cat > bin/pawncc <<'WRAP'
#!/bin/sh
BIN=$(CDPATH='' cd -- "$(dirname -- "$0")/compiler" && pwd)
LD_LIBRARY_PATH=$BIN${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} exec "$BIN/pawncc" "$@"
WRAP
chmod +x bin/pawncc
echo "Pawn toolchain in $DIR"
