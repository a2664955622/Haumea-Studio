// SPDX-License-Identifier: GPL-2.0-or-later
#include "pawnsyntax.h"

// Categories and Pawn keywords follow openmultiplayer/vscode-pawn's
// syntaxes/Pawn.tmLanguage (MIT; see THIRD_PARTY_NOTICES.md). The palette
// uses VS Code Dark+ colors; no TextMate/browser runtime is needed.
PawnWordKind ClassifyPawnWord(const std::string &word, char next)
{
	static const char *controls[] = {"assert", "break",  "case",	"continue", "default", "do",	 "else",
					 "exit",   "for",    "foreach", "goto",	    "if",      "return", "sleep",
					 "state",  "switch", "while",	"sizeof",   "tagof",   "defined"};
	static const char *storage[] = {"new",	 "enum", "public", "forward", "native", "char",	  "const", "static",
					"stock", "hook", "task",   "ptask",   "void",	"inline", "using", "operator"};
	for (const char *s : controls)
		if (word == s)
			return PAWN_WORD_KEYWORD;
	for (const char *s : storage)
		if (word == s)
			return PAWN_WORD_STORAGE;
	if (word == "true" || word == "false" || word == "TRUE" || word == "FALSE")
		return PAWN_WORD_CONSTANT;
	if (next == ':')
		return PAWN_WORD_TAG;
	if (next == '(')
		return PAWN_WORD_FUNCTION;
	bool upper = !word.empty(), letter = false;
	for (char c : word) {
		if (c >= 'A' && c <= 'Z')
			letter = true;
		else if (c != '_' && !(c >= '0' && c <= '9'))
			upper = false;
	}
	return upper && letter ? PAWN_WORD_CONSTANT : PAWN_WORD_IDENTIFIER;
}
