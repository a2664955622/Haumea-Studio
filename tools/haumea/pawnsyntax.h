// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

enum PawnWordKind {
	PAWN_WORD_IDENTIFIER,
	PAWN_WORD_KEYWORD,
	PAWN_WORD_STORAGE,
	PAWN_WORD_FUNCTION,
	PAWN_WORD_TAG,
	PAWN_WORD_CONSTANT
};
PawnWordKind ClassifyPawnWord(const std::string &word, char next = 0);
