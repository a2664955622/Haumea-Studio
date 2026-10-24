// SPDX-License-Identifier: GPL-2.0-or-later
#include "editorxml.h"
#include "updatecore.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace EditorXML
{
std::string Node::get(const std::string &key, const std::string &fallback) const
{
	auto it = attributes.find(key);
	return it == attributes.end() ? fallback : it->second;
}
const Node *Node::child(const std::string &tag) const
{
	for (const Node &n : children)
		if (n.name == tag)
			return &n;
	return nullptr;
}
Node &Node::add(const std::string &tag)
{
	children.emplace_back(tag);
	return children.back();
}
void Node::set(const std::string &key, const std::string &value)
{
	attributes[key] = value;
}
void Node::set(const std::string &key, const char *value)
{
	set(key, std::string(value ? value : ""));
}
void Node::set(const std::string &key, bool value)
{
	set(key, value ? "true" : "false");
}
void Node::set(const std::string &key, int value)
{
	set(key, std::to_string(value));
}
void Node::set(const std::string &key, unsigned value)
{
	set(key, std::to_string(value));
}
void Node::set(const std::string &key, float value)
{
	char b[64];
	snprintf(b, sizeof(b), "%.9g", value);
	set(key, b);
}
void Node::set(const std::string &key, double value)
{
	char b[64];
	snprintf(b, sizeof(b), "%.17g", value);
	set(key, b);
}
bool Node::number(const std::string &key, double *value) const
{
	auto it = attributes.find(key);
	if (it == attributes.end())
		return false;
	if (it->second == "true") {
		*value = 1;
		return true;
	}
	if (it->second == "false") {
		*value = 0;
		return true;
	}
	char *end;
	errno = 0;
	double n = strtod(it->second.c_str(), &end);
	if (errno || end == it->second.c_str() || *end || !std::isfinite(n))
		return false;
	*value = n;
	return true;
}
namespace
{
bool space(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
bool namechar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-' || c == '.' || c == ':' ||
	       (c >= '0' && c <= '9');
}
void utf8(std::string &out, unsigned c)
{
	if (c < 0x80)
		out += (char)c;
	else if (c < 0x800) {
		out += (char)(0xC0 | (c >> 6));
		out += (char)(0x80 | (c & 63));
	} else if (c < 0x10000) {
		out += (char)(0xE0 | (c >> 12));
		out += (char)(0x80 | ((c >> 6) & 63));
		out += (char)(0x80 | (c & 63));
	} else {
		out += (char)(0xF0 | (c >> 18));
		out += (char)(0x80 | ((c >> 12) & 63));
		out += (char)(0x80 | ((c >> 6) & 63));
		out += (char)(0x80 | (c & 63));
	}
}
bool legal(unsigned c)
{
	return c == 9 || c == 10 || c == 13 ||
	       (c >= 32 && c <= 0x10FFFF && !(c >= 0xD800 && c <= 0xDFFF) && c != 0xFFFE && c != 0xFFFF);
}
struct Parser {
	const std::string &s;
	size_t at = 0, nodes = 0;
	std::string problem;
	bool fail(const char *why)
	{
		problem = std::string(why) + " at byte " + std::to_string(at);
		return false;
	}
	bool skip()
	{
		for (;;) {
			while (at < s.size() && space(s[at]))
				at++;
			if (s.compare(at, 4, "<!--"))
				return true;
			size_t end = s.find("-->", at + 4);
			if (end == std::string::npos)
				return fail("Unclosed XML comment");
			at = end + 3;
		}
	}
	std::string name()
	{
		size_t start = at;
		if (at < s.size() && !((s[at] >= '0' && s[at] <= '9') || s[at] == '-' || s[at] == '.'))
			while (at < s.size() && namechar(s[at]))
				at++;
		return s.substr(start, at - start);
	}
	bool value(std::string &out)
	{
		if (at >= s.size() || (s[at] != '"' && s[at] != '\''))
			return fail("Expected quoted XML attribute");
		char quote = s[at++];
		while (at < s.size() && s[at] != quote) {
			unsigned char c = s[at++];
			if (c == '<')
				return fail("Unescaped XML bracket");
			if (c != '&') {
				if (!legal(c))
					return fail("Invalid XML character");
				out += (char)c;
				continue;
			}
			size_t end = s.find(';', at);
			if (end == std::string::npos || end - at > 20)
				return fail("Invalid XML entity");
			std::string entity = s.substr(at, end - at);
			at = end + 1;
			if (entity == "amp")
				out += '&';
			else if (entity == "lt")
				out += '<';
			else if (entity == "gt")
				out += '>';
			else if (entity == "quot")
				out += '"';
			else if (entity == "apos")
				out += '\'';
			else if (entity.size() > 1 && entity[0] == '#') {
				const char *digits = entity.c_str() + 1;
				int base = 10;
				if (*digits == 'x') {
					digits++;
					base = 16;
				}
				char *tail;
				errno = 0;
				unsigned long n = strtoul(digits, &tail, base);
				if (errno || tail == digits || *tail || n > 0x10FFFF || !legal((unsigned)n))
					return fail("Invalid XML code point");
				utf8(out, (unsigned)n);
			} else
				return fail("Unsupported XML entity");
		}
		if (at >= s.size())
			return fail("Unclosed XML attribute");
		at++;
		return true;
	}
	bool node(Node &out, int depth)
	{
		if (depth > 32 || ++nodes > 2000000)
			return fail("XML document is too complex");
		if (!skip() || at >= s.size() || s[at++] != '<')
			return fail("Expected XML element");
		out.name = name();
		if (out.name.empty())
			return fail("Invalid XML element name");
		for (;;) {
			if (!skip() || at >= s.size())
				return fail("Unclosed XML element");
			if (s.compare(at, 2, "/>") == 0) {
				at += 2;
				return true;
			}
			if (s[at] == '>') {
				at++;
				break;
			}
			std::string key = name();
			if (key.empty() || out.attributes.size() > 4096)
				return fail("Invalid XML attribute name");
			while (at < s.size() && space(s[at]))
				at++;
			if (at >= s.size() || s[at++] != '=')
				return fail("Expected XML attribute value");
			while (at < s.size() && space(s[at]))
				at++;
			std::string v;
			if (!value(v))
				return false;
			if (!out.attributes.emplace(key, v).second)
				return fail("Duplicate XML attribute");
		}
		for (;;) {
			if (!skip())
				return false;
			if (s.compare(at, 2, "</") == 0) {
				at += 2;
				std::string close = name();
				while (at < s.size() && space(s[at]))
					at++;
				if (close != out.name || at >= s.size() || s[at++] != '>')
					return fail("Mismatched XML element");
				return true;
			}
			Node child;
			if (!node(child, depth + 1))
				return false;
			out.children.push_back(std::move(child));
		}
	}
};
std::string escape(const std::string &s)
{
	std::string out;
	for (unsigned char c : s)
		switch (c) {
		case '&':
			out += "&amp;";
			break;
		case '<':
			out += "&lt;";
			break;
		case '>':
			out += "&gt;";
			break;
		case '"':
			out += "&quot;";
			break;
		case '\n':
			out += "&#10;";
			break;
		case '\r':
			out += "&#13;";
			break;
		case '\t':
			out += "&#9;";
			break;
		default:
			out += (char)c;
			break;
		}
	return out;
}
void writeNode(const Node &n, int depth, std::string &out)
{
	out.append(depth * 2, ' ');
	out += '<' + n.name;
	for (const auto &a : n.attributes)
		out += ' ' + a.first + "=\"" + escape(a.second) + '"';
	if (n.children.empty()) {
		out += "/>\n";
		return;
	}
	out += ">\n";
	for (const Node &child : n.children)
		writeNode(child, depth + 1, out);
	out.append(depth * 2, ' ');
	out += "</" + n.name + ">\n";
}
bool exists(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}
bool replace(const std::string &from, const std::string &to)
{
#ifdef _WIN32
	return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	return rename(from.c_str(), to.c_str()) == 0;
#endif
}
bool writeFile(const std::string &path, const std::string &data, std::string *error)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f) {
		*error = "Could not write " + path + ": " + strerror(errno);
		return false;
	}
	bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
	ok = fflush(f) == 0 && !ferror(f) && ok;
#ifdef _WIN32
	ok = _commit(_fileno(f)) == 0 && ok;
#else
	ok = fsync(fileno(f)) == 0 && ok;
#endif
	ok = fclose(f) == 0 && ok;
	if (!ok)
		*error = "Could not finish writing " + path + ": " + strerror(errno);
	return ok;
}
void syncDirectory(const std::string &path)
{
#ifndef _WIN32
	size_t slash = path.find_last_of('/');
	std::string parent = slash == std::string::npos ? "." : path.substr(0, slash);
	int fd = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
	if (fd >= 0) {
		fsync(fd);
		close(fd);
	}
#endif
}
bool owned(const Node &root, const char *kind)
{
	return root.name == "haumea" && root.get("version") == "1" && root.get("kind") == kind;
}
} // namespace
bool Parse(const std::string &source, Node *root, std::string *error)
{
	error->clear();
	if (source.size() > 256 * 1024 * 1024) {
		*error = "XML document exceeds 256 MiB";
		return false;
	}
	Parser p{source};
	if (source.compare(0, 3, "\xEF\xBB\xBF") == 0)
		p.at = 3;
	p.skip();
	if (source.compare(p.at, 5, "<?xml") == 0) {
		size_t end = source.find("?>", p.at + 5);
		if (end == std::string::npos) {
			*error = "Unclosed XML declaration";
			return false;
		}
		p.at = end + 2;
	}
	Node parsed;
	if (!p.node(parsed, 0) || !p.skip() || p.at != source.size()) {
		*error = p.problem.empty() ? "Unexpected XML content" : p.problem;
		return false;
	}
	*root = std::move(parsed);
	return true;
}
std::string Serialize(const Node &root)
{
	std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	writeNode(root, 0, out);
	return out;
}
bool ReadFile(const std::string &path, std::string *bytes, std::string *error)
{
	error->clear();
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) {
		if (errno != ENOENT)
			*error = "Could not read " + path + ": " + strerror(errno);
		return false;
	}
	bytes->clear();
	char buffer[65536];
	size_t n;
	while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
		bytes->append(buffer, n);
		if (bytes->size() > 256 * 1024 * 1024) {
			fclose(f);
			*error = "File exceeds 256 MiB: " + path;
			return false;
		}
	}
	bool ok = !ferror(f);
	fclose(f);
	if (!ok)
		*error = "Could not read " + path;
	return ok;
}
std::string Sidecar(const std::string &pawnPath)
{
	return pawnPath + ".xml";
}
int ReadDocument(const std::string &pawnPath, const std::string &pawn, const char *kind, Node *root, std::string *error)
{
	error->clear();
	std::string hash = Sha256Hex(pawn.data(), pawn.size()), bytes, previousError;
	Node parsed;
	std::string file = Sidecar(pawnPath);
	bool found = ReadFile(file, &bytes, error);
	bool valid = found && Parse(bytes, &parsed, error) && owned(parsed, kind);
	if (valid && parsed.get("pawn-sha256") == hash) {
		*root = std::move(parsed);
		return 1;
	}
	Node previous;
	if (ReadFile(file + ".previous", &bytes, &previousError) && Parse(bytes, &previous, &previousError) &&
	    owned(previous, kind) && previous.get("pawn-sha256") == hash) {
		*root = std::move(previous);
		error->clear();
		return 1;
	}
	if (valid) {
		*root = std::move(parsed);
		return 2;
	}
	if (found) {
		if (error->empty())
			*error = "Unsupported editor XML version or document kind: " + file;
		return -1;
	}
	return error->empty() ? 0 : -1;
}
bool WriteDocument(const std::string &pawnPath, const std::string &pawn, Node root, std::string *error)
{
	error->clear();
	std::string xml = Sidecar(pawnPath), oldPawn, oldXml;
	bool hadPawn = ReadFile(pawnPath, &oldPawn, error);
	if (!error->empty())
		return false;
	bool hadXml = ReadFile(xml, &oldXml, error);
	if (!error->empty())
		return false;
	if (hadXml) {
		Node previous;
		if (!Parse(oldXml, &previous, error) || !owned(previous, root.get("kind").c_str())) {
			if (error->empty())
				*error = "Refusing to overwrite unrelated editor XML: " + xml;
			return false;
		}
	}
	root.set("version", 1);
	root.set("pawn-sha256", Sha256Hex(pawn.data(), pawn.size()));
	std::string encoded = Serialize(root), testError;
	Node test;
	if (!Parse(encoded, &test, &testError)) {
		*error = "Invalid editor state: " + testError;
		return false;
	}
	static unsigned serial;
#ifdef _WIN32
	unsigned pid = GetCurrentProcessId();
#else
	unsigned pid = (unsigned)getpid();
#endif
	std::string suffix = ".tmp-" + std::to_string(pid) + "-" + std::to_string(++serial), pt = pawnPath + suffix,
		    xt = xml + suffix;
	if (!writeFile(pt, pawn, error) || !writeFile(xt, encoded, error)) {
		remove(pt.c_str());
		remove(xt.c_str());
		return false;
	}
	if (hadPawn && !hadXml) {
		std::string backup = pawnPath + ".pre-xml.bak";
		for (unsigned n = 2; exists(backup); n++)
			backup = pawnPath + ".pre-xml-" + std::to_string(n) + ".bak";
		if (!writeFile(backup, oldPawn, error)) {
			remove(pt.c_str());
			remove(xt.c_str());
			return false;
		}
		syncDirectory(backup);
	}
	std::string prev = xml + ".previous";
	if (hadXml) {
		Node selected;
		std::string why;
		if (hadPawn && ReadDocument(pawnPath, oldPawn, root.get("kind").c_str(), &selected, &why) == 1)
			oldXml = Serialize(selected);
		std::string bt = prev + suffix;
		if (!writeFile(bt, oldXml, error) || !replace(bt, prev)) {
			if (error->empty())
				*error = "Could not retain previous XML: " + xml;
			remove(bt.c_str());
			remove(pt.c_str());
			remove(xt.c_str());
			return false;
		}
		syncDirectory(prev);
	}
	if (!replace(xt, xml)) {
		*error = "Could not replace " + xml + ": " + strerror(errno);
		remove(pt.c_str());
		remove(xt.c_str());
		return false;
	}
	if (!replace(pt, pawnPath)) {
		*error = "Could not replace " + pawnPath + ": " + strerror(errno);
		if (hadXml) {
			if (!replace(prev, xml))
				*error += "; previous XML retained at " + prev;
		} else
			remove(xml.c_str());
		remove(pt.c_str());
		syncDirectory(xml);
		return false;
	}
	syncDirectory(pawnPath);
	if (hadXml)
		remove(prev.c_str());
	return true;
}
} // namespace EditorXML
