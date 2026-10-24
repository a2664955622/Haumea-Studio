// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#endif

// The Windows CRT's tmpfile() writes at the drive root, which can require
// elevation. Code generation needs only a private, seekable scratch stream.
inline FILE *OpenTemporaryCodeStream()
{
#ifdef _WIN32
	wchar_t directory[MAX_PATH + 1], path[MAX_PATH + 1];
	DWORD length = GetTempPathW(MAX_PATH + 1, directory);
	if(length == 0 || length > MAX_PATH || !GetTempFileNameW(directory, L"hmc", 0, path)){
		errno = EACCES;
		return nullptr;
	}
	// GetTempFileName reserves an empty file. Delete it on close, including
	// error paths, and keep the descriptor out of spawned child processes.
	int fd = _wopen(path, _O_RDWR | _O_BINARY | _O_TEMPORARY | _O_NOINHERIT);
	if(fd == -1){
		int error = errno;
		DeleteFileW(path);
		errno = error;
		return nullptr;
	}
	FILE *stream = _fdopen(fd, "w+b");
	if(stream == nullptr){
		int error = errno;
		_close(fd);
		errno = error;
	}
	return stream;
#else
	return tmpfile();
#endif
}
