# Changelog

## 1.41.0: Haumea Studio

Everything since 1.40.11. Haumea is now **Haumea Studio, the all-in-one editor
for open.mp and SA-MP**: it edits whole open.mp and SA-MP maps, not just
objects: NPCs and their routes, vehicles with their
tuning, pickups, labels, sounds, checkpoints and textdraws, all saved as Pawn
that compiles cleanly. It runs from its own folder, tests your map on a real
server with one key, and tells you what will break before you find out in
game.

### New

**open.mp NPCs**
- Place NPCs with a name, skin, weapon, health and armour, and pick how they
  move: along a path you draw from path points (`NPC_MoveByPath`), on the
  game's path nodes (`NPC_PlayNode`), or by playing a `.rec` recording
  (`NPC_StartPlayback`). Paths can loop.
- **Preview movement** walks them in the viewport at open.mp's speeds, and
  recordings play back on a timeline with their route drawn.
- Put an NPC in a vehicle, as driver or passenger; driver recordings move the
  car.
- Actors and NPCs can play an animation, and ped skins animate in the
  viewport.
- **Nodes** window (F3): add, link, move and delete the path nodes NPCs walk
  (`NODES*.DAT`) and save them to the server's `scriptfiles/NPCs/nodes`. The
  game's own node files are never changed.

**More map elements**
- Vehicles get SA-MP's 256 colours, a number plate, a paintjob and one tuning
  part per slot (only parts that fit), shown on the model.
- Sound places play an audio stream or a game sound to players nearby.
- Checkpoints and race checkpoints (streamer maps), with race order.
- **TextDraw designer**: draw global and per-player textdraws on the 640x448
  canvas, with the game's fonts, sprites and model previews. Textdraws have a
  `.pwn`/`.inc` file of their own (New, Open, Save, recent files in the
  TextDraws window), which Test in game includes; the map holds only the
  world. **TextDraw mode** swaps the world for a plain canvas or a still frame
  and turns map editing off, with its own undo; Esc leaves it. Maps from
  earlier builds keep their textdraws read-only until you move them to a file.
- SA-MP 0.3.DL and open.mp **custom models** from the server's
  `models/artconfig.txt`, under Custom models in the browser.
- Open and merge **Texture Studio** maps (`.db`), **MTA:SA** `.map` files and
  IPL text maps.

**Testing your map**
- **Test in game** (F5): runs the map as it is on a private open.mp server
  and shows its log. Your map file and your own server aren't touched.
- **Map Check** (F6): what will break or look wrong in game, before you save:
  limits (objects, vehicles, pickups, labels, actors, gang zones, map icons,
  textdraws, NPC slots), unknown models, positions outside the world,
  material and colour-code mistakes, invalid NPC names, missing recordings,
  paths with one point, duplicate variable names, and what the server needs.
  Click an item to select what it's about. It also runs, quietly, on save and
  Test in game.
- **Player view** (P): see the map the way a player would from the camera,
  with draw and stream distances, the 1000-object limit, and only the current
  virtual world and interior.

**Editing**
- **Transform tools** in the Tools window: array (linear or around a centre),
  mirror, align and distribute, exact moves and turns, and objects laid along
  a line, each one undo step.
- **Radar art from objects**: gang zones drawn from the objects' footprints,
  coloured from their textures, by group or in one colour, with a zone
  counter and "fit to N". Gang zones can be global, per-player or streamed
  around each player (open.mp only).
- **Hide and isolate** for busy areas: H hides the selection, Shift+H hides
  everything else, Alt+H shows it all. Only the editor hides them; the map
  keeps them.
- **Map Objects** is an outliner for big maps: groups, search, rename (F2),
  group the selection (Ctrl+G), and an eye to hide each object.
- **Texture Browser** (Ctrl+T) searches every texture.
- **Interiors** panel with the game's interiors and their spawn points.
- Duplicate (Ctrl+D) and focus the camera on the selection (F).
- Every keyboard shortcut can be rebound in **Preferences → Shortcuts**, and
  every mouse-wheel action has keys: Ctrl+= / Ctrl+- / Ctrl+0 for zoom (and
  numpad + / -), Ctrl+Shift+= / - for fly speed, Ctrl+wheel or
  Ctrl+Alt+= / - for the interface size, PgUp / PgDn to scroll a panel.

**Namaka** (formerly Agent Alpha), the AI assistant companion, installs from
**Windows → Namaka**: offline with Python 3.14, or downloading only a few
parts on 3.10-3.13. It has Repair, Update, Uninstall, a log, and copies the
MCP setup for Claude Code or Claude Desktop. Its install sits in the config
folder, so editor updates don't move it. Once it is installed, Haumea listens for it on
every start (Preferences > Namaka > "Let Namaka connect" turns that off), and
a second Haumea never takes over the first one's connection.

**The agent** (Namaka) can edit the open map and every element kind,
run Map Check and Test in game, and grab frames with the panels.

### Changed

- **W/A/S/D fly the camera only while the right mouse button is held**, as in
  Unity and Unreal: hold it to look around and fly, with Shift (fast) and Alt
  (slow). On their own the keys are shortcuts again, so `W`, `E` and `D` never
  move the view. It works with the mouse on the gizmo too, and a hold spent
  flying doesn't open the right-click menu. (The button is the look button in
  **Preferences → Camera**.)
- **The name is Haumea Studio**, the all-in-one editor for open.mp and SA-MP.
  Only the name you see changes: the program is still `haumea` (`haumea.exe`),
  and settings stay in the same folders.
- **Version numbers as open.mp has them**: 1.41.0 plus a build number, e.g.
  1.41.0.57 (the commit count), with the commit next to it in About, the
  title bar and `haumea --version`. Releases are tagged
  `v1.41.0.57-<channel>`, and the updater compares all four numbers.
- **Haumea runs from its own folder.** The first start asks for the **game
  folder** (GTA San Andreas, only ever read) and an optional **workspace**
  (the server you map for). Haumea never writes into the game folder. The old
  layout, inside the GTA folder, still works.
- **Start screen** on every launch: recent workspaces (pin, remove, open the
  folder), the selected workspace's recent maps with thumbnails, a new
  workspace or any folder as one, and the game folder with the SA-MP client
  version it has. "Open the last workspace automatically" skips it; File >
  Switch Workspace shows it again.
- **Settings moved** from `<GTA>/haumea_data` to `~/.config/haumea`
  (`%APPDATA%\Haumea` on Windows, `~/Library/Application Support/Haumea` on
  macOS). They are copied over once; the old folder is left as it was.
- **Mod Loader mods are no longer loaded.** Players don't have your client
  mods, so the editor shows the stock game, SA-MP's models and the server's
  custom models. Nothing is exported into `modloader/` any more.
- **Shortcuts changed.** Windows moved from single letters to Ctrl+number and
  F-keys, so the letters are free for editing:

  | Action | Was | Now |
  |---|---|---|
  | Object Browser | B | Ctrl+1 |
  | Object Info | I | Ctrl+2 |
  | Map Objects | M | Ctrl+3 |
  | Tools | X | Ctrl+4 |
  | View | V | Ctrl+5 |
  | Time & Weather | T | Ctrl+6 |
  | Camera | E | Ctrl+7 |
  | Changes | F | Ctrl+8 |
  | Live Pawn Code | | Ctrl+9 |
  | Texture Browser | | Ctrl+T |
  | Keyboard Shortcuts / Nodes / Log | | F1 / F3 / F12 |
  | Test in game / Map Check | | F5 / F6 |
  | Rotate gizmo | Q | E (Q still works) |
  | Zoom (FOV) | Ctrl+wheel | wheel, Ctrl+= / Ctrl+- (Ctrl+wheel is now interface size) |

  F1 lists them all, and Preferences → Shortcuts changes any of them.
- Generated Pawn compiles without warnings on open.mp and SA-MP 0.3.7,
  keeps variable names stable across saves, and is written with the same
  line endings (LF) on every platform.
- Saving writes a temporary file and renames it over the map, so a crash
  mid-save never leaves a cut-off `.pwn`.
- Undo covers every edit, including vehicles and the Nodes tool, and a drag
  or typing in a field is one step.
- Big maps are fast: a 20000-object map opens in about 0.1 s (it took 37 s).
- The interface is trimmed to what SA-MP and open.mp use: Time & Weather
  takes a weather ID, and the GTA III / VC / LCS switches are gone.
- **Accents and symbols show correctly in game.** SA-MP and open.mp clients
  draw text as Windows-1252, as pawno and Texture Studio write it, so maps are
  now saved that way: "Ação", "Coração", "€" and "ñ" in labels, material text,
  textdraws and notes look the same in game as in the editor. Maps saved as
  Windows-1252 also open correctly. Each map remembers its text encoding (a
  `// Text encoding:` line in the file); older Haumea maps saved as UTF-8 stay
  UTF-8 until you pick Windows-1252 in **Save As**. Map Check names any
  character Windows-1252 can't hold (it's saved as `?`).
- Windows: paths with any characters (João, Жора) work on Windows 10 1903
  and later.

### What you need

- **Linux**: x86_64 with glibc 2.38 or newer (Ubuntu 24.04, Debian 13,
  Fedora 39 or newer), `libglfw3`, and `zenity` or `kdialog` for file
  dialogs. Test in game also needs the 32-bit C library for open.mp's server.
- **Windows**: Windows 10 or 11; 1903 or later for non-ASCII paths.
- **NPC features** (NPCs, paths, nodes, recordings) need an **open.mp**
  server with its NPCs component; SA-MP 0.3.7 has no NPC natives.
- **Custom models** need SA-MP 0.3.DL or open.mp with `artwork.enable` on,
  the models in the server's `models/artconfig.txt`, and players on 0.3.DL or
  an open.mp client.
- **Checkpoints** and Streamer saves need the streamer plugin.
