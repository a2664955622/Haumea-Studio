#!/usr/bin/env python3
"""server_check.py maps-dir...: run generated maps on the open.mp server, with an
NPC as the player, and check the per-player code does what it should.

Each map gets a gamemode that wraps the per-player natives before including
the map (GangZoneShowForPlayer, SetPlayerMapIcon, RemoveBuildingForPlayer,
PlayAudioStreamForPlayer, PlayerPlaySound, TextDrawShowForPlayer,
PlayerTextDrawShow) to count calls and failed returns, and NPC_StartPlayback,
NPC_MoveByPath and NPC_PlayNode to know which NPCs should move. A probe NPC
joins; on its spawn the gamemode calls the map's _RemoveWorldObjectsForPlayer,
_ShowForPlayer and _ShowTextDraws and its sound helpers for it, then checks the
server's state: the gang zones are visible to it, its buildings are removed,
the textdraws and per-player gang zones are visible, streamed gang zones load
around it and unload far away, every map NPC exists, and the NPCs that play a
recording or walk a path or the nodes are doing so and have moved. The server
log must have no errors or warnings.

Uses the Pawn toolchain (PAWN_TOOLCHAIN: open.mp's Linux server and pawncc,
the streamer include and plugin) and, when HAUMEA_GAME_DIR is set, the game's
path node files for NPC_PlayNode and gta3.img for the showcase's custom model.
Exit status 0 when every map passes; 2 when it can't run (no server).
"""
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLCHAIN = Path(os.environ.get('PAWN_TOOLCHAIN') or HERE / '.toolchain')
SERVER = TOOLCHAIN / 'omp' / 'Server'
STREAMER = TOOLCHAIN / 'streamer'
SHOWCASE = HERE.parent / 'showcase'
GAME_DIR = os.environ.get('HAUMEA_GAME_DIR')

# Wrappers for the per-player natives: each counts calls and failed returns.
HOOKS = r'''
enum { HC_ZONE, HC_ICON, HC_REMOVE, HC_STREAM, HC_SOUND, HC_TD, HC_PTD, HC_PZONE, HC_KINDS }
new HC_calls[HC_KINDS], HC_fails[HC_KINDS];
new Text:HC_td[64], HC_numTd, PlayerText:HC_ptd[64], HC_numPtd;
new HC_movers[32], HC_numMovers, HC_npcsCreated;
new HC_vehicleOf[MAX_PLAYERS] = { -1, ... };	// NPC_PutInVehicle's vehicle per NPC
new bool:HC_plays[MAX_PLAYERS];
new Float:HC_start[MAX_PLAYERS][3];
new bool:HC_inRange;	// sound ticks: every place counts as in range

stock HC_note(kind, bool:ok) { HC_calls[kind]++; if(!ok) HC_fails[kind]++; return ok; }
stock bool:HC_GangZoneShowForPlayer(playerid, zoneid, colour)
	return bool:HC_note(HC_ZONE, GangZoneShowForPlayer(playerid, zoneid, colour));
stock bool:HC_PlayerGangZoneShow(playerid, zoneid, colour)
	return bool:HC_note(HC_PZONE, PlayerGangZoneShow(playerid, zoneid, colour));
// Per-player gang zones the player can see (their IDs are the player's own).
stock HC_playerZones(playerid)
{
	new n;
	for(new z = 0; z < 1024; z++)
		if(IsValidPlayerGangZone(playerid, z) && IsPlayerGangZoneVisible(playerid, z)) n++;
	return n;
}
stock bool:HC_SetPlayerMapIcon(playerid, iconid, Float:x, Float:y, Float:z, markerType, colour, MAPICON:style = MAPICON_LOCAL)
	return bool:HC_note(HC_ICON, SetPlayerMapIcon(playerid, iconid, x, y, z, markerType, colour, style));
stock bool:HC_RemoveBuildingForPlayer(playerid, modelid, Float:x, Float:y, Float:z, Float:radius)
	return bool:HC_note(HC_REMOVE, RemoveBuildingForPlayer(playerid, modelid, x, y, z, radius));
stock bool:HC_PlayAudioStreamForPlayer(playerid, const url[], Float:x = 0.0, Float:y = 0.0, Float:z = 0.0, Float:distance = 50.0, bool:usepos = false)
	return bool:HC_note(HC_STREAM, PlayAudioStreamForPlayer(playerid, url, x, y, z, distance, usepos));
stock bool:HC_PlayerPlaySound(playerid, soundid, Float:x = 0.0, Float:y = 0.0, Float:z = 0.0)
	return bool:HC_note(HC_SOUND, PlayerPlaySound(playerid, soundid, x, y, z));
stock bool:HC_TextDrawShowForPlayer(playerid, Text:textid)
{
	if(HC_numTd < sizeof HC_td) HC_td[HC_numTd++] = textid;
	return bool:HC_note(HC_TD, TextDrawShowForPlayer(playerid, textid));
}
stock bool:HC_PlayerTextDrawShow(playerid, PlayerText:textid)
{
	if(HC_numPtd < sizeof HC_ptd) HC_ptd[HC_numPtd++] = textid;
	return bool:HC_note(HC_PTD, PlayerTextDrawShow(playerid, textid));
}
stock bool:HC_IsPlayerInRangeOfPoint(playerid, Float:range, Float:x, Float:y, Float:z)
	return HC_inRange || IsPlayerInRangeOfPoint(playerid, range, x, y, z);
stock HC_NPC_Create(const name[])
{
	HC_npcsCreated++;
	return NPC_Create(name);
}
// Where an NPC is: its vehicle's position when it drives one.
stock HC_where(npcid, &Float:x, &Float:y, &Float:z)
{
	if(HC_vehicleOf[npcid] != -1)
		return GetVehiclePos(HC_vehicleOf[npcid], x, y, z);
	return NPC_GetPos(npcid, x, y, z);
}
stock HC_mover(npcid) { if(HC_numMovers < sizeof HC_movers) HC_movers[HC_numMovers++] = npcid; }
stock bool:HC_NPC_PutInVehicle(npcid, vehicleid, seatid)
{
	if(0 <= npcid < MAX_PLAYERS) HC_vehicleOf[npcid] = vehicleid;
	return NPC_PutInVehicle(npcid, vehicleid, seatid);
}
stock bool:HC_NPC_StartPlayback(npcid, const recordName[], bool:autoUnload = false, Float:startX = 0.0, Float:startY = 0.0, Float:startZ = 0.0, Float:rotX = 0.0, Float:rotY = 0.0, Float:rotZ = 0.0)
{
	HC_mover(npcid);
	if(0 <= npcid < MAX_PLAYERS) HC_plays[npcid] = true;
	return NPC_StartPlayback(npcid, recordName, autoUnload, startX, startY, startZ, rotX, rotY, rotZ);
}
stock bool:HC_NPC_MoveByPath(npcid, pathid, NPC_MOVE_TYPE:moveType = NPC_MOVE_TYPE_AUTO, Float:moveSpeed = NPC_MOVE_SPEED_AUTO, bool:reversed = false)
{
	HC_mover(npcid);
	return NPC_MoveByPath(npcid, pathid, moveType, moveSpeed, reversed);
}
stock bool:HC_NPC_PlayNode(npcid, nodeid, NPC_MOVE_TYPE:moveType = NPC_MOVE_TYPE_AUTO, Float:speed = NPC_MOVE_SPEED_AUTO, Float:radius = 0.0, bool:setAngle = true)
{
	HC_mover(npcid);
	return NPC_PlayNode(npcid, nodeid, moveType, speed, radius, setAngle);
}
'''

DEFINES = r'''
#define GangZoneShowForPlayer HC_GangZoneShowForPlayer
#define PlayerGangZoneShow HC_PlayerGangZoneShow
#define SetPlayerMapIcon HC_SetPlayerMapIcon
#define RemoveBuildingForPlayer HC_RemoveBuildingForPlayer
#define PlayAudioStreamForPlayer HC_PlayAudioStreamForPlayer
#define PlayerPlaySound HC_PlayerPlaySound
#define TextDrawShowForPlayer HC_TextDrawShowForPlayer
#define PlayerTextDrawShow HC_PlayerTextDrawShow
#define IsPlayerInRangeOfPoint HC_IsPlayerInRangeOfPoint
#define NPC_Create HC_NPC_Create
#define NPC_StartPlayback HC_NPC_StartPlayback
#define NPC_PutInVehicle HC_NPC_PutInVehicle
#define NPC_MoveByPath HC_NPC_MoveByPath
#define NPC_PlayNode HC_NPC_PlayNode
'''


def gamemode(map_path, source):
	prefix = re.search(r'stock (\w+)_CreateObjects\(\)', source).group(1)
	has = lambda name: f'stock {prefix}_{name}(' in source
	streamer = '#include <streamer>' in source
	init, spawn = [f'\t{prefix}_CreateObjects();'], []
	if has('CreateTextDraws'):
		init.append(f'\t{prefix}_CreateTextDraws();')
	if has('RemoveWorldObjectsForPlayer'):
		spawn.append(f'\t{prefix}_RemoveWorldObjectsForPlayer(playerid);')
	if has('ShowForPlayer'):
		spawn.append(f'\t{prefix}_ShowForPlayer(playerid);')
	if has('ShowTextDraws'):
		spawn.append(f'\t{prefix}_ShowTextDraws(playerid);')
	if has('SoundTick'):
		spawn.append(f'\tHC_inRange = true;\n\t{prefix}_SoundTick(playerid);\n\tHC_inRange = false;')
	if has('SoundArea'):
		spawn.append('\tfor(new a = 1; a <= Streamer_GetUpperBound(STREAMER_TYPE_AREA); a++)\n'
		             f'\t\t{prefix}_SoundArea(playerid, a, true);')
	# Streamed gang zones: the probe stands on the first one, so the cells
	# around it load, and then walks away, so they unload.
	streamed = re.search(r'_AddStreamedZone\(\s*([-\d.]+),\s*([-\d.]+),\s*([-\d.]+),\s*([-\d.]+)', source)
	zone_check = ''
	if has('ZoneUpdate') and streamed:
		x0, y0, x1, y1 = (float(v) for v in streamed.groups())
		spawn.append(f'\tHC_baseZones = HC_playerZones(playerid);\n'
		             f'\tNPC_SetPos(playerid, {(x0 + x1) / 2:.3f}, {(y0 + y1) / 2:.3f}, 20.0);\n'
		             f'\t{prefix}_ZoneUpdate(playerid);')
		zone_check = (f'\tnew streamedZones = HC_playerZones(HC_probe) - HC_baseZones;\n'
		              f'\tNPC_SetPos(HC_probe, 19000.0, 19000.0, 20.0);\n'
		              f'\t{prefix}_ZoneUpdate(HC_probe);\n'
		              f'\tnew left = HC_playerZones(HC_probe) - HC_baseZones;\n'
		              f'\tprintf("[check] streamed gang zones: %d loaded on the first zone, %d left far away", streamedZones, left);\n'
		              f'\tif(streamedZones < 1 || left != 0) failures++;')
	return f'''#include <open.mp>
{'#include <streamer>' if streamer else ''}
{HOOKS}
new HC_probe = INVALID_PLAYER_ID, HC_failures, HC_baseZones;

main() {{}}

public OnGameModeInit()
{{
{chr(10).join(init)}
	SetTimer("HC_Join", 1000, false);
	return 1;
}}

forward HC_Join();
public HC_Join()
{{
	HC_probe = NPC_Create("HaumeaProbe");
	NPC_Spawn(HC_probe);
}}

public OnNPCSpawn(npcid)
{{
	if(npcid != HC_probe)
		return 1;
	new playerid = npcid;
	printf("[check] probe %d spawned", playerid);
{chr(10).join(spawn)}
	for(new i = 0; i < MAX_PLAYERS; i++)
		if(NPC_IsValid(i))
			HC_where(i, HC_start[i][0], HC_start[i][1], HC_start[i][2]);
	SetTimer("HC_Check", 3000, false);
	return 1;
}}

forward HC_Check();
public HC_Check()
{{
	new failures, names[HC_KINDS][] = {{ "GangZoneShowForPlayer", "SetPlayerMapIcon",
		"RemoveBuildingForPlayer", "PlayAudioStreamForPlayer", "PlayerPlaySound",
		"TextDrawShowForPlayer", "PlayerTextDrawShow", "PlayerGangZoneShow" }};
	for(new k = 0; k < HC_KINDS; k++)
	{{
		if(!HC_calls[k]) continue;
		printf("[check] %s: %d call(s), %d failed", names[k], HC_calls[k], HC_fails[k]);
		if(HC_fails[k]) failures++;
	}}
	new zones;
	for(new z = 0; z < MAX_GANG_ZONES; z++)
		if(IsValidGangZone(z) && IsGangZoneVisibleForPlayer(HC_probe, z)) zones++;
	printf("[check] gang zones visible to the player: %d of %d shown", zones, HC_calls[HC_ZONE]);
	if(zones != HC_calls[HC_ZONE] - HC_fails[HC_ZONE]) failures++;
	new playerZones = HC_playerZones(HC_probe);
	if(HC_calls[HC_PZONE])
	{{
		printf("[check] per-player gang zones visible to the player: %d of %d shown", playerZones,
			HC_calls[HC_PZONE] - HC_fails[HC_PZONE]);
		if(playerZones != HC_calls[HC_PZONE] - HC_fails[HC_PZONE]) failures++;
	}}
{zone_check}
	new removed = GetPlayerBuildingsRemoved(HC_probe);
	printf("[check] buildings removed for the player: %d of %d", removed, HC_calls[HC_REMOVE]);
	if(removed != HC_calls[HC_REMOVE]) failures++;
	new visible;
	for(new t = 0; t < HC_numTd; t++) if(IsTextDrawVisibleForPlayer(HC_probe, HC_td[t])) visible++;
	for(new t = 0; t < HC_numPtd; t++)
		if(IsValidPlayerTextDraw(HC_probe, HC_ptd[t]) && IsPlayerTextDrawVisible(HC_probe, HC_ptd[t])) visible++;
	printf("[check] textdraws visible to the player: %d of %d", visible, HC_numTd + HC_numPtd);
	if(visible != HC_numTd + HC_numPtd) failures++;
	new npcs;
	for(new i = 0; i < MAX_PLAYERS; i++) if(NPC_IsValid(i) && i != HC_probe) npcs++;
	printf("[check] map NPCs: %d of %d", npcs, HC_npcsCreated);
	if(npcs != HC_npcsCreated) failures++;
	// Walkers now; recordings later, as a recording may start standing still
	// (open.mp's trains wait about 10 s at the station).
	HC_failures = failures;
	HC_CheckMovers(false);
	SetTimer("HC_CheckRecordings", 15000, false);
}}

forward HC_CheckRecordings();
public HC_CheckRecordings()
{{
	HC_CheckMovers(true);
	printf("[check] done: %d failure(s)", HC_failures);
	SendRconCommand("exit");
}}

stock HC_CheckMovers(bool:recordings)
{{
	for(new m = 0; m < HC_numMovers; m++)
	{{
		new id = HC_movers[m], Float:x, Float:y, Float:z;
		if(HC_plays[id] != recordings) continue;
		HC_where(id, x, y, z);
		new Float:moved = VectorSize(x - HC_start[id][0], y - HC_start[id][1], z - HC_start[id][2]);
		// A recording plays; a path or the nodes are walked.
		new bool:active = HC_plays[id] ? NPC_IsPlayingPlayback(id) : NPC_IsMoving(id);
		new bool:seated = HC_vehicleOf[id] == -1 || NPC_GetVehicle(id) == HC_vehicleOf[id];
		printf("[check] NPC %d: %s, %s, moved %.2f m%s", id,
			HC_plays[id] ? (NPC_IsPlayingPlayback(id) ? "playing its recording" : "NOT playing its recording") :
				(NPC_IsMoving(id) ? "moving" : "NOT moving"),
			HC_vehicleOf[id] == -1 ? "on foot" : (seated ? "in its vehicle" : "NOT in its vehicle"),
			moved, HC_vehicleOf[id] == -1 ? "" : " (the vehicle)");
		if(!active || !seated || moved < 0.5) HC_failures++;
	}}
}}
{DEFINES}
#include "{map_path}"
'''


def img_entries(path):
	with open(path, 'rb') as f:
		if f.read(4) != b'VER2':
			return {}
		count, = struct.unpack('<I', f.read(4))
		entries = {}
		for _ in range(count):
			offset, size, size2, name = struct.unpack('<IHH24s', f.read(32))
			entries[name.split(b'\0')[0].decode().lower()] = (offset * 2048, (size or size2) * 2048)
		return entries


def find_ci(folder, name):
	folder = Path(folder)
	if not folder.is_dir():
		return None
	for entry in folder.iterdir():
		if entry.name.lower() == name.lower():
			return entry
	return None


def make_server(work, name, source, port):
	server = Path(work, name)
	server.mkdir(parents=True)
	for item in ('omp-server', 'components'):
		(server / item).symlink_to(SERVER / item)
	shutil.copytree(SERVER / 'npcmodes', server / 'npcmodes')
	(server / 'gamemodes').mkdir()
	(server / 'scriptfiles').mkdir()
	notes = []
	plugins = []
	if '#include <streamer>' in source:
		(server / 'plugins').mkdir()
		shutil.copy(STREAMER / 'streamer.so', server / 'plugins' / 'streamer.so')
		plugins.append('streamer')
	if GAME_DIR and 'NPC_PlayNode' in source:
		paths = find_ci(find_ci(GAME_DIR, 'data') or '', 'Paths')
		nodes = server / 'scriptfiles' / 'NPCs' / 'nodes'
		nodes.mkdir(parents=True)
		for i in range(64):
			found = find_ci(paths, f'NODES{i}.DAT') if paths else None
			if found:
				shutil.copy(found, nodes / f'NODES{i}.DAT')
	models = False
	if 'custom models' in source and GAME_DIR and (SHOWCASE / 'artconfig.txt').exists():
		img = Path(GAME_DIR, 'models', 'gta3.img')
		entries = img_entries(img) if img.is_file() else {}
		files = {'barrel4.dff': 'showbarrel.dff', 'dynbarrels.txd': 'showbarrel.txd'}
		if all(f in entries for f in files):
			(server / 'models').mkdir()
			with open(img, 'rb') as f:
				for src, dst in files.items():
					offset, size = entries[src]
					f.seek(offset)
					(server / 'models' / dst).write_bytes(f.read(size))
			shutil.copy(SHOWCASE / 'artconfig.txt', server / 'models' / 'artconfig.txt')
			models = True
	config = {
		'announce': False, 'max_players': 10, 'max_bots': 100, 'name': 'Haumea server check',
		'artwork': {'enable': models, 'models_path': 'models', 'port': port,
		            'web_server_bind': '127.0.0.1'},
		'logging': {'enable': True, 'file': 'log.txt'},
		'network': {'port': port, 'bind': '127.0.0.1', 'public_addr': '127.0.0.1'},
		'pawn': {'legacy_plugins': plugins, 'main_scripts': ['check 1'], 'side_scripts': []},
		'rcon': {'enable': False, 'password': ''},
	}
	(server / 'config.json').write_text(json.dumps(config, indent=1))
	return server, notes


def check_map(work, map_path, port):
	name = map_path.stem
	source = map_path.read_text(errors='replace')
	server, notes = make_server(work, name, source, port)
	gm = server / 'gamemodes' / 'check.pwn'
	gm.write_text(gamemode(map_path.resolve(), source))
	cc = subprocess.run([str(TOOLCHAIN / 'bin' / 'pawncc'), str(gm), f'-i{SERVER / "qawno" / "include"}',
	                     f'-i{STREAMER}', f'-o{server / "gamemodes" / "check.amx"}', '-d3', '-;+', '-(+'],
	                    capture_output=True, text=True)
	diagnostics = [l for l in (cc.stdout + cc.stderr).splitlines() if re.search(r': (fatal )?(error|warning) \d+', l)]
	if diagnostics or not (server / 'gamemodes' / 'check.amx').exists():
		return False, ['the check gamemode did not compile:'] + diagnostics[:10]
	try:
		run = subprocess.run(['./omp-server'], cwd=server, capture_output=True, text=True, timeout=60)
	except subprocess.TimeoutExpired:
		run = None
	log = (server / 'log.txt').read_text(errors='replace').splitlines() if (server / 'log.txt').exists() else []
	checks = [l.split('[check] ', 1)[1] for l in log if '[check] ' in l]
	problems = [l for l in log if '[Error]' in l or '[Warning]' in l]
	done = [c for c in checks if c.startswith('done:')]
	ok = bool(done) and done[0].startswith('done: 0 failure') and not problems
	details = []
	if not done:
		details.append('the check never finished')
		if run is not None:
			details.append(f'server exited with code {run.returncode}')
			details += [f'server: {line}' for line in (run.stderr + run.stdout).splitlines()[-10:]]
	return ok, notes + checks + [f'log: {p}' for p in problems] + details


def main():
	if not (SERVER / 'omp-server').exists() or not (STREAMER / 'streamer.so').exists():
		print(f'skipped: no open.mp server or streamer plugin in {TOOLCHAIN} (fetch_toolchain.sh)')
		return 2
	maps = [p for d in sys.argv[1:] for p in sorted(Path(d).glob('*.pwn'))]
	failed = 0
	with tempfile.TemporaryDirectory() as work:
		for i, map_path in enumerate(maps):
			ok, lines = check_map(work, map_path, 7900 + i)
			print(('ok   ' if ok else 'FAIL ') + map_path.stem)
			for line in lines:
				print('     ' + line)
			failed += not ok
	return 1 if failed else 0


if __name__ == '__main__':
	sys.exit(main())
