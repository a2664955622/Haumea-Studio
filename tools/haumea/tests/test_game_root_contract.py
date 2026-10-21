# SPDX-License-Identifier: GPL-2.0-or-later
"""Headless source-contract tests for where Haumea finds and writes files."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]


def function_body(source: str, signature: str) -> str:
	start = source.index(signature)
	brace = source.index("{", start)
	depth = 0
	for index in range(brace, len(source)):
		if source[index] == "{":
			depth += 1
		elif source[index] == "}":
			depth -= 1
			if depth == 0:
				return source[brace + 1:index]
	raise AssertionError(f"unterminated function: {signature}")


class WindowsGameRootContractTests(unittest.TestCase):
	def test_game_root_comes_from_the_workspace_settings(self):
		# Haumea runs from its own folder: the game folder is a setting, never
		# the working directory (which native file dialogs can change).
		source = (ROOT / "tools/haumea/main.cpp").read_text()
		body = function_body(source, "GetGameRootDirectory(char *dir, size_t size)")
		self.assertIn("GetGameDir()", body)
		self.assertNotIn("getcwd", body)
		self.assertNotIn("GetCurrentDirectory", body)
		self.assertNotIn("chdir(", source)

	def test_game_files_open_relative_to_the_game_folder(self):
		source = (ROOT / "tools/haumea/main.cpp").read_text()
		self.assertIn("GamePath(path)", function_body(source, "fopen_ci(const char *path, const char *mode)"))

	def test_nothing_is_written_to_the_game_folder(self):
		source = (ROOT / "tools/haumea/main.cpp").read_text()
		self.assertIn("IsInGameDir(path)", function_body(source, "EnsureParentDirectoriesForPath(const char *path)"))

	def test_native_file_picker_restores_process_directory(self):
		source = (ROOT / "tools/haumea/gui.cpp").read_text()
		body = function_body(source, "pickFileDialog(char *dst, size_t size, const char *expectedExt)")
		windows_branch = body.split("#ifdef _WIN32", 1)[1].split("#else", 1)[0]

		self.assertIn("OFN_NOCHANGEDIR", windows_branch)
		self.assertLess(windows_branch.index("GetCurrentDirectoryA"),
		                windows_branch.index("GetOpenFileNameA"))
		self.assertGreater(windows_branch.index("SetCurrentDirectoryA"),
		                   windows_branch.index("GetOpenFileNameA"))


if __name__ == "__main__":
	unittest.main()
