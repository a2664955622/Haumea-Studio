# Building and testing Haumea Studio

## Build

Haumea builds with Premake and librw. Set `LIBRW` to the librw source folder, and run the
commands from the Haumea checkout. `patch_librw_haumea.py` also swaps librw's Dear ImGui for
the docking build in `vendor/imgui-docking`. Without it Haumea still builds, but panels float
instead of docking.

### Linux x86_64

```sh
export LIBRW=/path/to/librw

(cd "$LIBRW" && git apply "$OLDPWD/patches/librw-haumea.patch")
python3 tools/release/patch_librw_haumea.py "$LIBRW"
(cd "$LIBRW" && premake5 gmake2)
make -C "$LIBRW/build" config=release_linux-amd64-gl3 librw

premake5 gmake2
make -C build -j2 config=release_linux-amd64-gl3 haumea
```

The program is `bin/linux-amd64-gl3/Release/haumea`.

### macOS

```sh
export LIBRW=/path/to/librw

(cd "$LIBRW" && git apply "$OLDPWD/patches/librw-haumea.patch")
python3 tools/release/patch_librw_haumea.py "$LIBRW"
(cd "$LIBRW" && premake5 gmake2 --gfxlib=glfw)
make -C "$LIBRW/build" config=release_macos-arm64-gl3 librw  # Apple Silicon
# or: make -C "$LIBRW/build" config=release_macos-amd64-gl3 librw  # Intel

premake5 gmake2
make -C build config=release_macos-arm64-gl3 haumea  # Apple Silicon
# or: make -C build config=release_macos-amd64-gl3 haumea  # Intel
```

### Windows

The Windows build uses GLFW and OpenGL 3. Install Visual Studio, Premake 5 and the GLFW 3.3.4
Win64 binary package. Then build librw and Haumea from PowerShell:

```powershell
$Haumea = 'C:\path\to\haumea'
$Librw = 'C:\path\to\librw'
$Glfw = 'C:\path\to\glfw-3.3.4.bin.WIN64'
$env:LIBRW = $Librw

Push-Location $Librw
git apply "$Haumea\patches\librw-haumea.patch"
python "$Haumea\tools\release\patch_librw_haumea.py" $Librw
premake5 vs2019 "--glfwdir64=$Glfw"
msbuild build\librw.sln /p:Configuration=Release /p:Platform=win-amd64-gl3 /t:librw /m
Pop-Location

Push-Location $Haumea
premake5 vs2019 "--glfwdir64=$Glfw"
msbuild build\librwgta.sln /p:Configuration=Release /p:Platform=win-amd64-gl3 '/t:librwgta;haumea' /m
Pop-Location
```

## Command line

```sh
haumea [--game /path/to/GTA\ San\ Andreas] [--workspace /path/to/server] [--open map.pwn]
haumea --game /path/to/GTA --pawn-roundtrip in.pwn out.pwn   # open, save and quit
```

`--game` and `--workspace` apply to that run only. `HAUMEA_GAME_DIR` is also read when no game
folder is saved yet.

## Tests

```sh
tools/haumea/tests/run_all.sh
```

This runs every test on your machine:

- the unit tests in `tools/haumea/tests`;
- the Pawn round trip: the editor opens and saves each map in `tests/pawn/maps`, and the output
  must match `tests/pawn/expected` and save again unchanged;
- a compile of every saved map as a gamemode, with no errors or warnings against the open.mp
  includes and, for maps without open.mp-only calls, the SA-MP 0.3.7 includes.

The compiler (open.mp's `pawncc`), the includes and Namaka's wheels are downloaded once, at fixed
versions. The game folder is `HAUMEA_GAME_DIR` or the one Haumea has saved. Without one, or with
`--fixture`, the tests use a synthetic game folder from `tests/fixture/make_game.py`: simple box
models with the game's model names and the data files the test maps need, and no game files at
all. A test that can't run says why. After an intended change to the exporter,
`run_all.sh --update-expected` updates `tests/pawn/expected`.

CI builds Linux, Windows and macOS for `main` and pull requests, and runs `run_all.sh --fixture`
on Linux under Xvfb.
