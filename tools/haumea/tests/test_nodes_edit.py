"""Compile the Nodes tool's editing code from sapaths.cpp against a minimal
fixture and check that adding, linking and deleting nodes keeps NODES*.DAT
consistent, the way open.mp reads it."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
# The node files to edit: HAUMEA_NODES_DIR, else the game folder's; without
# either the test makes synthetic ones with the same code (write_synthetic_nodes).
GAME_PATHS = Path(os.environ.get('HAUMEA_NODES_DIR') or
                  (Path(os.environ['HAUMEA_GAME_DIR'], 'data', 'Paths') if os.environ.get('HAUMEA_GAME_DIR')
                   else os.path.expanduser('~/Games/gta-sa-wow64/drive_c/Program Files (x86)/'
                                           'Rockstar Games/GTA San Andreas/data/Paths')))


def between(source, start, end):
    a = source.index(start)
    return source[a:source.index(end, a)]


PRELUDE = r'''
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
typedef unsigned char uint8; typedef signed char int8;
typedef unsigned long long uint64;
typedef unsigned short uint16; typedef short int16;
typedef unsigned int uint32; typedef int int32;
#define nil nullptr
template <typename T> T min(T a, T b) { return a < b ? a : b; }
template <typename T> T max(T a, T b) { return a > b ? a : b; }
template <typename T> T clamp(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }
namespace rw { struct V3d { float x, y, z; }; struct V2d { float x, y; }; }
static unsigned gSeq;
unsigned BumpChangeSeq(void) { return ++gSeq; }
// Undo for node edits (UpdateNodesUndo) only needs these from the editor.
namespace ImGui { static bool IsAnyItemActive(void) { return false; } }
static bool gGizmoUsing;
void UndoRecordNodes(const std::vector<int> &, const std::vector<std::shared_ptr<void>> &,
	const std::vector<std::shared_ptr<void>> &) {}
namespace SAPaths {
static const int NUM_PATH_AREAS = 64;
static const int NUM_DYNAMIC_LINK_SLOTS = 16*12;
static const float PATH_POS_SCALE = 1.0f/8.0f;
static const float PATH_WIDTH_SCALE = 1.0f/16.0f;
static const float NAVI_POS_SCALE = 1.0f/8.0f;
static const float AREA_SIZE = 750.0f;
'''

MIDDLE = r'''
static AreaData gAreas[NUM_PATH_AREAS];
static AreaData gUndoBaseline[NUM_PATH_AREAS];
static bool gUndoBaselineValid;
static uint32 gNodesEditSerial, gNodesRecordedSerial;
static bool gLoadSucceeded = true;
static int gSelectedLinkIndex = -1;
static char gFolderSourceDir[1024];
Node *hoveredNode, *selectedNode;
static void EnsureLoaded(void) {}
static void clearPreviewWalkers(void) {}
static void clearPreviewTrafficCars(void) {}
static void clearPreviewParkedCars(void) {}
static void refreshMetadataForNode(const NodeAddress &) {}
static bool findReciprocalLinkSlot(const NodeAddress &, const NodeAddress &, uint16 *, int *) { return false; }
'''

TEST = r'''
using namespace SAPaths;

static int fails;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } }while(0)

static std::vector<uint8> readFile(const std::string &p)
{
	std::vector<uint8> d; FILE *f = fopen(p.c_str(), "rb"); if(!f) return d;
	uint8 b[65536]; size_t n; while((n = fread(b, 1, sizeof(b), f)) > 0) d.insert(d.end(), b, b+n);
	fclose(f); return d;
}

// What open.mp relies on, plus the game's layout.
static void checkArea(int ai)
{
	AreaData &a = gAreas[ai];
	CHECK(a.numNodes == a.nodes.size());
	CHECK(a.numNodes == a.numVehicleNodes + a.numPedNodes);
	CHECK(a.numCarPathLinks == a.naviNodes.size());
	if(a.numAddresses){
		CHECK(a.nodeLinks.size() == a.numAddresses + NUM_DYNAMIC_LINK_SLOTS);
		CHECK(a.linkLengths.size() == a.numAddresses + NUM_DYNAMIC_LINK_SLOTS);
		CHECK(a.intersections.size() == a.numAddresses + NUM_DYNAMIC_LINK_SLOTS);
		CHECK(a.naviLinks.size() == a.numAddresses);
	}
	std::vector<int> owner(a.numAddresses, -1);
	for(uint32 i = 0; i < a.nodes.size(); i++){
		Node &n = a.nodes[i];
		CHECK(n.raw.nodeId == i && n.raw.areaId == ai && n.index == i);
		for(int li = 0; li < n.numLinks(); li++){
			int s = n.raw.baseLinkId + li;
			CHECK(s >= 0 && s < (int)a.numAddresses);
			if(s < 0 || s >= (int)a.numAddresses) continue;
			CHECK(owner[s] == -1);
			owner[s] = i;
			Node *t = getNode(a.nodeLinks[s]);
			CHECK(t != nil);
			if(t) CHECK(isVehicleNode(t) == (i < a.numVehicleNodes));
		}
	}
	for(uint32 s = 0; s < a.numAddresses; s++)
		CHECK(owner[s] != -1);
	for(uint32 s = 0; s < a.naviLinks.size(); s++){
		CarPathLinkAddress n = a.naviLinks[s];
		if(n.isValid()) CHECK(n.nodeId() < gAreas[n.areaId()].naviNodes.size());
	}
	for(auto &nv : a.naviNodes)
		if(nv.attachedNodeId != 0xFFFF) CHECK(getNode({ nv.attachedAreaId, nv.attachedNodeId }) != nil);
}

static void checkAll(void) { for(int i = 0; i < NUM_PATH_AREAS; i++) checkArea(i); }

int main(int argc, char **argv)
{
	std::string dir = argv[1];
	for(int i = 0; i < NUM_PATH_AREAS; i++){
		std::vector<uint8> d = readFile(dir + "/NODES" + std::to_string(i) + ".DAT");
		char reason[64];
		if(!parseAreaData(i, d, "x", nil, &gAreas[i], nil, reason, sizeof(reason))){
			printf("FAIL parse %d %s\n", i, reason); return 1;
		}
		std::vector<uint8> out;
		buildAreaFileData(gAreas[i], out);
		CHECK(out.size() <= d.size() && memcmp(out.data(), d.data(), out.size()) == 0);
	}
	checkAll();

	// Add a vehicle node in an area that has ped nodes after its vehicle nodes.
	int ai = 36;
	AreaData &a = gAreas[ai];
	uint32 veh = a.numVehicleNodes, peds = a.numPedNodes;
	Node *firstPed = &a.nodes[veh];
	rw::V3d pedPos = getNodePosition(*firstPed);
	rw::V3d at = getNodePosition(a.nodes[0]);
	at.x += 1.0f;

	const char *why = nil;
	Node *added = AddNode(at, true, &gAreas[ai].nodes[0], &why);
	CHECK(added != nil && why == nil);
	CHECK(gAreas[ai].numVehicleNodes == veh + 1 && gAreas[ai].numPedNodes == peds);
	CHECK(added && added->index == veh && isVehicleNode(added));
	CHECK(added && AreNodesLinked(added, &gAreas[ai].nodes[0]));
	// The first ped node moved to the next point id and kept its place.
	rw::V3d moved = getNodePosition(gAreas[ai].nodes[veh + 1]);
	CHECK(moved.x == pedPos.x && moved.y == pedPos.y && moved.z == pedPos.z);
	checkAll();

	// A ped node can't link to a vehicle node.
	Node *ped = AddNode(pedPos, false, nil, &why);
	CHECK(ped != nil);
	CHECK(!LinkNodes(ped, &gAreas[ai].nodes[0], &why));
	CHECK(gAreas[ai].nodes.back().index == gAreas[ai].numNodes - 1);
	checkAll();

	// Chain three, unlink the middle, delete them all again.
	Node *n1 = AddNode({ at.x + 5, at.y, at.z }, true, nil, &why);
	NodeAddress a1 = n1->address();
	Node *n2 = AddNode({ at.x + 10, at.y, at.z }, true, getNode(a1), &why);
	NodeAddress a2 = n2->address();
	CHECK(AreNodesLinked(getNode(a1), getNode(a2)));
	CHECK(UnlinkNodes(getNode(a1), getNode(a2)));
	CHECK(!AreNodesLinked(getNode(a1), getNode(a2)));
	checkAll();

	// Delete real nodes with links, cross-area links and navis, everywhere.
	int deleted = 0;
	for(int k = 0; k < 64; k++){
		AreaData &b = gAreas[k];
		if(b.nodes.size() < 3) continue;
		CHECK(DeleteNode(&b.nodes[b.nodes.size() / 2]));
		CHECK(DeleteNode(&b.nodes[0]));
		deleted += 2;
	}
	checkAll();
	CHECK(deleted > 100);

	// Saved files read back the same.
	for(int i = 0; i < NUM_PATH_AREAS; i++){
		std::vector<uint8> out;
		buildAreaFileData(gAreas[i], out);
		AreaData back;
		char reason[64];
		CHECK(parseAreaData(i, out, "x", nil, &back, nil, reason, sizeof(reason)));
		std::vector<uint8> again;
		buildAreaFileData(back, again);
		CHECK(out == again);
	}
	printf(fails ? "FAILED %d\n" : "OK\n", fails);
	return fails ? 1 : 0;
}
'''


def nodes_source():
    """The Nodes tool's editing code from sapaths.cpp, with its stubs."""
    source = (ROOT / 'tools/haumea/sapaths.cpp').read_text()
    code = PRELUDE
    code += between(source, 'struct NodeAddress {', 'enum LoadSourceMode')
    code += MIDDLE
    code += between(source, 'static bool\nreadExact', 'static void\ncopyPreviewString')
    code += between(source, 'static AreaData*\ngetArea', 'static bool\nareaInDrawRange')
    code += between(source, 'static rw::V3d\ngetNodePosition', 'static rw::V3d\ngetPedWanderOffset')
    code += between(source, 'static bool\nparseAreaData', 'static bool\nloadAreaFromSource')
    code += between(source, 'static void\nbuildAreaFileData', 'static void\ndrawNodeSphere')
    code += between(source, '// ---- Editing for the open.mp Nodes tool', '// Absolute, with forward slashes')
    code = code.replace('static int getNaviOppositeLanes(const DiskCarPathLink &link);\n', '')
    code = code.replace('static int getNaviSameLanes(const DiskCarPathLink &link);\n', '')
    return code


GENERATE = r'''
using namespace SAPaths;

// Every area gets a chain of vehicle nodes, then ped nodes, and a link to
// the next area: what the test needs (ped nodes after vehicle nodes,
// cross-area links), made by the editor's own code.
int main(int argc, char **argv)
{
	std::string dir = argv[1];
	std::vector<uint8> empty(5 * sizeof(uint32), 0);
	char reason[64];
	for(int i = 0; i < NUM_PATH_AREAS; i++)
		if(!parseAreaData(i, empty, "x", nil, &gAreas[i], nil, reason, sizeof(reason))){
			printf("empty area %d: %s\n", i, reason);
			return 1;
		}
	const char *why = nil;
	std::vector<NodeAddress> first, last;
	for(int k = 0; k < NUM_PATH_AREAS; k++){
		float x = -3000.0f + 750.0f * (k % 8) + 375.0f, y = -3000.0f + 750.0f * (k / 8) + 375.0f;
		Node *n = AddNode({ x, y, 10.0f }, true, nil, &why);
		if(n == nil){ printf("add: %s\n", why); return 1; }
		first.push_back(n->address());
		NodeAddress prev = n->address();
		for(int j = 1; j < 4; j++){
			n = AddNode({ x + 20.0f * j, y, 10.0f }, true, getNode(prev), &why);
			if(n == nil){ printf("add: %s\n", why); return 1; }
			prev = n->address();
		}
		last.push_back(prev);
		Node *p = AddNode({ x, y + 20.0f, 10.0f }, false, nil, &why);
		NodeAddress pa = p->address();
		for(int j = 1; j < 3; j++){
			p = AddNode({ x + 15.0f * j, y + 20.0f, 10.0f }, false, getNode(pa), &why);
			pa = p->address();
		}
	}
	for(int k = 0; k + 1 < NUM_PATH_AREAS; k++)
		if(!LinkNodes(getNode(last[k]), getNode(first[k + 1]), &why)){
			printf("link %d: %s\n", k, why);
			return 1;
		}
	for(int i = 0; i < NUM_PATH_AREAS; i++){
		std::vector<uint8> out;
		buildAreaFileData(gAreas[i], out);
		std::string path = dir + "/NODES" + std::to_string(i) + ".DAT";
		FILE *f = fopen(path.c_str(), "wb");
		if(f == nil || fwrite(out.data(), 1, out.size(), f) != out.size()){ printf("write %s\n", path.c_str()); return 1; }
		fclose(f);
	}
	return 0;
}
'''


def write_synthetic_nodes(directory):
    """Write NODES0-63.DAT made by the Nodes code itself into directory."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / 'nodes_gen.cpp'
        exe = Path(tmp) / 'nodes_gen'
        src.write_text(nodes_source() + '}\n' + GENERATE)
        subprocess.run(['g++', '-std=c++14', '-O1', '-o', str(exe), str(src)], check=True)
        result = subprocess.run([str(exe), str(directory)], capture_output=True, text=True)
        if result.returncode != 0:
            raise RuntimeError('synthetic nodes: ' + result.stdout + result.stderr)


class NodesEditTests(unittest.TestCase):
    def test_add_link_delete_keep_files_consistent(self):
        code = nodes_source()
        code += '}\n' + TEST
        with tempfile.TemporaryDirectory() as tmp:
            paths = GAME_PATHS
            if not (paths / 'NODES0.DAT').exists():
                paths = Path(tmp, 'Paths')
                write_synthetic_nodes(paths)
            src = Path(tmp) / 'nodes_test.cpp'
            exe = Path(tmp) / 'nodes_test'
            src.write_text(code)
            subprocess.run(['g++', '-std=c++14', '-O1', '-o', str(exe), str(src)], check=True)
            result = subprocess.run([str(exe), str(paths)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
