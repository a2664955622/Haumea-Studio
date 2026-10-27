// SPDX-License-Identifier: GPL-2.0-or-later
#include "haumea.h"
#include "vehicle_tables.h"

// SA-MP vehicle colours, tuning parts and paintjobs, and how the editor
// shows them on a vehicle's model.

uint32
GetSampVehicleColour(int index)
{
	return index >= 0 && index < 256 ? sampVehicleColours[index] : 0;
}

bool
IsVehicleComponentValid(int vehicleModel, int component)
{
	if((component == 1192 || component == 1193) && vehicleModel == 576)
		return true;
	int c = component - 1000;
	int m = vehicleModel - 400;
	if(c < 0 || c >= 192 || m < 0 || m >= 212)
		return false;
	return (sampComponentModels[m*6 + (c >> 5)] & (1u << (c & 31))) != 0;
}

int
GetVehicleComponentSlot(int component)
{
	int c = component - 1000;
	return c >= 0 && c < 194 ? sampComponentSlots[c] : -1;
}

const char*
GetVehicleComponentSlotName(int slot)
{
	static const char *names[VEHICLE_MOD_SLOTS] = {
		"Spoiler", "Hood", "Roof", "Side skirts", "Lamps", "Nitro", "Exhaust",
		"Wheels", "Stereo", "Hydraulics", "Front bumper", "Rear bumper",
		"Right vent", "Left vent", "Front bullbar", "Rear bullbar",
	};
	return slot >= 0 && slot < VEHICLE_MOD_SLOTS ? names[slot] : "";
}

// Paintjobs are <model>1.txd to <model>3.txd in the game's archives.
static int
findPaintjobTxd(int vehicleModel, int paintjob)
{
	ObjectDef *obj = GetObjectDef(vehicleModel);
	if(obj == nil || !obj->m_isVehicle || paintjob < 0 || paintjob > 2)
		return -1;
	char name[MODELNAMELEN + 4];
	snprintf(name, sizeof(name), "%s%d", obj->m_name, paintjob + 1);
	int slot = FindTxdSlot(name);
	TxdDef *txd = slot >= 0 ? GetTxdDef(slot) : nil;
	return txd && txd->imageIndex >= 0 ? slot : -1;
}

int
GetVehiclePaintjobCount(int vehicleModel)
{
	int n = 0;
	while(n < 3 && findPaintjobTxd(vehicleModel, n) >= 0)
		n++;
	return n;
}


// GTA Reversed CCustomCarPlateMgr::CreatePlateTexture copies eight 8x16
// glyphs from this 4-column charset into a 64x16 plate texture.
static void
editorVehiclePlateGlyph(unsigned char character, int *column, int *row)
{
	static const char *rows[] = {
		"ABCD", "EFGH", "IJKL", "MNOP", "QRST",
		"UVWX", "YZ01", "2345", "6789"
	};
	if(character >= 'a' && character <= 'z')
		character = (unsigned char)(character - 'a' + 'A');
	for(int y = 0; y < (int)(sizeof(rows)/sizeof(rows[0])); y++)
		for(int x = 0; x < 4; x++)
			if(rows[y][x] == character){
				*column = x;
				*row = y;
				return;
			}
	// GTA's final atlas row starts with the blank/fallback glyph.
	*column = 0;
	*row = 9;
}

static rw::Texture*
createEditorVehiclePlateTexture(rw::Texture *charset, const char *text)
{
	if(charset == nil || charset->raster == nil || text == nil)
		return nil;
	rw::Image *charsetImage = charset->raster->toImage();
	if(charsetImage == nil)
		return nil;
	if(charsetImage->depth != 32)
		charsetImage->convertTo32();
	if(charsetImage->pixels == nil || charsetImage->bpp != 4 ||
	   charsetImage->width < 32 || charsetImage->height < 160 ||
	   charsetImage->stride < charsetImage->width * 4){
		charsetImage->destroy();
		return nil;
	}

	rw::Image *plateImage = rw::Image::create(64, 16, 32);
	if(plateImage == nil){
		charsetImage->destroy();
		return nil;
	}
	plateImage->allocate();
	if(plateImage->pixels == nil){
		plateImage->destroy();
		charsetImage->destroy();
		return nil;
	}
	memset(plateImage->pixels, 0, plateImage->stride * plateImage->height);

	size_t textLength = strlen(text);
	for(int i = 0; i < 8; i++){
		int column, row;
		editorVehiclePlateGlyph(i < (int)textLength ? (unsigned char)text[i] : 0,
			&column, &row);
		for(int y = 0; y < 16; y++){
			const uint8 *source = charsetImage->pixels +
				(row * 16 + y) * charsetImage->stride + column * 8 * 4;
			uint8 *destination = plateImage->pixels +
				y * plateImage->stride + i * 8 * 4;
			memcpy(destination, source, 8 * 4);
		}
	}

	rw::Raster *raster = rw::Raster::createFromImage(plateImage);
	plateImage->destroy();
	charsetImage->destroy();
	if(raster == nil)
		return nil;
	rw::Texture *texture = rw::Texture::create(raster);
	if(texture == nil){
		raster->destroy();
		return nil;
	}
	strncpy(texture->name, "carplate", sizeof(texture->name));
	texture->filterAddressing = (rw::Texture::CLAMP << 12) |
		(rw::Texture::CLAMP << 8) | rw::Texture::NEAREST;
	return texture;
}

void
ApplyEditorVehiclePlate(rw::Clump *clump, const char *plateText)
{
	if(clump == nil || plateText == nil || plateText[0] == '\0')
		return;
	int vehicleTxdSlot = FindTxdSlot("vehicle");
	if(vehicleTxdSlot < 0)
		return;
	if(!IsTxdLoaded(vehicleTxdSlot))
		LoadTxd(vehicleTxdSlot);
	rw::Texture *charset = FindTextureInTxd(vehicleTxdSlot, "platecharset");
	rw::Texture *plateback = FindTextureInTxd(vehicleTxdSlot, "plateback1");
	rw::Texture *plateTexture = createEditorVehiclePlateTexture(charset, plateText);

	FORLIST(lnk, clump->atomics){
		rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
		if(atomic == nil || atomic->geometry == nil)
			continue;
		rw::Geometry *geometry = atomic->geometry;
		for(int i = 0; i < geometry->matList.numMaterials; i++){
			rw::Material *material = geometry->matList.materials[i];
			if(material == nil || material->texture == nil)
				continue;
			const char *name = material->texture->name;
			if(plateTexture && rw::strcmp_ci(name, "carplate") == 0)
				material->setTexture(plateTexture);
			else if(plateback &&
			   (rw::strcmp_ci(name, "carpback") == 0 ||
			    rw::strcmp_ci(name, "plateback1") == 0)){
				// GTA Reversed uses plateback1 for the default/SF region.
				// The editor has no active game weather region to select another.
				material->setTexture(plateback);
			}
		}
	}
	if(plateTexture)
		plateTexture->destroy();
}

static rw::Frame*
findFrameCB(rw::Frame *frame, void *data)
{
	void **find = (void**)data;
	if(find[1])
		return nil;
	const char *name = gta::getNodeName(frame);
	if(name && rw::strcmp_ci(name, (const char*)find[0]) == 0){
		find[1] = frame;
		return nil;
	}
	frame->forAllChildren(findFrameCB, data);
	return frame;
}

static rw::Frame*
findFrame(rw::Clump *clump, const char *name)
{
	void *find[2] = { (void*)name, nil };
	rw::Frame *root = clump->getFrame();
	if(root)
		findFrameCB(root, find);
	return (rw::Frame*)find[1];
}

static rw::Atomic*
getComponentAtomic(int component)
{
	ObjectDef *obj = GetObjectDef(component);
	if(obj == nil || obj->m_type != ObjectDef::ATOMIC)
		return nil;
	if(!obj->IsLoaded()){
		RequestObject(component);
		LoadAllRequestedObjects();
	}
	return obj->m_atomics[0];
}

static void
hideFrameAtomics(rw::Clump *clump, rw::Frame *frame)
{
	FORLIST(lnk, clump->atomics){
		rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
		if(atomic && atomic->getFrame() == frame)
			atomic->setFlags(0);
	}
}

static void
attachComponent(rw::Clump *clump, int component, const char *frameName, const char *hideName)
{
	rw::Frame *parent = findFrame(clump, frameName);
	rw::Atomic *source = getComponentAtomic(component);
	if(parent == nil || source == nil)
		return;
	if(hideName){
		rw::Frame *hide = findFrame(clump, hideName);
		if(hide)
			hideFrameAtomics(clump, hide);
	}
	rw::Atomic *atomic = source->clone();
	if(atomic == nil)
		return;
	rw::Frame *frame = rw::Frame::create();
	parent->addChild(frame);
	atomic->setFrame(frame);
	clump->addAtomic(atomic);
}

// The game links left side skirts and vents to their right-hand twin.
static int
findLinkedComponent(int component)
{
	ObjectDef *obj = GetObjectDef(component);
	if(obj == nil)
		return -1;
	char name[MODELNAMELEN];
	snprintf(name, sizeof(name), "%s", obj->m_name);
	if(strncmp(name, "wg_l_", 5) == 0)
		name[3] = 'r';
	else if(strncmp(name, "bntl_", 5) == 0)
		name[3] = 'r';
	else
		return -1;
	int id = -1;
	return GetObjectDef(name, &id) ? id : -1;
}

void
AddEditorVehicleComponents(rw::Clump *clump, const uint16 *mods)
{
	if(clump == nil || mods == nil)
		return;
	for(int slot = 0; slot < VEHICLE_MOD_SLOTS; slot++){
		int component = mods[slot];
		if(component == 0)
			continue;
		switch(slot){
		case 0: attachComponent(clump, component, "ug_spoiler", nil); break;
		case 1: attachComponent(clump, component, "ug_bonnet", nil); break;
		case 2: attachComponent(clump, component, "ug_roof", nil); break;
		case 3:
		case 12:
		case 13: {
			// A part and its twin go on both sides.
			const char *left = slot == 3 ? "ug_wing_left" : "ug_bonnet_left";
			const char *right = slot == 3 ? "ug_wing_right" : "ug_bonnet_right";
			ObjectDef *obj = GetObjectDef(component);
			bool isRight = obj && (strncmp(obj->m_name, "wg_r_", 5) == 0 || strncmp(obj->m_name, "bntr_", 5) == 0);
			attachComponent(clump, component, isRight ? right : left, nil);
			int twin = isRight ? -1 : findLinkedComponent(component);
			if(twin >= 0)
				attachComponent(clump, twin, right, nil);
			break;
		}
		case 4: attachComponent(clump, component, "ug_lights", nil); break;
		case 5: attachComponent(clump, component, "ug_nitro", nil); break;
		case 6: attachComponent(clump, component, "exhaust_ok", "exhaust_ok"); break;
		case 7: {
			// Wheels swap the model of the four wheels.
			rw::Atomic *source = getComponentAtomic(component);
			if(source == nil)
				break;
			static const char *wheels[] = { "wheel_lf_dummy", "wheel_rf_dummy", "wheel_lb_dummy", "wheel_rb_dummy" };
			for(int w = 0; w < 4; w++){
				rw::Frame *frame = findFrame(clump, wheels[w]);
				if(frame == nil)
					continue;
				FORLIST(lnk, clump->atomics){
					rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
					if(atomic && atomic->getFrame() == frame)
						atomic->setGeometry(source->geometry, 0);
				}
			}
			break;
		}
		case 10: attachComponent(clump, component, "bump_front_dummy", "bump_front_ok"); break;
		case 11: attachComponent(clump, component, "bump_rear_dummy", "bump_rear_ok"); break;
		case 14: attachComponent(clump, component, "ug_frontbullbar", nil); break;
		case 15: attachComponent(clump, component, "ug_backbullbar", nil); break;
		default: break;	// stereo and hydraulics have no model
		}
	}
}

// A paintjob replaces the vehicle's remap texture ("remap..." in its TXD).
void
ApplyEditorVehiclePaintjob(rw::Clump *clump, int vehicleModel, int paintjob)
{
	if(clump == nil || paintjob <= 0)
		return;
	int slot = findPaintjobTxd(vehicleModel, paintjob - 1);
	if(slot < 0)
		return;
	if(!IsTxdLoaded(slot))
		LoadTxd(slot);
	rw::Texture *tex = GetTxdTextureCount(slot) > 0 ? GetTxdTexture(slot, 0) : nil;
	if(tex == nil)
		return;
	FORLIST(lnk, clump->atomics){
		rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
		if(atomic == nil || atomic->geometry == nil)
			continue;
		rw::Geometry *geo = atomic->geometry;
		for(int i = 0; i < geo->matList.numMaterials; i++){
			rw::Material *mat = geo->matList.materials[i];
			if(mat == nil || mat->texture == nil || rw::strncmp_ci(mat->texture->name, "remap", 5) != 0)
				continue;
			tex->refCount++;
			mat->texture->destroy();
			mat->texture = tex;
		}
	}
}
