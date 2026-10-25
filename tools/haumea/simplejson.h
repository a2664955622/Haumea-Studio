// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Shared JSON reader for server configuration and update metadata.
#include <stdint.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

namespace HaumeaJson {


struct Json {
	enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
	std::string str;
	std::vector<Json> items;							// ARR
	std::vector<std::pair<std::string, Json>> fields;	// OBJ
	bool boolean = false;

	const Json *get(const char *key) const {
		for(const auto &f : fields)
			if(f.first == key)
				return &f.second;
		return nullptr;
	}
	std::string text(const char *key) const {
		const Json *v = get(key);
		return v && v->type == STR ? v->str : std::string();
	}
	bool flag(const char *key) const {
		const Json *v = get(key);
		return v && v->type == BOOL && v->boolean;
	}
};

struct JsonParser {
	const char *p, *end;
	int depth = 0;

	void ws(void){ while(p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
	bool lit(const char *word){
		size_t n = strlen(word);
		if((size_t)(end - p) < n || memcmp(p, word, n) != 0) return false;
		p += n;
		return true;
	}
	static void utf8(std::string &out, uint32_t c){
		if(c < 0x80) out += (char)c;
		else if(c < 0x800){ out += (char)(0xC0 | c >> 6); out += (char)(0x80 | (c & 63)); }
		else if(c < 0x10000){ out += (char)(0xE0 | c >> 12); out += (char)(0x80 | (c >> 6 & 63)); out += (char)(0x80 | (c & 63)); }
		else{ out += (char)(0xF0 | c >> 18); out += (char)(0x80 | (c >> 12 & 63)); out += (char)(0x80 | (c >> 6 & 63)); out += (char)(0x80 | (c & 63)); }
	}
	bool hex4(uint32_t *c){
		if(end - p < 4) return false;
		*c = 0;
		for(int i = 0; i < 4; i++){
			char h = *p++;
			*c <<= 4;
			if(h >= '0' && h <= '9') *c |= h - '0';
			else if(h >= 'a' && h <= 'f') *c |= h - 'a' + 10;
			else if(h >= 'A' && h <= 'F') *c |= h - 'A' + 10;
			else return false;
		}
		return true;
	}
	bool string(std::string &out){
		if(p >= end || *p != '"') return false;
		p++;
		while(p < end && *p != '"'){
			if(*p != '\\'){
				out += *p++;
				continue;
			}
			if(++p >= end) return false;
			char c = *p++;
			switch(c){
			case 'n': out += '\n'; break;
			case 't': out += '\t'; break;
			case 'r': break;
			case 'b': case 'f': break;
			case 'u': {
				uint32_t cp;
				if(!hex4(&cp)) return false;
				if(cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u'){
					p += 2;
					uint32_t lo;
					if(!hex4(&lo)) return false;
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				}
				utf8(out, cp);
				break;
			}
			default: out += c; break;	// \" \\ \/
			}
		}
		if(p >= end) return false;
		p++;
		return true;
	}
	bool value(Json &v){
		if(++depth > 64) return false;
		ws();
		if(p >= end) return false;
		bool ok = true;
		if(*p == '{'){
			v.type = Json::OBJ;
			p++; ws();
			if(p < end && *p == '}') p++;
			else for(;;){
				std::string key;
				ws();
				if(!string(key)) return false;
				ws();
				if(p >= end || *p++ != ':') return false;
				v.fields.emplace_back(key, Json());
				if(!value(v.fields.back().second)) return false;
				ws();
				if(p < end && *p == ','){ p++; continue; }
				if(p < end && *p == '}'){ p++; break; }
				return false;
			}
		}else if(*p == '['){
			v.type = Json::ARR;
			p++; ws();
			if(p < end && *p == ']') p++;
			else for(;;){
				v.items.emplace_back();
				if(!value(v.items.back())) return false;
				ws();
				if(p < end && *p == ','){ p++; continue; }
				if(p < end && *p == ']'){ p++; break; }
				return false;
			}
		}else if(*p == '"'){
			v.type = Json::STR;
			ok = string(v.str);
		}else if(lit("true")){ v.type = Json::BOOL; v.boolean = true; }
		else if(lit("false")){ v.type = Json::BOOL; }
		else if(lit("null")){ v.type = Json::NUL; }
		else{
			v.type = Json::NUM;
			const char *start = p;
			while(p < end && (strchr("+-.eE", *p) || (*p >= '0' && *p <= '9'))) p++;
			ok = p > start;
			v.str.assign(start, p - start);	// the number's text
		}
		depth--;
		return ok;
	}
};

}
