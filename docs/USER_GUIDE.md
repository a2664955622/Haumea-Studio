# Haumea Studio user guide

Haumea Studio is the all-in-one editor for SA-MP and open.mp, built on GTA San Andreas.
You place objects, vehicles, NPCs and every other server element in the game world, design
textdraws, check the map against the server's limits and test it in game, and Haumea saves
it all as a Pawn file your gamemode includes.

The keys below are the defaults. Every one can be changed in **Preferences → Shortcuts**,
and **Help → Keyboard Shortcuts** (`F1`) lists them all.

![Haumea editing Grove Street](images/editor.png)

## Setup

Haumea needs two folders:

- **Game folder**: your GTA San Andreas install. Haumea only reads it and never writes to it.
- **Workspace**: the SA-MP or open.mp server you map for. Haumea takes maps, custom models,
  NPC files and the test server from it.

To install Haumea, see [Install](../README.md#install). The first start asks for both: type
each path or use **Browse...** (on Linux that needs `zenity` or `kdialog`). You can change them later in **File → Workspace** or in
**Preferences → Workspace**. Haumea keeps its own settings, autosaves and prefabs in its
config folder (`~/.config/haumea` on Linux, `%APPDATA%\Haumea` on Windows).

## Moving around

Hold the right mouse button in the 3D view to look around, and while it's held fly with
`W`/`A`/`S`/`D` (`Shift` faster, `Alt` slower). Middle-drag pans and the wheel zooms. The
look button can be changed in **Preferences → Camera**.

## Placing objects

1. Open the **Object Browser** (`Ctrl+1`) and pick an object. Search by name, ID, a list or
   a range (`1337, 19000-19100`). SA-MP objects are filed in categories.
2. Click **Place**, then click in the world. `Esc` stops placing.
3. Click an object to select it. Drag the gizmo to move it (`W`) or rotate it (`E`), or type
   exact values in **Object Info** (`Ctrl+2`). `G` drops it to the ground, `F` focuses the
   camera on it, and `Ctrl+D` duplicates it.

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

Each row shows how many the map has; click the count to select them all. Elements are saved
as native or streamer calls (`CreateActor`, `CreateDynamicPickup`, ...) and read back from
existing scripts.

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

## Custom models (SA-MP 0.3.DL and open.mp)

**File → Import → Custom Model** puts a DFF and TXD in the workspace's `models` folder and
`artconfig.txt`, so 0.3.DL and open.mp players download them. Custom models appear under
**Custom models** in the browser and keep their negative IDs in the saved map.

## TextDraws

Textdraws are saved in a file of their own, apart from the map. **Windows → TextDraws** opens
TextDraw mode: the world gives way to the 640x448 game screen, either a plain colour or a still
frame of the view you had, and map editing is off until you leave with **Exit** in the banner or
**Esc**. Add text, boxes, sprites and model previews, drag them on the screen (the corner resizes
them), and set their text, font, colours and style. Undo (Ctrl+Z) has its own history here.

The window's **New**, **Open**, **Save** and **Save As** work on the textdraw file (a `.pwn` or
`.inc`), and Ctrl+S saves it while the mode is on. Include it in your gamemode and call
`Haumea_<file>_CreateTextDraws()` from `OnGameModeInit` and `Haumea_<file>_ShowTextDraws(playerid)`
when a player should see them. **Test in game** includes the open textdraw file as it is.

A map saved by an earlier build may still hold textdraws. Opening it offers **Move textdraws to a
separate file** (`<map>_textdraws.pwn`); save the map afterwards to take them out of it. If you
keep them in the map, they show on the canvas but are read-only and are saved back unchanged.

![The TextDraw designer](images/textdraws.png)

## Map tools

**Tools** also has tools for layouts: **Array** (copies in a line or a ring), **Mirror**,
**Align & distribute**, **Drop & copy as Pawn** (drop to the ground, copy as attachments or
offsets), and **Along a line** (lay objects along a path you click). Exact moves and turns
are in Object Info's Delta mode.

**Radar art from objects** (in Tools) draws your objects on the radar as gang zones. It takes
every object, the selection or one group, rasterises their footprints from above into cells,
and merges cells of one colour into rectangles, one gang zone each, in the group "radar art".
Colour them with one colour, by group or from their textures, with an outline and lighter
higher surfaces. The counter shows how many zones it takes; **Fit to this many zones** picks
the cell size. **Shown as** decides how they reach players: **global zones** (at most 1024 on
the server), **per-player zones** (1024 for each player), or **streamed per-player zones**,
made only around each player as they move (streamer areas), for larger maps. The last two are
open.mp only. **Apply** makes them in one undo step.

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

A map is a Pawn file. **File → Save** (`Ctrl+S`) writes it, and **Save As** picks a new file or
the code style:

- **Native**: `CreateObject`, `CreateActor`, ... for any server.
- **Streamer**: `CreateDynamicObject`, ... with the streamer plugin, for big maps.

Moved or deleted GTA buildings become `RemoveBuildingForPlayer` calls. The first lines of the
saved file say which functions to call from your gamemode. **File → Open** reads `.pwn` maps
and Texture Studio `.db` maps; **File → Import → Merge Pawn File** also takes MTA:SA `.map` and
IPL files.

## The agent

Agent Alpha lets an AI assistant (Claude Code or another MCP host) look at and edit the open map
through a local socket. See [the agent setup guide](../tools/release/AGENT_README.md).

## Shortcuts

**Help → Keyboard Shortcuts** (`F1`) lists every command with its key, and
**Preferences → Shortcuts** rebinds them.
