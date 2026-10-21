#pragma once

// Marks UI text for translation: _("Size") is the text in the user's
// language. Every string is English until translations are added; the marks
// let them be collected.
inline const char *Translate(const char *text) { return text; }
#define _(text) Translate(text)
