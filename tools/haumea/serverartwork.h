// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "simplejson.h"
#include <fstream>
#include <sstream>
#include <algorithm>

// A server's configured artwork directory, relative to its root unless absolute.
// open.mp's config.json takes precedence over the legacy server.cfg.
inline std::string
ServerArtworkDirectory(const std::string &server)
{
	std::string path = "models";
	std::ifstream jsonFile(server + "/config.json");
	if(jsonFile){
		std::ostringstream contents;
		contents << jsonFile.rdbuf();
		std::string text = contents.str();
		HaumeaJson::Json root;
		HaumeaJson::JsonParser parser = { text.data(), text.data() + text.size() };
		if(parser.value(root) && root.type == HaumeaJson::Json::OBJ){
			const HaumeaJson::Json *artwork = root.get("artwork");
			std::string configured = artwork ? artwork->text("models_path") : "";
			if(configured.empty()) configured = root.text("artwork.models_path");
			if(!configured.empty()) path = configured;
		}
	}else{
		std::ifstream cfg(server + "/server.cfg");
		std::string line;
		while(std::getline(cfg, line)){
			std::istringstream fields(line);
			std::string key;
			fields >> key;
			if(key != "artpath") continue;
			std::string value;
			std::getline(fields >> std::ws, value);
			size_t end = value.find_last_not_of(" \t\r");
			if(end == std::string::npos) continue;
			value.resize(end + 1);
			if(value.size() >= 2 && value.front() == '"' && value.back() == '"')
				value = value.substr(1, value.size()-2);
			if(!value.empty()) path = value;
		}
	}
	size_t begin = path.find_first_not_of(" \t\r\n");
	size_t end = path.find_last_not_of(" \t\r\n");
	path = begin == std::string::npos ? "models" : path.substr(begin, end-begin+1);
	std::replace(path.begin(), path.end(), '\\', '/');
	bool absolute = !path.empty() && (path[0] == '/' ||
		(path.size() > 2 && path[1] == ':' && path[2] == '/'));
	return absolute ? path : server + "/" + path;
}
