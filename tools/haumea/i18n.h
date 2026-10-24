#pragma once

#include <string>
#include <vector>

// Marks UI text for translation: _("Size") is the text in the user's
// language, English (the key) when there is no catalog or no entry.
// "Text##id" and "Text###id" are looked up as "Text" and keep their suffix.
const char *Translate(const char *text);
#define _(text) Translate(text)

// Marks text in a static table or global that is built before the catalog
// loads. It stays the English literal; call _(entry) where it is drawn.
#define N_(text) text

// A label that is also an ImGui ID (window, tab, header, button): the text
// shown in the user's language, then "###" and the English text, so the ID is
// the same in every language. L("Object Info") is "Objet###Object Info".
// Valid until the language changes (at the start of a frame); use it directly
// as the ImGui label argument.
const char *Label(const char *english);
#define L(text) Label(text)

// With an icon (ICON_FA_*, a string literal): LI(icon, "Tools") is
// icon + " " + _("Tools") + "###Tools" for ID labels, TI(icon, "Copy as Pawn")
// is icon + " " + _(text) for text that is not an ID. The icon stays out of
// the catalog key. Cached per literal pair, valid like L().
const char *LabelIcon(const char *icon, const char *english);
const char *TextIcon(const char *icon, const char *english);
#define LI(icon, text) LabelIcon(icon, text)
#define TI(icon, text) TextIcon(icon, text)

// Catalogs are lang/<code>.po files; every one found is a language.
struct I18nLanguage {
	std::string code;	// "pt-BR", the file name without .po
	std::string name;	// "Português (Brasil)", from the catalog's Language-Name header
};

const std::vector<I18nLanguage> &I18nLanguages(void);

// Call once at startup (before the first frame): finds the catalogs and
// applies the saved language. Old imgui.ini layouts need no migration step:
// L() and LI() move a window's saved position and dock from its old
// (untranslated) title to the ### ID the first time the label is built.
void I18nInit(void);

// The setting: "auto" (the OS language if a catalog matches, else English),
// "en" (English) or a catalog code. Applied at the next I18nNewFrame().
const char *I18nGetLanguage(void);
void I18nSetLanguage(const char *preference);
// The code actually in use, "en" when none.
const char *I18nActiveLanguage(void);

// Call once per frame before drawing: applies a changed preference.
void I18nNewFrame(void);

// Loads a catalog from text (tests): replaces the active one. Entries whose
// printf conversions differ from the English text are dropped.
bool I18nLoadCatalogText(const std::string &po, int *dropped = nullptr);

// Preferences > Language.
bool uiLanguagePreferences(void);	// true when the choice changed

// The characters (codepoints) the installed catalogs translate into, so the
// font atlas can bake exactly what a language draws. ASCII is left out: the UI
// font's default ranges already cover it.
void I18nCatalogCodepoints(std::vector<unsigned int> &out);
