English | [Português (Brasil)](USER_GUIDE.pt-BR.md)

# Haumea Studio user guide

Haumea Studio is the all-in-one editor for open.mp and SA-MP, built on GTA San Andreas.
You place objects, vehicles, NPCs and every other server element in the game world, design
textdraws, check the map against the server's limits and test it in game, and Haumea saves
the map and textdraws as Pawn files your gamemode includes.

The keys below are the defaults. Every one can be changed in **Preferences → Shortcuts**,
and **Help → Keyboard Shortcuts** (`F1`) lists them all.

![Haumea editing Grove Street](images/editor.png)

## Setup

Haumea needs two folders:

- **Game folder**: your GTA San Andreas install. Haumea reads the game's files and never changes them.
- **Workspace**: the SA-MP or open.mp server you map for. Haumea takes maps, custom models,
  NPC files and the test server from it.

To install Haumea, see [Install](../README.md#install). Haumea opens on the **start screen**:

- **Workspaces** you used, newest first, with when you last opened them. **Pin** (the star) keeps
  one at the top; the folder button opens it in your file manager, and × takes it off the list (the
  folder stays). **New workspace** makes a server folder with `gamemodes`, `filterscripts` and
  `scriptfiles`; **Open folder as workspace** uses one you already have.
- **Maps** in the selected workspace that you opened before, with a picture of each. Double-click
  one, or select it and press **Open map**. **Open workspace** opens it with a new map.
- **GTA San Andreas**: the game folder, with the SA-MP client version found there and the
  workspace's 0.3.DL custom models. A new game folder loads after a restart.
- **Continue without a workspace**, or **Skip** to keep what is open.

The first start asks for the game folder here before anything loads. Type each path or use
**Browse...** (on Linux that needs `zenity` or `kdialog`). Tick **Open the last workspace
automatically** to skip the screen; **File → Switch Workspace** shows it again. Both folders can
also be changed in **File → Workspace** or **Preferences → Workspace**. The window title shows
the workspace.

If required GTA files are missing or unreadable, the start screen lists them and keeps the
folder chooser open. Restore the files or choose another complete installation to continue.

Haumea keeps its own settings, autosaves, recent maps, favourites and prefabs in its config
folder: `~/.config/haumea` on Linux, `%APPDATA%\Haumea` on Windows, `~/Library/Application
Support/Haumea` on macOS. The first start copies an older install's `<GTA>/haumea_data` there
and leaves the old folder as it was. A Haumea inside the GTA folder, or started from it, finds
the game folder without asking. For command-line options, see
[Building and testing](BUILDING.md#command-line).

## Map, TextDraws and Code

Choose **Map**, **TextDraws** or **Code** in the top bar to switch editors. Your current map,
textdraw file and code document stay open while you switch. **Windows** contains the panels
and utilities for the editor.

The yellow **New!** balloons introduce TextDraws and Code. Opening an editor or dismissing
its balloon clears that hint, including after a restart.

## Moving around

Hold the right mouse button in the 3D view to look around, and while it's held fly with
`W`/`A`/`S`/`D` (`Shift` faster, `Alt` slower). The cursor hides while you look and comes
back where it was. Middle-drag pans and the wheel zooms. The look button can be changed in
**Preferences → Camera**.

How far you see is set in the **View** window (**Windows → View**): **World draw distance**
for the game's buildings, **Map draw distance** for your map's own objects (they show that
far whatever their model's draw distance), and **Fog** (Game is as thick as in game, Light
only far out). **Focus my map** dims the game's buildings. Player view (`P`) always uses the
game's distances.

## Placing objects

1. Open the **Object Browser** (`Ctrl+1`) and pick an object. Search by name, ID, a list or
   a range (`1337, 19000-19100`). Every model, the game's and SA-MP's, is filed in a
   category by what the game's own files say about it (its name, IDE file, collision
   size), and **Favourites** keeps the ones you use often.
2. Click **Place**, then click in the world. `Esc` stops placing.
3. Click an object to select it. Drag the gizmo to move it (`W`) or rotate it (`E`), or type
   exact values in **Object Info** (`Ctrl+2`). `G` drops it to the ground, `F` focuses the
   camera on it, and `Ctrl+D` duplicates it.

**Edit → Go to Coordinates** (`Ctrl+J`) points the camera at a position you paste: `x, y, z`,
`x y z` with an angle after it, or a whole `SetPlayerPos(playerid, x, y, z);` line. It only
moves the view, so there's nothing to undo. **Edit → Copy Camera Position** copies where the
camera is as `x, y, z`.

Right-click an object in the world or browser and choose **Find instances**. The Object Browser
lists counts by model and locations; enter several IDs or ranges and choose **Both**, **My map**
or **GTA world**. **Jump to**, **Previous** and **Next** move the camera, including across interiors.
Search results are yellow and leave your editing selection unchanged. Closing the browser or
returning to models removes the search highlights.

**Similar models** uses collision-box dimensions or proportions with an adjustable tolerance.
Proportions ignore overall size; rotating a footprint by 90 degrees still matches. Use the existing
model previews to compare actual shapes. Models without collision bounds cannot be compared.

The toolbar switches step snapping (the magnet), object snap (`N`) and ground following on
and off. **Tools** holds their settings, plus the brush for scattering objects.

Object Info lays out everything the same way: the name and Pawn variable, the object's
own settings, then position. World & streaming, attachment, materials (textures, colours
and text) and group & note follow, closed until you need them. Select several objects to
change a field on all of them at once.

## SA-MP elements

The browser's **SA-MP** tab places everything else a server map needs:

| Players | World |
| --- | --- |
| Actors, player classes (spawn points) | Pickups, 3D text labels, gang zones |
| NPCs and NPC path points (open.mp only) | Map icons, sound places, checkpoints (streamer only) |

Players never see 3D text labels, gang zones, map icons, sound places and path points as
objects, so the editor shows each as a small coloured icon (a T, a speaker, a pin, a square,
a node) that keeps its size on screen; click it to select it.

Each row shows how many the map has; click the count to select them all. Elements are saved
as native or streamer calls (`CreateActor`, `CreateDynamicPickup`, ...) and read back from
existing scripts. Gang zones and native map icons are shown to a player by the map's
`_ShowForPlayer(playerid)` function.

A **sound place** plays an audio stream (a URL) or a game sound (picked from a list) to players
who come within its radius.

<img src="images/samp-elements.png" alt="The SA-MP tab" width="360">

## NPCs (open.mp only)

open.mp NPCs are bot players. Place one from the SA-MP tab, then set its name, skin, weapon,
health and armour in Object Info. Under **Movement** pick what it does:

- **Follow a path**: place NPC path points first, clicking one after another, then pick the
  path. Saved as `NPC_MoveByPath`.
- **Walk the nodes**: walk the game's path nodes from a start point (`NPC_PlayNode`). Edit the
  node files in **Windows → Nodes** (`F3`); Haumea saves them to the server's
  `scriptfiles/NPCs/nodes` and never changes the game's own files.
- **Play a recording**: play a `.rec` file from `npcmodes/recordings` (`NPC_StartPlayback`).

**Preview movement** walks the NPC in the viewport. Actors and NPCs can also play an
animation (library, name, speed, loop, locks, freeze and time), and you see it on the skin.

<img src="images/npc-object-info.png" alt="Object Info for an NPC that follows a path" width="360">

## Vehicles

Vehicles are in the browser like objects. In Object Info set static (`AddStaticVehicleEx`) or
dynamic creation, the respawn delay, the two colours (-1 is random), a number plate, a
paintjob and tuning parts. Only parts that fit the model are offered, one per slot.

**Trains** (Freight 537, Brown Streak 538, Tram 449) go on the track as SA-MP puts them: placing
one snaps it to the nearest track and dragging slides it along (hold `Alt` to move it freely),
with the tracks drawn faintly while a train is selected or being placed. They come from your game's
`data/Paths/tracks.dat` and `tracks3.dat` (SA-MP doesn't use `tracks2.dat` and `tracks4.dat`).
The Freight pulls three flat freights and the Brown Streak three coaches, drawn behind it as in
game; the Tram runs alone. Only the engine is written to the map, since the game adds the
carriages, and each train takes four vehicle ids. Its angle only says which way it runs: over
180 one way, up to 180 the other. Trains need **Static vehicle**, because open.mp's `CreateVehicle`
refuses them, and Map check says so, and flags a train placed more than 3 m off a track or a
carriage on its own.

## Materials

Under **Materials** in Object Info, each material slot of an object can get another texture, a
colour or text. **Browse textures...** shows the textures to choose from, and **Windows →
Texture Browser** has every texture, Texture Studio's texture list too. The 3D view
shows them as the SA-MP 0.3.7 client does: text uses the real font files, including
`{RRGGBB}` colours, and recoloured materials lose their shading as they do in game.

Fonts are looked up by name in this order: your fonts folder, the system's fonts (and the
Wine prefix's `windows/Fonts`), the GTA folder, then the fonts Haumea ships. Haumea ships
SA-MP's own fonts (GTAWEAPON3 for the weapon and skull icons, SampAux3) and puts them in your
fonts folder at start: `%APPDATA%\haumea\fonts` on Windows, `~/.local/share/haumea/fonts`
on Linux, `~/Library/Application Support/Haumea/fonts` on macOS. **Open fonts folder** (next to
the font in Object Info, and in Preferences) opens it: drop in any `.ttf` and press **Rescan
fonts**. Without Arial, Times New Roman or Courier New, the preview uses Liberation Sans, Serif
or Mono, which have the same letter widths. Object Info shows which font file the preview uses,
and Map Check notes a font that isn't installed at all.

## Custom models (SA-MP 0.3.DL and open.mp)

**File → Import → Custom Model** puts a DFF and TXD in the workspace's `models` folder and
`artconfig.txt`, so 0.3.DL and open.mp players download them. Custom models appear under
**Custom models** in the browser and keep their negative IDs in the saved map.

## TextDraws

Textdraws are saved in a file of their own, apart from the map. Select **TextDraws** in the
top bar: the world gives way to the 640x448 game screen, either a plain colour or a still
frame of the view you had. Select **Map** to return to the world; **Esc** also leaves this editor
when no textdraw is selected. Add text, boxes, sprites and model previews, drag them on the screen (the corner resizes
them), and set their text, font, colours and style. Undo (Ctrl+Z) has its own history here.

The window's **New**, **Open**, **Save** and **Save As** work on the textdraw file (a `.pwn` or
`.inc`), and Ctrl+S saves it while the mode is on. Include it in your gamemode and call
`Haumea_<file>_CreateTextDraws()` from `OnGameModeInit` and `Haumea_<file>_ShowTextDraws(playerid)`
when a player should see them. **Test in game** includes the open textdraw file as it is.

A map saved by an earlier build may still hold textdraws. Opening it offers **Move textdraws to a
separate file** (`<map>_textdraws.pwn`); save the map afterwards to take them out of it. If you
keep them in the map, they show on the canvas but are read-only and are saved back unchanged.

![The TextDraw designer](images/textdraws.png)

### TextDraw colour tokens

The canvas follows the game's rules for the tokens in a textdraw's text:

- `~r~` red, `~g~` green, `~b~` blue, `~y~` yellow, `~p~` purple, `~w~` and `~s~` light grey,
  `~l~` black (lower case only). Only the first letter counts: `~red~` is `~r~`.
- `~h~` makes the current colour 1.5 times brighter, and stacks: `~h~~h~` is brighter still.
- `~n~` starts a new line. The new line starts in the textdraw's own colour (TextDrawColor)
  plus the **last** colour token before it, and nothing else. Lines wrapped at the text size
  work the same way.
  - `~y~test~n~test`: both lines are yellow.
  - `test~n~test` (no token): both lines are TextDrawColor.
  - `~w~~h~test~n~test`: the first line is bright white, the second is TextDrawColor brightened
    once (`~h~` was the last token), not white.
  - `~r~~h~test~n~test`: the second line is also TextDrawColor brightened, not light red.
  - `test~n~~w~test`: a token after `~n~` applies as usual.
- `~k~~PED_FIREWEAPON~` shows a key name in game; Haumea leaves it out.

## Prefabs

A prefab is a group of objects saved to place again. Select the objects and use **File → Export
→ Selection as Prefab** (`Ctrl+Shift+E`). Give it a name and tags, and save it to **Mine** (your
config folder) or to the workspace.

The Object Browser's **Prefabs** tab lists yours, the workspace's and a **Starter** set. Search
by name or tag, pick one and click **Place**; `Q`, `E` or the mouse wheel turn it before you
click. **As a group**
puts its objects in a group named after the prefab.

## Map tools

**Tools** also has tools for layouts: **Array** (copies in a line or a ring), **Mirror**,
**Align & distribute**, **Drop & copy as Pawn** (drop to the ground, copy as attachments or
offsets), and **Along a line** (lay objects along a path you click). Exact moves and turns
are in Object Info's Delta mode.

**Coordinate pin** places a reference pin without adding a map object. Click **Place pin**, then a
surface, or use **Pin at selected centre**. Drag from the pin marker to an object to measure the total
distance and signed X/Y/Z differences. **Pick endpoint** also works without dragging. Surface snapping
includes selected objects; empty space uses a horizontal plane at the pin's height.

Edit **Pin XYZ**, copy the coordinates, align individual axes with the endpoint, or move by an exact
**Surface offset** along the last picked surface normal (negative values use the other side). To
centre a doorway embedded in a building, pick one edge, pick the other as the endpoint and use
**Midpoint**. A selected object's centre comes from its collision box. The pin and ruler are temporary
editor references; they are hidden when the section, Tools or Map workspace closes and are not exported.

**Radar art from objects** (in Tools) draws your objects on the radar with gang zones. It uses
every object, the selection or one group, seen from above, and puts the zones in the group
"radar art".

- **Colour**: one colour, by group, or from the objects' textures. An outline and lighter high
  parts are optional.
- **Detail**: the counter shows how many zones it needs. **Fit to this many zones** sets the
  detail to match a number you choose.
- **Shown as**: **global zones** (at most 1024 on the server), **per-player zones** (1024 for
  each player), or **streamed per-player zones** (made near each player as they move, for big
  maps). The last two are open.mp only.

**Apply** makes the zones, and one undo removes them all.

## Interiors

**Windows → Interiors** lists GTA's interiors. Click one to go there and show it. Right-click one
to copy the Pawn code that takes a player there (`SetPlayerInterior` and `SetPlayerPos`).

## Checking the map

- **Map Check** (`F6`) lists what will break in game: limits, unknown models, elements outside
  the world, bad colour codes and more. Click an item to select what it is about.
- **Player view** (`P`, or the checkbox in **Windows → View**) draws the map as a player at the camera would
  see it: draw and stream distances, the streamer's limits, the virtual world and the interior.

## Test in game

**Test in game** (`F5`) runs the map on a private open.mp server in Haumea's config folder.
Your map file and your own server are not touched. **Launch game** joins it, and **Reload map**
picks up your changes. Pick the server package and port in **Preferences → Test server**.
On 64-bit Linux, open.mp's server and compiler need the 32-bit C library: see
[Install](../README.md#install).

![Test in game with the server log](images/testserver.png)

## Saving

In **Map**, a map is a Pawn file. **File → Save** (`Ctrl+S`) writes it, and **Save As** picks a new file or
the code style:

- **Native**: `CreateObject`, `CreateActor`, ... for any server.
- **Streamer**: `CreateDynamicObject`, ... with the streamer plugin, for big maps.

A map that uses the streamer stays in streamer code when you open and save it. Moved or deleted
GTA buildings become `RemoveBuildingForPlayer` calls. The first lines of the saved file say which
functions to call from your gamemode. The code compiles without warnings with both the open.mp
and the SA-MP 0.3.7 includes.

- **Open**: **File → Open Map** (`Ctrl+O`, or drop a `.pwn` on the window) reads `.pwn` maps and
  Texture Studio `.db` maps (from `scriptfiles/tstudio/SavedMaps`). Saving a Texture Studio map
  writes a new `.pwn` and never changes the `.db`. Texture Studio stores textures by number in its
  texture list; when your server has Texture Studio installed, Haumea uses that server's list.
- **Merge**: **File → Import → Merge Pawn File** adds another `.pwn`, `.db`, MTA:SA `.map` or IPL
  map to yours.
- **Groups and notes** are saved after each object as `// model @group("...") @note("...")`.
  Keep these comments when you edit the file by hand.
- **Autosave**: while a map has unsaved changes, Haumea saves copies in its config folder.
  **File → Recover Autosave** opens them.

## Code editor

Select **Code** in the top bar (`Ctrl+9`) to edit Pawn with syntax highlighting, line numbers,
undo/redo and **Find / Replace** (`Ctrl+F`). Open an existing `.pwn` or `.inc` with
**Open script...**, or start from the generated map code. **Native** and **Streamer** choose
the format used when generating code from the map.

While the generated document is untouched, it follows changes to the map. Once you edit it,
your edits stay in the document. **Refresh from map** generates a fresh copy and asks before
replacing unsaved edits.

**Save script** (`Ctrl+S`) saves the document as a separate script; the default name for generated
code ends in `.script.pwn`. The editor protects the map's own `.pwn`, which is regenerated when
you save the visual map. Code edits do not change the 3D scene. Choose **Windows-1252** or **UTF-8**
for the script's encoding; use UTF-8 for characters Windows-1252 cannot represent.

Unsaved code is kept as a recoverable draft in Haumea's config folder. It reappears when you
open **Code** after restarting.

## Namaka (AI assistant)

Namaka, named after Haumea's inner moon, lets an AI assistant (Claude Code, Codex, Claude Desktop
or any other MCP client) look at and edit the open map through a local socket. It used to be called
Agent Alpha.

**Windows → Namaka** installs it. It needs Python 3.10 or newer; the panel says which Python it
found and what to install if there is none.

- With **Python 3.14**, **Install** works offline from the files that come with Haumea.
- With 3.10-3.13 it downloads only the few parts Haumea bundles for 3.14 alone.

Installing runs in the background: the panel shows progress, **Cancel** stops it, and **Open the
log** shows what happened. Namaka goes in the config folder (`~/.config/haumea/namaka` on Linux,
`%APPDATA%\Haumea\namaka` on Windows), so updating Haumea doesn't move it.

- **Repair** installs it again; the old install stays until the new one works.
- **Update** appears when Haumea comes with a newer Namaka, and Haumea also updates it after an
  editor update.
- **Uninstall** removes it.

The copy buttons put this install's MCP setup on the clipboard:

- **Copy for Claude Code**: a `claude mcp add` command.
- **Copy for Codex**: a `codex mcp add` command (Codex's CLI, IDE extension and desktop app share
  `~/.codex/config.toml`).
- **Copy for Claude Desktop**: an entry for `claude_desktop_config.json`.
- **Copy MCP JSON**: the same `mcpServers` entry, for any other MCP client configured with JSON.

Once Namaka is installed, Haumea listens for it whenever it starts: on Linux and macOS through
`~/.cache/haumea-agent.sock`, which only you can use, and on Windows on a local port with a new key
each time, written to `namaka\connection.json` in the config folder. **Let Namaka connect**
(**Preferences → Namaka**, or the panel) turns this off from the next start. Only one Haumea at a
time can be reached: if another one already is, the panel says so. Starting Haumea with
`--agent-socket` still uses that socket instead.
The assistant usually looks at the map without moving your view. When you ask it to show you
something or take you somewhere, it flies the camera there (`move_camera`) and tells you;
`camera_back` returns to where you were. It never moves the camera while you're dragging the
gizmo, placing or looking around with the right mouse button.
An Agent Alpha install keeps working; once Namaka is set up, point your MCP client at it and
delete the old folder. More in [the setup guide](../tools/release/AGENT_README.md).

## Updates

When a new version is out, a banner in the top right says so: **What's new** shows its
notes, **Update now** downloads it in the background, and it installs the next time Haumea
starts (or right away with **Restart now**, once your changes are saved). Haumea checks the
download against the release's checksums and replaces only its own files; your maps,
settings and anything else in its folder are left alone, and a Haumea file you changed is
kept as `<file>.local`. **Help → Roll back** puts the previous version back.
**Preferences → Updates** turns the check or automatic downloads on or off and picks the
channel; **Help → Check for updates** checks right away.

## Privacy

Haumea sends nothing unless you choose to, and never your maps. It can send two things: a daily
usage ping and crash reports.

The **daily usage ping** goes only if you say yes: the start screen asks once (closing it
without answering means no), and **Preferences → Privacy** turns it on or off at any time.

When it is on, Haumea sends at most once a day, to `telemetry.itsneufox.xyz`:

- a random install ID (32 hexadecimal characters made on your computer; it says nothing
  about you or your computer),
- the version (for example 1.0.0.57) and channel (production or beta, or dev/pr for test
  builds),
- the operating system: Linux, Windows or macOS.

This only counts how many installs are in use, on which versions and systems. It is kept until
12 months after your install last sent one. Turning the ping off deletes the install ID; turning
it on again makes a new one.

**Crash and error reports**: when Haumea crashes or hits an error, it keeps a report, and at the
next start a banner asks: **View report** shows exactly what would be sent, then **Send**,
**Don't send** or **Always send**. **Preferences → Privacy** sets this to Ask (the default),
Always send or Never send. A report has:

- the version, commit, channel, operating system and its version, the processor type, and the
  graphics card and driver names,
- where it happened in Haumea's code (the stack, as offsets in its own files), and the error
  message if there was one,
- the last lines of the editor's log, and which mode and window were in use.

Before it is kept, your home folder, user name, game folder and workspace are replaced by
placeholders, and map names are left out; no map contents are ever included. A report carries the
install ID only if the daily ping is on too. Reports are sent to `reports.itsneufox.xyz` and kept
for 6 months; unsent ones are deleted after 30 days. New kinds of crashes are summed up in a
private Discord channel of the developer's (the version, system, graphics card and where in the
code it happened; never log lines or install IDs).

The server keeps no IP addresses or access logs, and only the developer sees what it gets.
[What is sent and kept](https://telemetry.itsneufox.xyz/privacy) has the details.

## Shortcuts

**Help → Keyboard Shortcuts** (`F1`) lists every command with its key, and
**Preferences → Shortcuts** rebinds them.

## Panels

Panels dock around the 3D view. You can also float them, and they snap to the screen edges and
to each other. **Windows → Reset Panel Layout** puts them back.
