#pragma once

#include <stdio.h>

// Developer self-tests on the opened map (gui.cpp runs them from the
// command line). They return how many checks failed.
//   --undo-selftest <map.pwn> <report>: every kind of edit is one undo step,
//     and undo/redo give back the exported Pawn byte for byte.
//   --profile <map.pwn> <report>: timings for open, save, Map check,
//     selection and player view, plus frame times once it is open.
int RunUndoSelfTest(FILE *report);
int RunProfile(FILE *report, double openSeconds);
