#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write a tiny synthetic GTA San Andreas folder for Haumea's tests.

No game files are copied: every model is a coloured box written here in the
PC RenderWare format, and the data files hold only the entries the test maps
use (with the game's model names, so the generated Pawn reads the same as
with the real game). The path nodes are made by the Nodes tool's code, so it
needs g++. Usage: make_game.py <dir>
"""
import struct
import sys
from pathlib import Path

RW_VERSION = 0x1803FFFF	# GTA San Andreas (RW 3.6.0.3)

# Models the tests and the test maps use, and the stand-ins Haumea draws
# elements with: id -> (name, txd, draw distance, flags).
OBJECTS = {
	1215: ('bollardlight', 'metal', 40, 2097156),
	1225: ('barrel4', 'dynbarrels', 40, 2162816),
	1226: ('lamppost3', 'dynsigns', 62, 32896),
	1228: ('roadworkbarrier1', 'barrier', 62, 128),
	1238: ('trafficcone', 'dyncones', 100, 128),
	1239: ('info', 'icons6', 100, 128),
	1240: ('health', 'icons5', 100, 128),
	1241: ('adrenaline', 'icons8', 100, 128),
	1242: ('bodyarmour', 'icons7', 100, 128),
	1247: ('bribe', 'icons9', 100, 128),
	1254: ('killfrenzy', 'icons', 100, 128),
	1257: ('bustopm', 'bustopm', 60, 2097284),
	1271: ('gunbox', 'gunbox', 40, 128),
	1275: ('clothesp', 'icons1', 100, 128),
	1280: ('parkbench1', 'benches_cj', 50, 128),
	1281: ('parktable1', 'benches', 47, 2097280),
	1313: ('killfrenzy2plyr', 'icons', 100, 128),
	1317: ('Cylinder', 'ingame', 100, 2097152),
	1318: ('arrow', 'ingame', 100, 0),
	1337: ('BinNt07_LA', 'LABins01_LA', 70, 128),
	1408: ('DYN_F_WOOD_2', 'BREAK_FENCE1', 60, 128),
	1411: ('DYN_MESH_1', 'BREAK_FEN_mesh2', 65, 132),
	1481: ('DYN_BAR_B_Q', 'BREAK_STREET2', 100, 0),
	1502: ('Gen_doorINT04', 'int_doors', 50, 160),
	1559: ('diamond_3', 'DIAMOND', 100, 2097152),
	1610: ('sandcastle1', 'sandcastle', 20, 128),
	1634: ('landjump2', 'landjump', 100, 128),
	1655: ('waterjumpx2', 'landjump', 240, 128),
	2232: ('MED_SPEAKER_4', 'CJ_HI_FI', 30, 0),
	2524: ('CJ_B_SINK4', 'CJ_BATHROOM', 50, 0),
	3096: ('bb_pickup', 'bbpcpx', 100, 132),
	3555: ('compmedhos2_LAe', 'comedhos1_la', 80, 128),
	3563: ('LODpmedhos2_LAe', 'comphouse_lod', 400, 128),
	3578: ('DockBarr1_LA', 'sjmla_las', 80, 128),
	3594: ('la_fuckcar1', 'lafuckar', 80, 128),
	4206: ('PershingPool_LAn', 'pershingsq', 150, 128),
	8391: ('ballys03_lvs', 'ballys01', 225, 128),
	14720: ('int2lasmone02', 'lasmall1int2', 100, 0),
	16640: ('des_a51_inner3', 'a51', 30, 0),
}
# SA-MP's own objects live in SAMP/SAMP.img with SAMP/SAMP.ide.
SAMP_OBJECTS = {
	19297: ('PointLight17', 'MatLights', 120, 4),
	19353: ('wall001', 'all_walls', 100, 0),
	19377: ('wall025', 'all_walls', 100, 0),
}
VEHICLES = [
	'411, infernus, infernus, car, INFERNUS, INFERNU, null, executive, 5, 0, 0, -1, 0.7, 0.7, 0',
	'468, sanchez, sanchez, bike, DIRTBIKE, SANCHEZ, biked, motorbike, 5, 0, 0, 23, 0.68, 0.62, -1',
	'522, nrg500, nrg500, bike, NRG500, NRG500, bikes, motorbike, 10, 0, 1f341210, 16, 0.68, 0.68, -1',
	'538, streak, streak, train, STREAK, STREAK, null, ignore, 10, 0, 0, -1, 1.0, 1.0, -1',
	'560, sultan, sultan, car, SULTAN, SULTAN, null, richfamily, 7, 0, 0, -1, 0.7, 0.7, 1',
	'562, elegy, elegy, car, ELEGY, ELEGY, null, richfamily, 7, 0, 0, -1, 0.68, 0.68, 1',
]
PEDS = [
	'0, null, generic, PLAYER1, STAT_PLAYER, player, 0, 0, null, 9,9, PED_TYPE_PLAYER, VOICE_PLY_CR, VOICE_PLY_CR',
	'7, male01, male01, CIVMALE, STAT_SENSIBLE_GUY, man, 0, 0, man, 1,4, PED_TYPE_GEN, VOICE_GEN_MALE01, VOICE_GEN_MALE01',
	'101, WMYST, WMYST, CIVMALE, STAT_STREET_GUY, man,1983,1, null,4,3,PED_TYPE_GEN,VOICE_GEN_WMYST,VOICE_GEN_WMYST',
	'102, BALLAS1, BALLAS1, GANG1, STAT_GANG1, gang1, 110F,1, null,3,3,PED_TYPE_GANG,VOICE_GNG_BALLAS1,VOICE_GNG_BALLAS2',
	'105, FAM1, FAM1, GANG2, STAT_GANG2, gang2, 110F,1, null,5,5,PED_TYPE_GANG,VOICE_GNG_FAM1,VOICE_GNG_FAM2',
	'179, WMYAMMO, WMYAMMO, CIVMALE, STAT_TOUGH_GUY, man, 1000,1, man,1,4,PED_TYPE_GFD,VOICE_GFD_WMYAMMO,VOICE_GFD_WMYAMMO',
	'180, BMYTATT, BMYTATT, CIVMALE, STAT_TOUGH_GUY, man, 1000,1, man,1,4,PED_TYPE_GFD,VOICE_GFD_BMYTATT,VOICE_GFD_BMYTATT',
	'280, lapd1, lapd1, COP, STAT_COP, swat, 1FFF, 0, null, 9,9, PED_TYPE_EMG,VOICE_EMG_LAPD1,VOICE_EMG_LAPD8',
	'255, WMYCH, WMYCH, CIVMALE, STAT_TOUGH_GUY, man, 0,1, null,7,0,PED_TYPE_GEN,VOICE_GEN_WMYCH,VOICE_GEN_WMYCH',
]
# The world building the round-trip fixture removes, with its LOD.
WORLD = [
	# index, id, name, x, y, z, lod index
	(0, 3563, 'LODpmedhos2_LAe', 2063.132813, -1731.78125, 15.2734375, -1),
	(1, 3555, 'compmedhos2_LAe', 2063.132813, -1731.78125, 15.2734375, 0),
	(2, 4206, 'PershingPool_LAn', 1479.554688, -1639.609375, 13.648438, -1),
]
PARTICLE_TEXTURES = ['waterclear256', 'cloud1', 'cloud2', 'cloud3', 'cloudhilit', 'sunrays', 'coronastar',
	'coronamoon', 'coronaringa', 'coronaheadlightline', 'coronaringb', 'lensflare1', 'lensflare2',
	'shad_exp', 'shad_ped', 'shad_car', 'shad_bike', 'shad_heli', 'shad_rcbaron', 'waterwake',
	'seabd32', 'collisionsmoke']


# ---- RenderWare streams ----

def chunk(kind, payload):
	return struct.pack('<III', kind, len(payload), RW_VERSION) + payload


def rw_struct(payload):
	return chunk(0x01, payload)


def rw_string(text):
	data = text.encode('ascii') + b'\0'
	data += b'\0' * (-len(data) % 4)
	return chunk(0x02, data)


def extension(payload=b''):
	return chunk(0x03, payload)


def texture_native(name, colour, size=8):
	"""A D3D9 32-bit (8888) texture, the format of the PC game's TXDs."""
	r, g, b, a = colour
	pixels = bytes([b, g, r, a]) * (size * size)
	header = struct.pack('<II', 9, 0x1102)	# D3D9, linear filter, wrap
	header += name.encode('ascii')[:31].ljust(32, b'\0') + b''.ljust(32, b'\0')
	header += struct.pack('<IIHHBBBB', 0x0500, 21, size, size, 32, 1, 4, 1 if a < 255 else 0)
	header += struct.pack('<I', len(pixels)) + pixels
	return chunk(0x15, rw_struct(header) + extension())


def texture_dictionary(textures):
	natives = b''.join(texture_native(name, colour) for name, colour in textures)
	return chunk(0x16, rw_struct(struct.pack('<HH', len(textures), 2)) + natives + extension())


def box_geometry(size, texture, centre=(0, 0, 0)):
	"""A box with normals, UVs and one textured material, around centre."""
	sx, sy, sz = (s / 2 for s in size)
	cx, cy, cz = centre
	faces = [((1, 0, 0), [(sx, -sy, -sz), (sx, sy, -sz), (sx, sy, sz), (sx, -sy, sz)]),
	         ((-1, 0, 0), [(-sx, sy, -sz), (-sx, -sy, -sz), (-sx, -sy, sz), (-sx, sy, sz)]),
	         ((0, 1, 0), [(sx, sy, -sz), (-sx, sy, -sz), (-sx, sy, sz), (sx, sy, sz)]),
	         ((0, -1, 0), [(-sx, -sy, -sz), (sx, -sy, -sz), (sx, -sy, sz), (-sx, -sy, sz)]),
	         ((0, 0, 1), [(-sx, -sy, sz), (sx, -sy, sz), (sx, sy, sz), (-sx, sy, sz)]),
	         ((0, 0, -1), [(-sx, sy, -sz), (sx, sy, -sz), (sx, -sy, -sz), (-sx, -sy, -sz)])]
	verts, normals, uvs, tris = [], [], [], []
	for normal, quad in faces:
		base = len(verts)
		verts += [(x + cx, y + cy, z + cz) for x, y, z in quad]
		normals += [normal] * 4
		uvs += [(0, 0), (1, 0), (1, 1), (0, 1)]
		tris += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
	flags = 0x02 | 0x04 | 0x10 | 0x20 | 0x40	# positions, textured, normals, light, modulate
	data = struct.pack('<IIII', flags | (1 << 16), len(tris), len(verts), 1)
	data += b''.join(struct.pack('<ff', u, v) for u, v in uvs)
	# RW triangle order: v2, v1, material, v3
	data += b''.join(struct.pack('<HHHH', b, a, 0, c) for a, b, c in tris)
	radius = (sx * sx + sy * sy + sz * sz) ** 0.5
	data += struct.pack('<ffffII', cx, cy, cz, radius, 1, 1)
	data += b''.join(struct.pack('<fff', *v) for v in verts)
	data += b''.join(struct.pack('<fff', *n) for n in normals)
	tex = chunk(0x06, rw_struct(struct.pack('<HH', 0x1106, 0)) + rw_string(texture) + rw_string('') + extension())
	material = chunk(0x07, rw_struct(struct.pack('<IBBBBIIfff', 0, 255, 255, 255, 255, 0, 1, 1.0, 1.0, 1.0)) +
	                 tex + extension())
	materials = chunk(0x08, rw_struct(struct.pack('<Ii', 1, -1)) + material)
	# Bin mesh: one mesh, triangle lists, listing the triangles' indices.
	indices = [i for t in tris for i in t]
	binmesh = struct.pack('<III', 0, 1, len(indices)) + struct.pack('<II', len(indices), 0)
	binmesh += b''.join(struct.pack('<I', i) for i in indices)
	return chunk(0x0F, rw_struct(data) + materials + extension(chunk(0x50E, binmesh)))


def frame_list(names):
	"""names[0] is the root; the rest are its children (dummies, nodes)."""
	data = struct.pack('<I', len(names))
	for i in range(len(names)):
		data += struct.pack('<9f', 1, 0, 0, 0, 1, 0, 0, 0, 1) + struct.pack('<fff', 0, 0, 0)
		data += struct.pack('<iI', -1 if i == 0 else 0, 0)
	exts = b''.join(extension(chunk(0x253F2FE, name.encode('ascii'))) for name in names)
	return chunk(0x0E, rw_struct(data) + exts)


def clump(size, texture, frames=None, centre=(0, 0, 0)):
	names = frames or ['root']
	geometry = chunk(0x1A, rw_struct(struct.pack('<I', 1)) + box_geometry(size, texture, centre))
	atomic = chunk(0x14, rw_struct(struct.pack('<IIII', 0, 0, 5, 0)) + extension())
	return chunk(0x10, rw_struct(struct.pack('<III', 1, 0, 0)) + frame_list(names) + geometry + atomic + extension())


def collision(name, model_id, size, centre=(0, 0, 0)):
	"""A COL1 box: the bounds and one box."""
	sx, sy, sz = (s / 2 for s in size)
	cx, cy, cz = centre
	radius = (sx * sx + sy * sy + sz * sz) ** 0.5
	lo, hi = (cx - sx, cy - sy, cz - sz), (cx + sx, cy + sy, cz + sz)
	body = name.encode('ascii')[:21].ljust(22, b'\0') + struct.pack('<H', model_id)
	body += struct.pack('<f3f3f3f', radius, cx, cy, cz, *lo, *hi)
	body += struct.pack('<I', 0) + struct.pack('<I', 0)	# spheres, unknown
	body += struct.pack('<I', 1) + struct.pack('<3f3fBBBB', *lo, *hi, 0, 0, 0, 0)
	body += struct.pack('<I', 0) + struct.pack('<I', 0)	# vertices, faces
	return b'COLL' + struct.pack('<I', len(body)) + body


def write_img(path, files):
	"""A VER2 IMG archive: 2048-byte sectors, a directory of 32-byte entries."""
	names = sorted(files)
	header_sectors = (8 + 32 * len(names) + 2047) // 2048
	directory, blobs, sector = b'', b'', header_sectors
	for name in names:
		data = files[name]
		data += b'\0' * (-len(data) % 2048)
		count = len(data) // 2048
		directory += struct.pack('<IHH', sector, count, 0) + name.encode('ascii')[:23].ljust(24, b'\0')
		blobs += data
		sector += count
	header = b'VER2' + struct.pack('<I', len(names)) + directory
	header += b'\0' * (header_sectors * 2048 - len(header))
	path.write_bytes(header + blobs)


def shape_for(name):
	"""A size and an off-centre origin per model, like real models have."""
	h = sum(name.lower().encode()) * 2654435761 & 0xFFFFFFFF
	size = (1.0 + (h & 3) * 0.75, 1.0 + (h >> 2 & 3) * 0.75, 1.0 + (h >> 4 & 3) * 0.5)
	centre = (((h >> 6 & 3) - 1.5) * 0.2, ((h >> 8 & 3) - 1.5) * 0.2, size[2] / 2 - 0.3)
	return size, centre


def colour_for(name):
	h = sum(name.encode()) * 2654435761 & 0xFFFFFF
	return (64 + (h & 0x7F), 64 + (h >> 8 & 0x7F), 64 + (h >> 16 & 0x7F), 255)


def main(target):
	root = Path(target)
	for sub in ('data/maps', 'data/Paths', 'models', 'SAMP', 'anim'):
		(root / sub).mkdir(parents=True, exist_ok=True)
	(root / 'gta_sa.exe').write_bytes(b'')	# marks the folder as GTA San Andreas

	img, samp_img = {}, {}
	txds = {}
	for model_id, (name, txd, dist, flags) in OBJECTS.items():
		size, centre = shape_for(name)
		img[name.lower() + '.dff'] = clump(size, name.lower(), centre=centre)
		txds.setdefault(txd.lower(), []).append(name.lower())
	for model_id, (name, txd, dist, flags) in SAMP_OBJECTS.items():
		samp_img[name.lower() + '.dff'] = clump((2, 0.2, 3), name.lower())
		samp_img[txd.lower() + '.txd'] = texture_dictionary([(name.lower(), colour_for(name))])
	for line in VEHICLES:
		fields = [f.strip() for f in line.split(',')]
		name = fields[1]
		img[name + '.dff'] = clump((2, 4.5, 1.4), name, ['root', 'chassis_dummy', 'wheel_lf_dummy',
			'wheel_rf_dummy', 'wheel_lb_dummy', 'wheel_rb_dummy', 'chassis'])
		img[name + '.txd'] = texture_dictionary([(name, colour_for(name))])
	for line in PEDS:
		fields = [f.strip() for f in line.split(',')]
		name, txd = fields[1], fields[2]
		if name == 'null':
			continue
		img[name.lower() + '.dff'] = clump((0.6, 0.4, 1.8), name.lower())
		img[txd.lower() + '.txd'] = texture_dictionary([(name.lower(), colour_for(name))])
	for txd, textures in txds.items():
		img[txd + '.txd'] = texture_dictionary([(t, colour_for(t)) for t in textures])
	(root / 'models' / 'fixture.col').write_bytes(b''.join(collision(name, model_id, *shape_for(name))
		for model_id, (name, txd, dist, flags) in sorted(OBJECTS.items())))
	write_img(root / 'models' / 'gta3.img', img)
	write_img(root / 'models' / 'gta_int.img', {})
	write_img(root / 'SAMP' / 'SAMP.img', samp_img)
	(root / 'models' / 'particle.txd').write_bytes(
		texture_dictionary([(name, (120, 170, 220, 160)) for name in PARTICLE_TEXTURES]))

	def ide(entries):
		return 'objs\n' + ''.join(f'{i}, {n}, {t}, {d}, {f}\n' for i, (n, t, d, f) in sorted(entries.items())) + 'end\n'
	(root / 'data' / 'maps' / 'fixture.ide').write_text(ide(OBJECTS))
	(root / 'SAMP' / 'SAMP.ide').write_text(ide(SAMP_OBJECTS))
	(root / 'data' / 'vehicles.ide').write_text('cars\n' + ''.join(v + '\n' for v in VEHICLES) + 'end\n')
	(root / 'data' / 'peds.ide').write_text('peds\n' + ''.join(p + '\n' for p in PEDS) + 'end\n')
	(root / 'data' / 'default.ide').write_text('')
	(root / 'data' / 'maps' / 'fixture.ipl').write_text('inst\n' + ''.join(
		f'{i[1]}, {i[2]}, 0, {i[3]}, {i[4]}, {i[5]}, 0, 0, 0, 1, {i[6]}\n' for i in WORLD) + 'end\n'
		# One interior entrance, as in the game's IPLs, for the Interiors window.
		'enex\n286.149, -41.5444, 1000.57, 0, 1.5, 1, 8, 286.149, -40.6444, 1000.57, -0.100006, 1, 8, '
		'"AMMUN1", 4, 2, 0, 24\nend\n')
	(root / 'data' / 'default.dat').write_text('IDE DATA\\DEFAULT.IDE\nIDE DATA\\VEHICLES.IDE\nIDE DATA\\PEDS.IDE\n')
	(root / 'data' / 'gta.dat').write_text('IDE DATA\\MAPS\\FIXTURE.IDE\nCOLFILE 0 MODELS\\FIXTURE.COL\n'
	                                       'IPL DATA\\MAPS\\FIXTURE.IPL\n')
	(root / 'data' / 'water.dat').write_text('processed\n')
	# The open.mp NPC path nodes, written by the Nodes tool's own code.
	sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
	from test_nodes_edit import write_synthetic_nodes
	write_synthetic_nodes(root / 'data' / 'Paths')
	print(root)


if __name__ == '__main__':
	main(sys.argv[1])
