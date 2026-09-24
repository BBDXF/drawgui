// P6 sub-step a1's golden scene, built through the C ABI rather than
// dg::RenderTree/dg::Widget directly - see golden_test.cpp's own comment on
// why this is the one scene in the table that does not paint on a bare
// dg::Canvas. Only compiled/linked when DRAWGUI_HAVE_SDL3 (the C ABI does
// not exist without a real windowing backend - CMakeLists.txt's own reason
// for gating src/abi/*.cpp the identical way).

#pragma once

#include <cstdint>
#include <vector>

// Opens an app, adds `font_dir`'s "DgTest Latin" (tests/fonts/
// gen_test_fonts.py's own deterministic, byte-identical-every-run font -
// NOT a system font, because a golden compares bytes and a system font's
// glyphs are a property of the machine, not of drawgui), builds one
// DG_NODE_TYPE_LABEL window root, sets its text through dg_node_set_text(),
// forces one paint via dg_wait_events(), and returns the encoded PNG bytes
// dg_debug_capture_png() hands back. Empty on any failure - the caller
// reports "rendering produced no PNG" exactly as the Canvas-based scenes do.
std::vector<std::uint8_t> produce_abi_label_scene(const char* font_dir);
