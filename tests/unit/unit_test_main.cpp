// The unit test entry point.
//
// doctest's runner lives in exactly one translation unit; every other file in
// this executable includes the header without the implementation macro. Kept
// on its own so that a test file can never accidentally become the one that
// carries a second copy of the runner.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
