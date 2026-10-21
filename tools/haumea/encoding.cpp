#include "encoding.h"

#include <string.h>

// Windows-1252 bytes 0x80-0x9F; the rest of 0xA0-0xFF is Latin-1, which is
// the same code point. 0 marks the five bytes 1252 leaves undefined.
static const unsigned short cp1252High[32] = {
	0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
	0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
};

static PawnTextEncoding gEncoding = PAWN_TEXT_CP1252;

static void
appendUtf8(std::string &out, unsigned cp)
{
	if(cp < 0x80)
		out += (char)cp;
	else if(cp < 0x800){
		out += (char)(0xC0 | cp >> 6);
		out += (char)(0x80 | (cp & 0x3F));
	}else if(cp < 0x10000){
		out += (char)(0xE0 | cp >> 12);
		out += (char)(0x80 | (cp >> 6 & 0x3F));
		out += (char)(0x80 | (cp & 0x3F));
	}else{
		out += (char)(0xF0 | cp >> 18);
		out += (char)(0x80 | (cp >> 12 & 0x3F));
		out += (char)(0x80 | (cp >> 6 & 0x3F));
		out += (char)(0x80 | (cp & 0x3F));
	}
}

// The code point at s[i], advancing i; -1 for a byte that doesn't start a
// valid UTF-8 sequence (i moves past it).
static long
nextUtf8(const std::string &s, size_t &i)
{
	unsigned char c = s[i++];
	if(c < 0x80)
		return c;
	int extra = c >= 0xF5 ? -1 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 ? 1 : -1;
	if(extra < 0 || i + extra > s.size())
		return -1;
	long cp = c & (0x3F >> extra);
	for(int k = 0; k < extra; k++){
		unsigned char d = s[i + k];
		if((d & 0xC0) != 0x80)
			return -1;
		cp = cp << 6 | (d & 0x3F);
	}
	// Overlong forms and surrogates are not UTF-8.
	if((extra == 2 && cp < 0x800) || (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF)) ||
	   (cp >= 0xD800 && cp <= 0xDFFF))
		return -1;
	i += extra;
	return cp;
}

bool
IsValidUtf8(const std::string &s)
{
	for(size_t i = 0; i < s.size(); )
		if(nextUtf8(s, i) < 0)
			return false;
	return true;
}

std::string
Cp1252ToUtf8(const std::string &s)
{
	std::string out;
	out.reserve(s.size() + s.size() / 8);
	for(size_t i = 0; i < s.size(); i++){
		unsigned char c = s[i];
		unsigned cp = c;
		if(c >= 0x80 && c < 0xA0)
			cp = cp1252High[c - 0x80] ? cp1252High[c - 0x80] : c;	// undefined: kept as C1
		appendUtf8(out, cp);
	}
	return out;
}

static int
cp1252Byte(long cp)
{
	if(cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF))
		return (int)cp;
	for(int k = 0; k < 32; k++)
		if(cp1252High[k] == cp)
			return 0x80 + k;
	if(cp >= 0x80 && cp < 0xA0)
		return (int)cp;	// the undefined bytes, which Cp1252ToUtf8 keeps as C1
	return -1;
}

std::string
Utf8ToCp1252(const std::string &s, bool *lossy)
{
	std::string out;
	out.reserve(s.size());
	for(size_t i = 0; i < s.size(); ){
		long cp = nextUtf8(s, i);
		int b = cp < 0 ? -1 : cp1252Byte(cp);
		if(b < 0){
			out += '?';
			if(lossy)
				*lossy = true;
		}else
			out += (char)b;
	}
	return out;
}

bool
Cp1252CanEncode(const std::string &utf8)
{
	bool lossy = false;
	Utf8ToCp1252(utf8, &lossy);
	return !lossy;
}

std::string
Cp1252Unencodable(const std::string &utf8)
{
	std::string missing;
	for(size_t i = 0; i < utf8.size(); ){
		size_t start = i;
		long cp = nextUtf8(utf8, i);
		if(cp >= 0 && cp1252Byte(cp) >= 0)
			continue;
		std::string ch = utf8.substr(start, i - start);
		if(missing.find(ch) == std::string::npos)
			missing += ch;
	}
	return missing;
}

bool
IsAscii(const char *s)
{
	for(; s && *s; s++)
		if((unsigned char)*s >= 0x80)
			return false;
	return true;
}

PawnTextEncoding
GetPawnTextEncoding(void)
{
	return gEncoding;
}

void
SetPawnTextEncoding(PawnTextEncoding encoding)
{
	gEncoding = encoding == PAWN_TEXT_UTF8 ? PAWN_TEXT_UTF8 : PAWN_TEXT_CP1252;
}

const char*
PawnTextEncodingName(PawnTextEncoding encoding)
{
	return encoding == PAWN_TEXT_UTF8 ? "UTF-8" : "Windows-1252";
}

static const char headerTag[] = "// Text encoding: ";

std::string
DecodePawnFileText(const std::string &raw, PawnTextEncoding *encoding)
{
	// Haumea's own header says which it is.
	size_t at = raw.find(headerTag);
	if(at != std::string::npos && at < 4096){
		at += sizeof(headerTag) - 1;
		bool utf8 = raw.compare(at, 5, "UTF-8") == 0;
		if(encoding)
			*encoding = utf8 ? PAWN_TEXT_UTF8 : PAWN_TEXT_CP1252;
		return utf8 ? raw : Cp1252ToUtf8(raw);
	}
	if(!IsValidUtf8(raw)){
		if(encoding)
			*encoding = PAWN_TEXT_CP1252;
		return Cp1252ToUtf8(raw);
	}
	bool ascii = true;
	for(size_t i = 0; i < raw.size() && ascii; i++)
		ascii = (unsigned char)raw[i] < 0x80;
	if(encoding)
		*encoding = ascii ? PAWN_TEXT_CP1252 : PAWN_TEXT_UTF8;
	return raw;
}

std::string
EncodePawnFileText(const std::string &utf8, PawnTextEncoding encoding, bool *lossy)
{
	if(encoding == PAWN_TEXT_UTF8)
		return utf8;
	return Utf8ToCp1252(utf8, lossy);
}
