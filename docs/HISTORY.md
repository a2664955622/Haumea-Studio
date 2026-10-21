# History: from Haumea to Haumea Studio

Haumea Studio 1.0.0 grew out of the Haumea map editor (last released as
1.40.11). If you used Haumea before, this is what changed for you; the
changelog lists Haumea Studio's features.

- **The name is Haumea Studio.** Only the name you see changes: the program
  is still `haumea` (`haumea.exe`).
- **Version numbers start again at 1.0.0**, with a build number:
  1.0.0.<build>. Haumea's own numbers went up to 1.40.11.
- **Haumea runs from its own folder.** The old layout, inside the GTA folder,
  still works.
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

- **W/A/S/D no longer fly the camera on their own**: hold the right mouse
  button to fly.
- **Text encoding**: maps are now saved as Windows-1252, as the clients draw
  them. Older Haumea maps saved as UTF-8 stay UTF-8 until you pick
  Windows-1252 in **Save As**.
- **Faster**: a 20000-object map opens in about 0.1 s; it took 37 s.
- The interface is trimmed to what open.mp and SA-MP use: Time & Weather
  takes a weather ID, and the GTA III / VC / LCS switches are gone.
- The AI companion Agent Alpha is now **Namaka**.
