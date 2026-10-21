#pragma once

#include <string>

// Text encodings of Pawn maps. Inside the editor all text is UTF-8 (ImGui
// and the agent use it). SA-MP and open.mp clients draw the bytes of a
// string with the game's Windows-1252 font, so that is what a .pwn holds
// by default: pawno and Texture Studio write it too. UTF-8 is kept for maps
// that already use it, so they save unchanged.

enum PawnTextEncoding {
	PAWN_TEXT_CP1252,	// Windows-1252, what the client draws (the default)
	PAWN_TEXT_UTF8,
};

bool IsValidUtf8(const std::string &s);
std::string Cp1252ToUtf8(const std::string &s);
// Characters Windows-1252 has no byte for become '?' and set *lossy.
std::string Utf8ToCp1252(const std::string &s, bool *lossy = nullptr);
// Whether every character of a UTF-8 string has a Windows-1252 byte, and
// the ones that don't (each once, as UTF-8).
bool Cp1252CanEncode(const std::string &utf8);
std::string Cp1252Unencodable(const std::string &utf8);
bool IsAscii(const char *s);

// The open map's text encoding, saved as a "// Text encoding:" header line.
PawnTextEncoding GetPawnTextEncoding(void);
void SetPawnTextEncoding(PawnTextEncoding encoding);
const char *PawnTextEncodingName(PawnTextEncoding encoding);

// A map file's text as UTF-8, and the encoding it was in: its header line if
// it has one, else Windows-1252 when it isn't valid UTF-8 (pawno, Texture
// Studio), UTF-8 when it has non-ASCII UTF-8 text, else the default.
std::string DecodePawnFileText(const std::string &raw, PawnTextEncoding *encoding);
// The encoding the last imported map's text was in (ImportPawnSource).
PawnTextEncoding GetLastImportTextEncoding(void);
// The editor's UTF-8 Pawn code in the bytes of an encoding.
std::string EncodePawnFileText(const std::string &utf8, PawnTextEncoding encoding, bool *lossy = nullptr);
