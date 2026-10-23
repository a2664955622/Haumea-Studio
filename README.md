English | [Português (Brasil)](README.pt-BR.md)

<p align="center"><img src="docs/images/banner.png" alt="Haumea, by itsneufox: map editor for open.mp and SA-MP" width="100%"></p>

# Haumea Studio

**The all-in-one editor for open.mp and SA-MP.** Build your server's world in GTA San Andreas:
objects, vehicles, NPCs, pickups, textdraws and more. Haumea Studio saves it all as a Pawn file
that your gamemode includes, checks it against the server's limits, and runs it on a test
server with one key.

![Haumea editing Grove Street: objects, a vehicle, actors, an NPC on its path, a player class, a 3D text label, a gang zone and the SA-MP tab](docs/images/editor.png)

New to Haumea Studio? Read the **[user guide](docs/USER_GUIDE.md)**.

## Features

**Build the map**

- **Objects**: find GTA and SA-MP objects by name, ID or category, then place, move and rotate them, with undo.
- **Vehicles**: set colours, a number plate, a paintjob and tuning parts, and see them on the car.
- **Server elements**: pickups, 3D text labels, actors, player classes, gang zones, map icons, sounds and checkpoints.
- **NPCs** (open.mp only): place bots that follow a path, walk the game's roads or play a recording.
- **Animations**: give actors and NPCs an animation and watch it in the editor.
- **Materials**: change an object's textures, colours and text, shown as players will see them.
- **Custom models** (0.3.DL): add your own models for SA-MP 0.3.DL and open.mp players.
- **TextDraws**: design textdraws on the game screen and save them in their own file.
- **Prefabs**: save a group of objects and place it again; a starter set is included.
- **Map tools**: copy objects in a line or a ring, mirror, align, scatter with a brush, and lay objects along a line.
- **Radar art**: turn your objects into gang zones that draw them on the radar (global, per-player or streamed; the last two are open.mp only).
- **Path nodes**: edit the roads that open.mp NPCs walk, saved to your server.
- **Interiors**: go to any GTA interior and copy the Pawn code that takes a player there.

**Check and test**

- **Map Check**: find what will break in game before players see it.
- **Player view**: see the map as a player sees it, with draw distances and streamer limits.
- **Test in game**: run the map on a private open.mp server with one key, then join it.

**Files**

- **Pawn maps**: save as native or streamer code that compiles with the open.mp and SA-MP 0.3.7 includes.
- **Other editors**: open Texture Studio maps, and merge MTA:SA and IPL maps into yours.
- **Autosave**: get back unsaved work after a crash.

**The editor**

- **Workspaces**: a start screen with your servers and their maps.
- **Your own keys**: change every shortcut.
- **Panels**: dock them around the 3D view or let them float.
- **Namaka**: let an AI assistant look at and edit your map.
- **Safe for your game**: Haumea Studio never changes your game's files.

<table>
  <tr>
    <td><img src="docs/images/samp-elements.png" alt="The SA-MP tab: actors, player classes, NPCs and path points under Players; pickups, labels, gang zones, map icons, sounds and checkpoints under World" width="360"></td>
    <td><img src="docs/images/textdraws.png" alt="The TextDraw designer with a HUD drawn over the 3D view" width="360"></td>
    <td><img src="docs/images/testserver.png" alt="Test in game: the map running on a private open.mp server, with its log" width="360"></td>
  </tr>
  <tr>
    <td align="center">Server elements on the SA-MP tab</td>
    <td align="center">The TextDraw designer</td>
    <td align="center">Test in game</td>
  </tr>
</table>

## Install

Download Haumea Studio from the [releases page](https://github.com/itsneufox/Haumea-Studio/releases).
You also need GTA San Andreas, and, to test maps, an open.mp or SA-MP server folder.

Download **one file** for your platform. Namaka is included in the Windows and Linux downloads;
there is no separate companion ZIP to install.

| Platform | Install (recommended) | Portable |
| --- | --- | --- |
| Windows 64-bit | `Haumea-Studio-Windows-Setup.exe` | `Haumea-Studio-Windows-Portable.zip` |
| Linux x86_64 | `Haumea-Studio-Linux-Setup.run` | `haumea-linux-amd64.tar.gz` |
| macOS Apple Silicon | | `Haumea-Studio-macOS-Apple-Silicon-Portable.zip` |

**Windows**: run Setup to add Haumea Studio to the Start menu. It installs for your account
and can be removed from Windows Settings. For portable use, extract the **entire** ZIP,
open the `Haumea Studio` folder and double-click `haumea.exe`. Keep its folders and DLLs beside it.

**Linux** (x86_64, glibc 2.38 or newer: Ubuntu 24.04, Debian 13, Fedora 39 or later):

```sh
sudo apt install libglfw3 curl python3 zenity  # Fedora: sudo dnf install glfw curl python3 zenity
                                            # Arch: sudo pacman -S glfw curl python zenity
sh Haumea-Studio-Linux-Setup.run
```

The installer needs Python 3.10+ and adds Haumea Studio to your applications menu. Run it
as your normal user. It installs in `$XDG_DATA_HOME/haumea-studio` (usually
`~/.local/share/haumea-studio`) and prints the uninstall command. For portable use, extract
`haumea-linux-amd64.tar.gz` into its own folder and run `./haumea` there.

**macOS**: install GLFW with `brew install glfw`, extract the ZIP and run `./haumea` from
the `Haumea Studio` folder in Terminal. This build is for Apple Silicon.

The Windows `.tar.gz` is for automatic updates; the symbols ZIP is for crash reports.
You do not need either to install. `SHA256SUMS` contains download checksums.

`kdialog` also works instead of `zenity`. Without either, type paths instead of browsing.
Your graphics driver must support OpenGL 3.3 (Mesa does).

**Test in game** on 64-bit Debian or Ubuntu also needs the 32-bit C library, once:

```sh
sudo dpkg --add-architecture i386 && sudo apt update
sudo apt install libc6:i386 libatomic1:i386
```

## Quick start

1. Start Haumea Studio and choose your GTA San Andreas folder and your server folder (the workspace).
2. Open the **Object Browser** (`Ctrl+1`), pick an object, click **Place** and click in the world.
3. Press `F5` to test the map on a private open.mp server.
4. Press `Ctrl+S` to save it as a Pawn file.

The [user guide](docs/USER_GUIDE.md) explains every feature. To build Haumea Studio or run its
tests, see [Building and testing](docs/BUILDING.md).

## Credits

- [JustMichael (ADRFranklin)](https://github.com/ADRFranklin): SA-MP model support.

## Lineage and license

Haumea Studio grew from euryopa, the map viewer in [librwgta](https://github.com/aap/librwgta), and is built on [librw](https://github.com/aap/librw), both by aap.

Licensed under the GNU GPL v2 or later; see [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

To report a bug, suggest a feature, translate or contribute code, see [CONTRIBUTING.md](CONTRIBUTING.md).

Haumea Discord: https://discord.gg/yTF8DWQqpP

open.mp Discord: https://discord.gg/samp
