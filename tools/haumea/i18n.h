#pragma once

#include <string>
#include <unordered_map>

// Marks UI text for translation: _("Size") is the text in the user's
// language. Every string is English until translations are added; the marks
// let them be collected.
inline const char *Translate(const char *text) { return text; }
#define _(text) Translate(text)

// Marks text in a static table or global that is built before the catalog
// loads. It stays the English literal; call _(entry) where it is drawn.
#define N_(text) text

// A label that is also an ImGui ID (window, tab, header, button): the text
// shown in the user's language, then "###" and the English text, so the ID is
// the same in every language. L("Object Info") is "Objet###Object Info".
// The result is cached per literal and stays valid until the next call after
// the language changes; use it directly as the ImGui label argument.
inline const char *Label(const char *english) {
	static std::unordered_map<const char *, std::string> cache;
	std::string &s = cache[english];
	if (s.empty()) {
		s = Translate(english);
		s += "###";
		s += english;
	}
	return s.c_str();
}
#define L(text) Label(text)
