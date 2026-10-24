// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <map>
#include <string>
#include <vector>

namespace EditorXML
{
struct Node {
	std::string name;
	std::map<std::string, std::string> attributes;
	std::vector<Node> children;
	Node(const std::string &name = "") : name(name) {}
	std::string get(const std::string &key, const std::string &fallback = "") const;
	const Node *child(const std::string &name) const;
	Node &add(const std::string &name);
	void set(const std::string &key, const std::string &value);
	void set(const std::string &key, const char *value);
	void set(const std::string &key, bool value);
	void set(const std::string &key, int value);
	void set(const std::string &key, unsigned value);
	void set(const std::string &key, float value);
	void set(const std::string &key, double value);
	bool number(const std::string &key, double *value) const;
};
bool Parse(const std::string &source, Node *root, std::string *error);
std::string Serialize(const Node &root);
bool ReadFile(const std::string &path, std::string *bytes, std::string *error);
std::string Sidecar(const std::string &pawnPath);
// 0: absent, 1: current snapshot, 2: Pawn changed externally, -1: invalid XML.
// A previous XML generation is used after an interrupted paired save.
int ReadDocument(const std::string &pawnPath, const std::string &pawn, const char *kind, Node *root,
		 std::string *error);
// Stage both files before replacing either; a failed second rename restores
// the old XML. First conversion retains the original Pawn as .pre-xml.bak.
bool WriteDocument(const std::string &pawnPath, const std::string &pawn, Node root, std::string *error);
} // namespace EditorXML
