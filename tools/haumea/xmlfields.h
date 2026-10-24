// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "editorxml.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace XMLFields
{
using EditorXML::Node;
template <class T> void write(Node &n, const std::string &key, const T &value)
{
	n.set(key, value);
}
inline void write(Node &n, const std::string &key, const rw::V3d &v)
{
	n.set(key + ".x", v.x);
	n.set(key + ".y", v.y);
	n.set(key + ".z", v.z);
}
inline void write(Node &n, const std::string &key, const rw::Quat &v)
{
	n.set(key + ".x", v.x);
	n.set(key + ".y", v.y);
	n.set(key + ".z", v.z);
	n.set(key + ".w", v.w);
}
inline void write(Node &n, const std::string &key, const rw::RGBA &v)
{
	char b[16];
	snprintf(b, sizeof(b), "0x%08X",
		 ((unsigned)v.red << 24) | ((unsigned)v.green << 16) | ((unsigned)v.blue << 8) | v.alpha);
	n.set(key, b);
}
template <size_t N> void write(Node &n, const std::string &key, const char (&value)[N])
{
	n.set(key, value);
}
template <class T, size_t N> void write(Node &n, const std::string &key, const T (&value)[N])
{
	for (size_t i = 0; i < N; i++)
		write(n, key + "-" + std::to_string(i), value[i]);
}
inline bool read(const Node &n, const std::string &key, std::string &value, std::string *error)
{
	auto it = n.attributes.find(key);
	if (it != n.attributes.end())
		value = it->second;
	return true;
}
template <size_t N> bool read(const Node &n, const std::string &key, char (&value)[N], std::string *error)
{
	auto it = n.attributes.find(key);
	if (it == n.attributes.end())
		return true;
	if (it->second.size() >= N) {
		*error = "XML field is too long: " + key;
		return false;
	}
	memset(value, 0, N);
	memcpy(value, it->second.data(), it->second.size());
	return true;
}
template <class T>
typename std::enable_if<std::is_arithmetic<T>::value, bool>::type read(const Node &n, const std::string &key, T &value,
								       std::string *error)
{
	if (!n.attributes.count(key))
		return true;
	double number;
	if (!n.number(key, &number) || number < (double)std::numeric_limits<T>::lowest() ||
	    number > (double)std::numeric_limits<T>::max() ||
	    (std::is_integral<T>::value && std::floor(number) != number)) {
		*error = "Invalid XML number: " + key;
		return false;
	}
	value = (T)number;
	return true;
}
inline bool read(const Node &n, const std::string &key, rw::V3d &v, std::string *e)
{
	return read(n, key + ".x", v.x, e) && read(n, key + ".y", v.y, e) && read(n, key + ".z", v.z, e);
}
inline bool read(const Node &n, const std::string &key, rw::Quat &v, std::string *e)
{
	return read(n, key + ".x", v.x, e) && read(n, key + ".y", v.y, e) && read(n, key + ".z", v.z, e) &&
	       read(n, key + ".w", v.w, e);
}
inline bool read(const Node &n, const std::string &key, rw::RGBA &v, std::string *e)
{
	if (!n.attributes.count(key))
		return true;
	unsigned c = 0;
	if (!read(n, key, c, e))
		return false;
	v = {(rw::uint8)(c >> 24), (rw::uint8)(c >> 16), (rw::uint8)(c >> 8), (rw::uint8)c};
	return true;
}
template <class T, size_t N> bool read(const Node &n, const std::string &key, T (&value)[N], std::string *error)
{
	for (size_t i = 0; i < N; i++)
		if (!read(n, key + "-" + std::to_string(i), value[i], error))
			return false;
	return true;
}
} // namespace XMLFields
