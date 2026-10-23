// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

enum PawnEditorAction {
	PAWN_EDITOR_NEW,
	PAWN_EDITOR_OPEN,
	PAWN_EDITOR_SAVE,
	PAWN_EDITOR_SAVE_AS,
	PAWN_EDITOR_REFRESH,
	PAWN_EDITOR_UNDO,
	PAWN_EDITOR_REDO,
	PAWN_EDITOR_CUT,
	PAWN_EDITOR_COPY,
	PAWN_EDITOR_PASTE,
	PAWN_EDITOR_SELECT_ALL
};
void PawnEditorRequest(PawnEditorAction action);
void PawnEditorDraw(void);
void PawnEditorTick(void);
void PawnEditorFlushDraft(void);
bool PawnEditorHasUnsavedChanges(void);
bool InPawnIdeMode(void);

// UI automation checks the real editor document, not the generated map.
const std::string &PawnEditorText(void);
