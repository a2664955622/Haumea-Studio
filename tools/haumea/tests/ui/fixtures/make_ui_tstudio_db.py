# SPDX-License-Identifier: GPL-2.0-or-later
"""Writes ui_tstudio.db, a two-object Texture Studio map for import_export.uis.

Run it again only to change the map: python3 make_ui_tstudio_db.py
Texture Studio keeps maps in SQLite with these tables (only the columns
Haumea reads). TextureIndex is Texture Studio's compressed array: a 4-byte
length (7 bits a byte), then one byte per small value with bit 7 set.
"""
from pathlib import Path
import sqlite3

path = Path(__file__).with_name('ui_tstudio.db')
path.unlink(missing_ok=True)
db = sqlite3.connect(path)
db.execute('CREATE TABLE Objects (IndexID INTEGER, ModelID INTEGER, xPos REAL, yPos REAL, zPos REAL, '
           'rxRot REAL, ryRot REAL, rzRot REAL, DrawDistance REAL, GroupID INTEGER, Note TEXT, '
           'TextureIndex BLOB, ColorIndex BLOB, usetext INTEGER, FontFace INTEGER, FontSize INTEGER, '
           'ObjectText TEXT)')
db.execute('CREATE TABLE RemovedBuildings (ModelID INTEGER, xPos REAL, yPos REAL, zPos REAL, Range REAL)')
db.execute('CREATE TABLE Settings (VirtualWorld INTEGER, Interior INTEGER)')
textured = bytes([0, 0, 0, 5, 0x80 | 3])	# slot 0: texture 3, 2notherbuildsfe ferry_build14
db.execute('INSERT INTO Objects VALUES (0, 1271, 2060.0, -3000.0, 5.0, 0, 0, 0, 300, 0, "", ?, NULL, 0, 0, 0, "")',
           (textured,))
db.execute('INSERT INTO Objects VALUES (1, 1634, 2070.0, -3000.0, 5.0, 0, 0, 180, 300, 0, "ts ramp", NULL, NULL, 0, 0, 0, "")')
db.execute('INSERT INTO Settings VALUES (-1, -1)')
db.commit()
db.close()
