#!/bin/sh
# mkgm.sh map.pwn out.pwn [openmp|samp037]: a gamemode that loads a Haumea map
# and calls its functions, so the compiler checks every generated call.
set -eu
map=$(realpath "$1"); out=$2; target=${3:-openmp}
prefix=$(grep -o 'stock [A-Za-z0-9_]*_CreateObjects' "$map" | sed 's/stock //; s/_CreateObjects//' | head -n 1)
npc=$(grep -q 'NPC_Create(' "$map" && echo 1 || true)
{
# open.mp first: it also silences the a_samp wrapper's warning.
[ "$target" = openmp ] && echo "#include <open.mp>"
echo "#include \"$map\""
echo
echo "main() {}"
echo
echo "public OnGameModeInit()"
echo "{"
echo "	${prefix}_CreateObjects();"
[ -n "$npc" ] && echo "	SetTimer(\"HV_CheckNPCs\", 3000, false);"
echo "	return 1;"
echo "}"
echo
echo "public OnPlayerConnect(playerid)"
echo "{"
grep -q "${prefix}_RemoveWorldObjectsForPlayer" "$map" && echo "	${prefix}_RemoveWorldObjectsForPlayer(playerid);"
grep -q "${prefix}_ShowForPlayer" "$map" && echo "	${prefix}_ShowForPlayer(playerid);"
echo "	return 1;"
echo "}"
if grep -q "${prefix}_ZoneArea" "$map"; then
	echo
	echo "public OnPlayerSpawn(playerid)"
	echo "{"
	echo "	${prefix}_ZoneUpdate(playerid);"
	echo "	return 1;"
	echo "}"
	echo
	echo "public OnPlayerEnterDynamicArea(playerid, STREAMER_TAG_AREA:areaid)"
	echo "{"
	echo "	${prefix}_ZoneArea(playerid, areaid);"
	echo "	return 1;"
	echo "}"
	echo
	echo "public OnPlayerLeaveDynamicArea(playerid, STREAMER_TAG_AREA:areaid)"
	echo "{"
	echo "	${prefix}_ZoneArea(playerid, areaid);"
	echo "	return 1;"
	echo "}"
fi
if grep -q "${prefix}_OnNPCFinishMovePath" "$map"; then
	echo
	echo "public OnNPCFinishMovePath(npcid, pathid)"
	echo "{"
	echo "	${prefix}_OnNPCFinishMovePath(npcid, pathid);"
	echo "	return 1;"
	echo "}"
fi
if [ -n "$npc" ]; then
cat <<'EON'

public OnNPCSpawn(npcid)
{
	return 1;
}

forward HV_CheckNPCs();
public HV_CheckNPCs()
{
	for(new i = 0; i < MAX_PLAYERS; i++)
		if(NPC_IsValid(i))
			printf("NPC %d valid", i);
}
EON
fi
} > "$out"
