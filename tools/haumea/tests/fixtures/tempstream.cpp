// SPDX-License-Identifier: GPL-2.0-or-later
#include "tempstream.h"
#include <cassert>
#include <string>
#include <vector>

int main()
{
#ifdef _WIN32
	wchar_t base[MAX_PATH + 1];
	assert(GetCurrentDirectoryW(MAX_PATH + 1, base));
	std::wstring directory = std::wstring(base) + L"\\scratch \u00e7\u03a9";
	assert(CreateDirectoryW(directory.c_str(), nullptr));
	assert(SetEnvironmentVariableW(L"TMP", directory.c_str()));
	std::vector<std::wstring> names;
#endif
	std::vector<FILE*> streams;
	std::string data = "// Pawn\n";
	data.append(150000, 'x');
	data.append("\r\n\x00\xff", 4);
	for(int i = 0; i < 8; ++i){
		FILE *stream = OpenTemporaryCodeStream();
		assert(stream);
		streams.push_back(stream);
#ifdef _WIN32
		wchar_t path[32768];
		HANDLE handle = (HANDLE)_get_osfhandle(_fileno(stream));
		DWORD length = GetFinalPathNameByHandleW(handle, path, 32768, FILE_NAME_NORMALIZED);
		assert(length && length < 32768);
		std::wstring name(path);
		assert(name.find(directory) != std::wstring::npos);
		for(const auto &previous : names) assert(previous != name);
		names.push_back(name);
#endif
		assert(fwrite(data.data(), 1, data.size(), stream) == data.size());
		assert(fflush(stream) == 0 && ftell(stream) == (long)data.size());
		assert(fseek(stream, 0, SEEK_SET) == 0);
		std::string read(data.size(), '\0');
		assert(fread(&read[0], 1, read.size(), stream) == read.size());
		assert(read == data);
	}
	for(FILE *stream : streams) assert(fclose(stream) == 0);
#ifdef _WIN32
	for(const auto &name : names) assert(GetFileAttributesW(name.c_str()) == INVALID_FILE_ATTRIBUTES);
	// A failed creation must leave no stray files either.
	assert(SetEnvironmentVariableW(L"TMP", (directory + L"\\missing").c_str()));
	assert(OpenTemporaryCodeStream() == nullptr);
	assert(RemoveDirectoryW(directory.c_str()));
#endif
	puts("Temporary code streams: passed");
}
