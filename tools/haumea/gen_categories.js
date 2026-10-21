#!/usr/bin/env node
//
// gen_categories.js
// Reads mta_objects.xml and mta_vehicles.xml, generates object_categories.h
//

const fs = require('fs');
const path = require('path');

const dir = __dirname;
const objectsXml = fs.readFileSync(path.join(dir, 'mta_objects.xml'), 'utf-8');
const vehiclesXml = fs.readFileSync(path.join(dir, 'mta_vehicles.xml'), 'utf-8');

// Categories: { name, parent } indexed by category index
const categories = [];
// Mapping: modelId -> categoryIndex (first occurrence wins)
const modelMap = new Map();

function escapeStr(s) {
	return s.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
}

function parseXml(xml, isVehicles) {
	const lines = xml.split('\n');
	// Stack of group category indices
	const groupStack = [];

	// For vehicles, we wrap everything in a top-level "Vehicles" category
	let vehiclesTopIdx = -1;
	if (isVehicles) {
		vehiclesTopIdx = categories.length;
		categories.push({ name: 'Vehicles', parent: -1 });
	}

	for (const line of lines) {
		// Check for <group name="...">
		const groupMatch = line.match(/<group\s+name="([^"]*)"/);
		if (groupMatch) {
			const name = groupMatch[1];
			let parentIdx;
			if (groupStack.length === 0) {
				if (isVehicles) {
					// Top-level vehicle groups are children of "Vehicles"
					parentIdx = vehiclesTopIdx;
				} else {
					parentIdx = -1;
				}
			} else {
				parentIdx = groupStack[groupStack.length - 1];
			}
			const idx = categories.length;
			categories.push({ name: name, parent: parentIdx });
			groupStack.push(idx);
			continue;
		}

		// Check for </group>
		if (/<\/group>/.test(line)) {
			groupStack.pop();
			continue;
		}

		// Check for <object ...> or <vehicle ...>
		const objMatch = line.match(/<(?:object|vehicle)\s+model="(\d+)"/);
		if (objMatch) {
			const modelId = parseInt(objMatch[1], 10);
			// Assign to the innermost group
			if (groupStack.length > 0 && !modelMap.has(modelId)) {
				modelMap.set(modelId, groupStack[groupStack.length - 1]);
			}
			continue;
		}
	}
}

// Parse objects first, then vehicles
parseXml(objectsXml, false);
parseXml(vehiclesXml, true);

// Ped skins are filed here at load time, like vehicles.
categories.push({ name: 'Skins', parent: -1 });

// Keep SA-MP definitions in their own browser group. These categories are
// appended so the generated GTA category indices remain stable.
const sampRootCategory = categories.length;
categories.push({ name: 'SA-MP', parent: -1 });
const sampUncatCategory = categories.length;
categories.push({ name: 'Uncategorized', parent: sampRootCategory });

// SA-MP models are filed under the SA-MP group following the categories of
// Prineside's model ID list (crawl_prineside.py), each in the deepest
// category that lists it.
// The data file is not kept in the repository; without it SA-MP models stay
// in SA-MP / Uncategorized.
const prinesidePath = path.join(dir, 'prineside_categories.json');
const prineside = fs.existsSync(prinesidePath)
	? JSON.parse(fs.readFileSync(prinesidePath, 'utf-8'))
	: { source: '', crawled: '', categories: [], samp: [] };
const sampIds = new Set(prineside.samp);
const bySlug = new Map(prineside.categories.map(c => [c.slug, c]));
const members = new Map(prineside.categories.map(c => [c.slug, new Set(c.models)]));
function childrenOf(slug) {
	return prineside.categories.filter(c => c.parent === slug);
}
const sampLeaf = new Map();
for (const id of sampIds) {
	let best = null;
	for (const c of prineside.categories) {
		if (!members.get(c.slug).has(id))
			continue;
		if (childrenOf(c.slug).some(child => members.get(child.slug).has(id)))
			continue;
		if (best === null || c.models.length < best.models.length)
			best = c;
	}
	if (best)
		sampLeaf.set(id, best.slug);
}
// Only categories that end up holding an SA-MP model, plus their parents.
const usedSlugs = new Set();
for (const slug of sampLeaf.values())
	for (let s = slug; s; s = bySlug.get(s).parent)
		usedSlugs.add(s);
const sampCategoryIndex = new Map();
function addSampCategory(c, parentIdx) {
	if (!usedSlugs.has(c.slug))
		return;
	const idx = categories.length;
	categories.push({ name: c.title, parent: parentIdx });
	sampCategoryIndex.set(c.slug, idx);
	const kids = childrenOf(c.slug).sort((a, b) => a.title.localeCompare(b.title));
	for (const child of kids)
		addSampCategory(child, idx);
}
const roots = prineside.categories.filter(c => !c.parent).sort((a, b) => a.title.localeCompare(b.title));
for (const c of roots)
	addSampCategory(c, sampRootCategory);
for (const [id, slug] of sampLeaf)
	modelMap.set(id, sampCategoryIndex.get(slug));

// The server's SA-MP 0.3.DL / open.mp custom models, filed at run time.
const customCategory = categories.length;
categories.push({ name: 'Custom models', parent: -1 });

// Build sorted model->category array
const entries = Array.from(modelMap.entries());
entries.sort((a, b) => a[0] - b[0]);

// Generate header
let out = '';
out += '// Auto-generated from MTA:SA Map Editor catalog files\n';
if (prineside.source)
	out += '// SA-MP models filed by ' + prineside.source + ' (crawled ' + prineside.crawled + ')\n';
out += '// Categories for GTA SA object browser\n';
out += '\n';
out += '#define NUM_OBJ_CATEGORIES ' + categories.length + '\n';
out += '#define SAMP_CATEGORY_ROOT ' + sampRootCategory + '\n';
out += '#define SAMP_CATEGORY_UNCAT ' + sampUncatCategory + '\n';
out += '#define CUSTOM_MODELS_CATEGORY ' + customCategory + '\n';
out += '\n';
out += 'struct ObjCategory {\n';
out += '\tconst char *name;\n';
out += '\tint parent;  // -1 for top-level\n';
out += '};\n';
out += '\n';
out += 'static ObjCategory objCategories[] = {\n';
for (let i = 0; i < categories.length; i++) {
	const c = categories[i];
	out += '\t{ "' + escapeStr(c.name) + '", ' + c.parent + ' },\n';
}
out += '};\n';
out += '\n';
out += 'static int objCategoryMap[][2] = {\n';
for (const [modelId, catIdx] of entries) {
	out += '\t{ ' + modelId + ', ' + catIdx + ' },\n';
}
out += '\t{ -1, -1 }\n';
out += '};\n';

const outPath = path.join(dir, 'object_categories.h');
fs.writeFileSync(outPath, out, 'utf-8');
console.log('Generated ' + outPath);
console.log('  ' + categories.length + ' categories, ' + entries.length + ' model mappings');
