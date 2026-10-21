#pragma once

#include <stdio.h>
#include <string>
#include <vector>

// SA-MP textdraws: designed on the client's 640x448 canvas and written to the
// map as TextDrawCreate / CreatePlayerTextDraw calls.

enum {
	TD_FONT_BECKETT,	// 0..3 are text fonts
	TD_FONT_AHARONI,
	TD_FONT_BANK_GOTHIC,
	TD_FONT_PRICEDOWN,
	TD_FONT_SPRITE,	// text is "txd:texture"
	TD_FONT_PREVIEW,	// a 3D model
};

struct TextDraw
{
	char name[32];	// the Pawn variable
	bool perPlayer;	// CreatePlayerTextDraw
	float x, y;
	std::string text;
	float letterX, letterY;	// TextDrawLetterSize
	float sizeX, sizeY;	// TextDrawTextSize
	int alignment;	// 1 left, 2 centre, 3 right
	unsigned int colour, boxColour, backgroundColour;	// 0xRRGGBBAA
	bool useBox;
	int shadow, outline;
	int font;
	bool proportional;
	bool selectable;
	int previewModel;
	float previewRotX, previewRotY, previewRotZ, previewZoom;
	int previewColour1, previewColour2;
};

void InitTextDraw(TextDraw *td);	// the client's defaults
std::vector<TextDraw> &GetTextDraws(void);
void TextDrawsChanged(void);	// after an edit: marks the map unsaved and makes an undo step
bool TextDrawsEditPending(void);
bool TakeTextDrawsEdit(void);	// true once per finished edit
void ResetTextDraws(void);

// Pawn: the globals, the two functions and the header line.
bool HasTextDraws(void);
void WriteTextDrawsPawnHeader(FILE *f, const char *prefix);
void WriteTextDrawsPawnGlobals(FILE *f, const char *prefix);
void WriteTextDrawsPawnFunctions(FILE *f, const char *prefix);
// Reads TextDrawCreate / CreatePlayerTextDraw and their setters. Returns
// how many textdraws were added.
int ImportTextDrawsFromSource(const std::string &source);

// textdraws_ui.cpp
void DrawTextDrawsWindow(bool *open);
void DrawTextDrawsOverlay(void);	// also records undo steps for edits
void RenderTextDrawPreviews(void);	// model previews, offscreen before the world is drawn
void SelectTextDraw(int index);	// in the TextDraws window, e.g. from Map Check
