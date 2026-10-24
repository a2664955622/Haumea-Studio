# Pawn and XML documents

Saving `map.pwn` produces `map.pwn` and `map.pwn.xml`. TextDraw files use the same rule:
`hud.inc` and `hud.inc.xml`. Keep the pair together for editing. Only the Pawn belongs in the
server's include/compile flow; it runs without the XML.

The XML is UTF-8, with root `<haumea version="1" kind="map">` or `kind="textdraws"`.
It stores structured fields, rather than a copy of Pawn source:

- `scene/object`: stable document ID, model, element kind, position, quaternion rotation, source
  Euler angles, interior, world/removal state, visibility, properties, vehicle settings and materials.
- Object `parent`, `npc-vehicle` and `lod`: references to document IDs, resolved anew on load.
  Runtime instance IDs and cached texture slots are not persisted.
- `removals`: original model `-1` removal spheres; `radar`: editable layer names, modes and zones.
- `textdraws`: every layer in order, including reference images, hidden/locked layers, artboards,
  shape settings and ruler guides. Runtime values are present here as well.
- `alignment`: pins, linked measurement endpoints, snapping and alignment options.
- `camera`: saved position, target, interior and FOV. Moving the camera alone does not mark a map unsaved.
- Root `format` and `encoding`: Pawn export format and text encoding. XML itself always uses UTF-8.

Numbers use a decimal point; colours use `0xRRGGBBAA` where the corresponding editor field does.
Object transforms and materials retain the editor's precision. XML attributes escape quotes,
ampersands, newlines and Unicode text. Unchanged world objects are reference geometry, so only
world edits and saved visibility/display overrides enter the document. Missing properties on an
otherwise unchanged world object use version 1's object defaults.

`pawn-sha256` identifies the exact Pawn bytes paired with this snapshot. When they match, XML
restores the full scene, including entities unsupported by the chosen Pawn export format. When
Pawn has changed externally, Pawn supplies runtime values; XML supplies metadata for matching
objects/layers. Named objects can retain their metadata after a position change. Unmatched
objects follow the Pawn source. The editor reports that this reconciliation happened.

Old Pawn metadata comments remain readable. Opening does not write anything. The first save of
an existing Pawn without XML keeps its original bytes in `.pre-xml.bak` (or a numbered backup
if that name is occupied), then writes the new pair. Groups, notes, legacy TextDraw comments and
other supported editor metadata move into XML.

Both outputs are staged before replacement. A previous XML generation is retained during the
replacement so an interrupted save can reopen the generation matching the Pawn. A normal save
removes that recovery file. Invalid or unsupported XML is never silently overwritten: repair or
move it before saving over the pair. A map can still open its Pawn and explain the XML error;
a TextDraw document refuses the broken pair so its reference layers cannot be lost accidentally.
