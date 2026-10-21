# Haumea Studio with Namaka

This Linux x86_64 bundle contains Haumea Studio (the all-in-one editor for SA-MP
and open.mp), Namaka's Python wheel,
fonts, and Python dependency wheels for offline setup. It does not include GTA
files. Namaka's MCP server runs as a separate process and connects to the
editor over a private local socket. The editor needs the same system libraries
as the plain Linux release (GLFW, and `zenity` or `kdialog` for file dialogs):
see Install in Haumea's README.

## Install Namaka

Unzip the bundle anywhere you like and run its setup script. It needs Python
3.10 or newer with `venv`; on Debian or Ubuntu that is two packages:

```sh
sudo apt install python3 python3-venv   # Debian/Ubuntu; Fedora and Arch include venv
cd Haumea-Namaka-linux-x86_64
./setup-agent.sh
```

The setup script creates `.venv-agent` beside the bundle and installs the MCP
adapter and CLI. With Python 3.14 (Ubuntu 26.04, Fedora, Arch) it installs
fully offline from the wheelhouse; Python 3.10–3.13 (Ubuntu 24.04, Debian)
downloads only the few compiled parts the wheelhouse has for 3.14 alone, so
the first setup needs the internet, and says so if it can't reach it. The
Namaka CLI is `.venv-agent/bin/haumeactl`; the MCP server command is
`.venv-agent/bin/haumea-mcp`. If setup stops halfway, fix what it says and run
it again.

## Start Haumea

Haumea runs from its own folder and remembers the GTA San Andreas folder and
the workspace (your server folder) it asked for on its first start. Start the
editor with the agent socket from a terminal:

```sh
"/path/to/this-bundle/launch-haumea-agent.sh"
```

The launcher, `haumeactl` and `haumea-mcp` all default to
`~/.cache/haumea-agent.sock`. To use another socket, set `HAUMEA_ENGINE_SOCKET`
to the same path for the launcher and for the MCP host.
`HAUMEA_GAME_DIR` and `HAUMEA_WORKSPACE` choose another game folder or
workspace for that run, and `HAUMEA_EXECUTABLE` points at an editor stored
somewhere else.

On the first start Haumea asks for the GTA San Andreas folder and the
workspace. Check that the agent can reach the editor:

```sh
.venv-agent/bin/haumeactl ping
```

## Connect an MCP host

Point the host at the absolute path of the bundled environment's `haumea-mcp`
command. For Claude Code, an example configuration is:

```json
{
  "mcpServers": {
    "haumea": {
      "command": "/path/to/this-bundle/.venv-agent/bin/haumea-mcp",
      "args": [],
      "env": {
        "HAUMEA_ENGINE_SOCKET": "/home/USER/.cache/haumea-agent.sock"
      }
    }
  }
}
```

Replace the socket value with the exact path used to launch Haumea. To add the
server from a terminal, first remove an old entry with the same name, then run:

```sh
claude mcp remove haumea --scope user
claude mcp add --scope user haumea \
  -e HAUMEA_ENGINE_SOCKET="$HOME/.cache/haumea-agent.sock" -- \
  "/path/to/this-bundle/.venv-agent/bin/haumea-mcp"
```

Restart Claude Code; `/mcp` lists `haumea` as connected while the editor runs.

The CLI can index the GTA installation for local asset search:

```sh
.venv-agent/bin/haumeactl assets index --gta-dir "/path/to/GTA San Andreas"
```

## Editing a map with an agent

The agent edits the map open in Haumea: the Pawn file (`.pwn`) the editor saves
with `Ctrl+S`. Everything the map holds is an element the agent can list,
place, move, change and delete: objects (with materials and material text),
vehicles, pickups, 3D text labels, actors, player classes, gang zones, map
icons, open.mp NPCs (skin, weapon, movement along a path, a `.rec` recording or
the game's path nodes, an animation) and NPC path points. Deleting a GTA world
building exports `RemoveBuildingForPlayer`.

A typical session through the MCP tools:

1. `haumea_status` shows the open map, whether it has unsaved changes, the
   edit session and the camera target, a good place to build. `open_map`
   opens a `.pwn`/`.inc`, Texture Studio `.db`, MTA:SA `.map` or `.ipl` map,
   and `new_map` starts an empty one. Both refuse to drop unsaved changes
   unless `discard_changes` is set. `merge_map` adds another map's contents to
   the open one.
2. Edit. The first edit starts an edit session by itself.
   - `place_element` places one element and `place_elements` several. Their
     settings go in `properties` in the same call; the tool descriptions list
     each kind's properties, with examples.
   - `set_element_properties` changes them later.
   - `move_element` moves or turns an element; what you leave out stays as
     it is. `delete_element` deletes.
   - `set_object_material` and `set_object_material_text` retexture objects.
   - `apply_scene_patch` places larger compositions under stable keys.
   - `map_tool` runs the Tools window's transform tools on a set of ids: array,
     mirror, align, distribute, snap rotations, move, rotate, drop to ground,
     relative offsets as Pawn, and objects laid along a line.
3. Check: `map_check` lists what breaks in game, with the elements concerned,
   and `capture_view` takes a screenshot, choosing a clear view of a target
   when no camera position is given. `list_scene_objects` lists the map
   (`include_world` adds the GTA buildings around a point), and
   `validate_scene` checks placement. `undo_edit` steps back through the
   editor's undo history, which the user's `Ctrl+Z` shares.
   `test_in_game("run")` runs the map on a private open.mp server; poll
   `test_in_game("status")` for compiler errors and the server log.
4. `save_map` writes the map like `Ctrl+S` and keeps the session's edits. A
   path works like **Save As** and becomes the map's path, and `format`
   switches between native `CreateObject` and streamer `CreateDynamicObject`
   output. `finish_edit_session(commit=False)` instead puts the map back as it
   was before the edits.

## Example sessions

These are runs against a live editor, with the calls an agent made.

**"A small gas station with a tuned car, an attendant actor and a looping radio
sound"** (7 calls):

1. `new_map(discard_changes=true)`, then `haumea_status` for the camera target.
2. `search_assets("petrol")` finds `petrolpump` (1244), 1.1 x 1.2 x 1.7 m.
3. `place_elements` with two pumps, a Sultan
   (`properties: {"color1": 1, "mods": [1010, 1080, 1100]}`), a mechanic actor
   (`{"anim_lib": "PED", "anim_name": "IDLE_CHAT", "anim_loop": true}`) and a
   stream sound (`{"sound_kind": "stream", "url": "http://..."}`). Part 1100
   doesn't fit a Sultan: nothing is placed, and the error lists the parts that
   fit, by slot.
4. The same call with 1029 (an exhaust) instead places all five.
5. `capture_view(target=[x, y, z])` shows the station from a clear angle; the
   car turned out to sit on a lamp post, which snap counts as ground.
6. `save_map(path=...)` commits the edits and writes the map.

**"A patrol NPC that walks around a parking lot and a race of 4 checkpoints out
of it"** (5 calls):

1. `new_map`.
2. One `place_elements`: four path points with `{"path": "lot_patrol"}`, an NPC
   with `{"npc_name": "LotGuard", "path": "lot_patrol", "move_type": 1}`, and
   four checkpoints with `{"race": "exit_race", "order": 0..3}`, the last one
   with `"race_type": 1` (finish).
3. `map_check` warns that native code leaves checkpoints out.
4. `capture_view(target=...)`.
5. `save_map(path=..., format="streamer")`.

**"Convert this MTA map and fix what Map check complains about"** (5 calls):

1. `open_map("race_lobby.map")` converts it; the import report lists what the
   MTA map had that SA-MP can't show (`left_out`).
2. `map_check` reports an object outside the world bounds, naming it with its
   position (a `gunbox` at x 25000, a typo for 2500), and a model the editor
   doesn't know, which was left out.
3. `move_element(id, x=2500)`.
4. `map_check` again: only the unknown model is left, to report to the user.
5. `save_map()` writes `race_lobby.pwn` beside the `.map`.

The user can keep working in the editor during a session. The window title
shows unsaved changes, and the map saved by the agent is the one the editor
reopens.

## Saving a map

Haumea saves maps as Pawn/open.mp source. Press `Ctrl+S` or use **File → Save
Pawn Map** to write the map's file. **File → Save Pawn Map As...** chooses
another path and native or streamer output. The agent's `save_map` does the
same.

The bundle is a companion distribution. Keep `haumea`, `fonts`, the wheel,
`wheelhouse`, and setup files together; the game's models and textures stay in
the user's GTA installation.
