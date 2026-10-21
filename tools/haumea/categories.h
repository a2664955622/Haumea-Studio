#pragma once

// The Object Browser's categories (categories.cpp): Haumea's own, filed at
// load from the game's files. CAT(id, name, parent): parents come first.
#define CATEGORY_LIST \
	CAT(CAT_NATURE_ROOT, "Nature", -1) \
	CAT(CAT_TREES, "Trees", CAT_NATURE_ROOT) \
	CAT(CAT_PLANTS, "Bushes and plants", CAT_NATURE_ROOT) \
	CAT(CAT_GRASS, "Grass", CAT_NATURE_ROOT) \
	CAT(CAT_ROCKS, "Rocks and cliffs", CAT_NATURE_ROOT) \
	CAT(CAT_BUILDINGS_ROOT, "Buildings and land", -1) \
	CAT(CAT_HOUSES, "Houses", CAT_BUILDINGS_ROOT) \
	CAT(CAT_SHOPS, "Shops, offices and public buildings", CAT_BUILDINGS_ROOT) \
	CAT(CAT_TOWERS, "Towers", CAT_BUILDINGS_ROOT) \
	CAT(CAT_INDUSTRIAL_BUILDINGS, "Sheds, barns and warehouses", CAT_BUILDINGS_ROOT) \
	CAT(CAT_IN_LS, "Los Santos", CAT_BUILDINGS_ROOT) \
	CAT(CAT_IN_SF, "San Fierro", CAT_BUILDINGS_ROOT) \
	CAT(CAT_IN_LV, "Las Venturas", CAT_BUILDINGS_ROOT) \
	CAT(CAT_IN_COUNTRY, "Countryside", CAT_BUILDINGS_ROOT) \
	CAT(CAT_GROUND, "Ground and seabed", CAT_BUILDINGS_ROOT) \
	CAT(CAT_TRANSPORT_ROOT, "Roads, railways and airports", -1) \
	CAT(CAT_ROADS, "Roads", CAT_TRANSPORT_ROOT) \
	CAT(CAT_BRIDGES, "Bridges", CAT_TRANSPORT_ROOT) \
	CAT(CAT_TUNNELS, "Tunnels", CAT_TRANSPORT_ROOT) \
	CAT(CAT_RAILWAYS, "Railways", CAT_TRANSPORT_ROOT) \
	CAT(CAT_CAR_PARKS, "Car parks", CAT_TRANSPORT_ROOT) \
	CAT(CAT_AIRPORTS, "Airports and helipads", CAT_TRANSPORT_ROOT) \
	CAT(CAT_STREET_ROOT, "Street objects", -1) \
	CAT(CAT_LIGHTS, "Lights", CAT_STREET_ROOT) \
	CAT(CAT_SIGNS, "Signs and statues", CAT_STREET_ROOT) \
	CAT(CAT_FENCES, "Fences, walls and barriers", CAT_STREET_ROOT) \
	CAT(CAT_STREET_FURNITURE, "Benches, bins and street furniture", CAT_STREET_ROOT) \
	CAT(CAT_INDUSTRY, "Industry and construction", -1) \
	CAT(CAT_WATER, "Water and coast", -1) \
	CAT(CAT_PROPS_ROOT, "Props", -1) \
	CAT(CAT_BOXES, "Boxes and crates", CAT_PROPS_ROOT) \
	CAT(CAT_BARRELS, "Barrels and containers", CAT_PROPS_ROOT) \
	CAT(CAT_FOOD, "Food and drink", CAT_PROPS_ROOT) \
	CAT(CAT_WEAPON_PROPS, "Weapons", CAT_PROPS_ROOT) \
	CAT(CAT_SPORTS, "Sports and play", CAT_PROPS_ROOT) \
	CAT(CAT_OTHER_PROPS, "Other props", CAT_PROPS_ROOT) \
	CAT(CAT_INTERIORS_ROOT, "Interiors", -1) \
	CAT(CAT_FURNITURE, "Furniture", CAT_INTERIORS_ROOT) \
	CAT(CAT_KITCHEN, "Kitchens and bathrooms", CAT_INTERIORS_ROOT) \
	CAT(CAT_SHOP_FITTINGS, "Shop, bar and casino fittings", CAT_INTERIORS_ROOT) \
	CAT(CAT_DOORS, "Doors and windows", CAT_INTERIORS_ROOT) \
	CAT(CAT_OTHER_INTERIOR, "Rooms and other interior objects", CAT_INTERIORS_ROOT) \
	CAT(CAT_VEHICLES_ROOT, "Vehicles", -1) \
	CAT(CAT_CARS, "Cars", CAT_VEHICLES_ROOT) \
	CAT(CAT_WORK_VEHICLES, "Vans, trucks and work vehicles", CAT_VEHICLES_ROOT) \
	CAT(CAT_EMERGENCY, "Police, ambulance and fire", CAT_VEHICLES_ROOT) \
	CAT(CAT_MOTORBIKES, "Motorbikes and quads", CAT_VEHICLES_ROOT) \
	CAT(CAT_BICYCLES, "Bicycles", CAT_VEHICLES_ROOT) \
	CAT(CAT_BOATS, "Boats", CAT_VEHICLES_ROOT) \
	CAT(CAT_PLANES, "Planes", CAT_VEHICLES_ROOT) \
	CAT(CAT_HELICOPTERS, "Helicopters", CAT_VEHICLES_ROOT) \
	CAT(CAT_TRAINS, "Trains and trams", CAT_VEHICLES_ROOT) \
	CAT(CAT_TRAILERS, "Trailers", CAT_VEHICLES_ROOT) \
	CAT(CAT_RC, "Remote control", CAT_VEHICLES_ROOT) \
	CAT(CAT_VEHICLE_PARTS, "Vehicle parts", -1) \
	CAT(CAT_PEOPLE, "People (skins)", -1) \
	CAT(CAT_NIGHT, "Night-time objects", -1) \
	CAT(CAT_LOD, "Far versions (LOD)", -1) \
	CAT(CAT_SAMP_ROOT, "SA-MP objects", -1) \
	CAT(CAT_SAMP_BLOCKS, "Building blocks", CAT_SAMP_ROOT) \
	CAT(CAT_SAMP_TEXT, "Text and materials", CAT_SAMP_ROOT) \
	CAT(CAT_SAMP_LIGHTS, "Lights", CAT_SAMP_ROOT) \
	CAT(CAT_SAMP_EFFECTS, "Effects and particles", CAT_SAMP_ROOT) \
	CAT(CAT_SAMP_WEARABLES, "Wearable items", CAT_SAMP_ROOT) \
	CAT(CAT_SAMP_MARKERS, "Markers and boundaries", CAT_SAMP_ROOT) \
	CAT(CAT_SAMP_NATURE, "Nature", CAT_SAMP_ROOT) \
	CAT(CAT_SAMP_ROADS, "Roads and railways", CAT_SAMP_ROOT) \
	CAT(CAT_SAMP_BUILDINGS, "Buildings", CAT_SAMP_ROOT) \
	CAT(CAT_SAMP_STREET, "Street objects", CAT_SAMP_ROOT) \
	CAT(CAT_SAMP_INTERIORS, "Interiors", CAT_SAMP_ROOT) \
	CAT(CAT_SAMP_PROPS, "Props", CAT_SAMP_ROOT) \
	CAT(CAT_SAMP_OTHER, "Other SA-MP objects", CAT_SAMP_ROOT) \
	CAT(CAT_CUSTOM, "Custom models", -1) \
	CAT(CAT_UNSORTED, "Not sorted yet", -1)

enum ObjectCategoryId {
#define CAT(id, name, parent) id,
	CATEGORY_LIST
#undef CAT
};

struct ObjCategory {
	const char *name;
	int parent;	// -1 for top-level
};
extern const ObjCategory objCategories[];
extern const int NUM_OBJ_CATEGORIES;

#define SAMP_CATEGORY_ROOT CAT_SAMP_ROOT
#define SAMP_CATEGORY_UNCAT CAT_SAMP_OTHER
#define CUSTOM_MODELS_CATEGORY CAT_CUSTOM

// The category rules for one model (SA-MP's models get their own branch).
int ClassifyObjectCategory(int id, bool samp);
// --dump-categories: id, name, TXD, IDE file and category path of every model
// (tab-separated); returns how many.
int DumpObjectCategories(const char *path);
