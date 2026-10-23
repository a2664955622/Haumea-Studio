# SPDX-License-Identifier: GPL-2.0-or-later
"""Check Pawn's VS Code token distinctions without launching the editor."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class PawnSyntaxTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'needs a C++ compiler')
    def test_pawn_word_categories(self):
        src = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='haumea-pawnsyntax-') as tmp:
            test = Path(tmp, 'check.cpp')
            test.write_text('''#include "pawnsyntax.h"
#include <cassert>
int main() {
    assert(ClassifyPawnWord("if", '(') == PAWN_WORD_KEYWORD);
    assert(ClassifyPawnWord("foreach", '(') == PAWN_WORD_KEYWORD);
    assert(ClassifyPawnWord("new") == PAWN_WORD_STORAGE);
    assert(ClassifyPawnWord("hook") == PAWN_WORD_STORAGE);
    assert(ClassifyPawnWord("Float", ':') == PAWN_WORD_TAG);
    assert(ClassifyPawnWord("CreateDynamicObject", '(') == PAWN_WORD_FUNCTION);
    assert(ClassifyPawnWord("INVALID_PLAYER_ID") == PAWN_WORD_CONSTANT);
    assert(ClassifyPawnWord("false") == PAWN_WORD_CONSTANT);
    assert(ClassifyPawnWord("playerid") == PAWN_WORD_IDENTIFIER);
    assert(ClassifyPawnWord("_private") == PAWN_WORD_IDENTIFIER);
    assert(ClassifyPawnWord("If", '(') == PAWN_WORD_FUNCTION);
    assert(ClassifyPawnWord("_") == PAWN_WORD_IDENTIFIER);
}''')
            binary = Path(tmp, 'check')
            subprocess.run(['c++', '-std=c++14', '-I', str(src), str(test),
                            str(src / 'pawnsyntax.cpp'), '-o', str(binary)], check=True,
                           capture_output=True, text=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
