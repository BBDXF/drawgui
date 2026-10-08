/* examples/19_c_client - a genuinely pure C program against drawgui's C ABI.
 *
 * This is P5's own acceptance criterion (design.md section 5.8, slice
 * 6-3): create two windows, respond to clicks, all
 * through include/drawgui/abi/drawgui.h and nothing else. This file is
 * compiled as C (see CMakeLists.txt: `set_source_files_properties(main.c
 * PROPERTIES LANGUAGE C)` plus a C, not C++, compiler front end) and links
 * only libdrawgui.a - if a single C++ or Skia type had leaked into
 * drawgui.h, this file would not compile, which is the whole point of
 * writing it this way rather than asserting the header's purity by reading
 * it.
 *
 *   (no arguments)        opens two windows with a clickable button each;
 *                         click either to see it respond, close both to exit
 *   --verify-c-client     the headless CTest oracle - no human, no display.
 *                         Run under SDL_VIDEODRIVER=dummy, set by the CTest
 *                         entry's own ENVIRONMENT property
 *                         (examples/19_c_client/CMakeLists.txt), not by this
 *                         file: setenv() is POSIX, not ISO C, and the only
 *                         way to declare it under strict `-std=c11` is
 *                         `_POSIX_C_SOURCE` - a reserved identifier
 *                         clang-tidy's bugprone-reserved-identifier rightly
 *                         objects to defining, with no alternative spelling
 *                         POSIX permits. This file stays free of it instead.
 *                         See run_verify() below for exactly what it checks.
 */

#include <stdio.h>
#include <string.h>

#include "drawgui/abi/drawgui.h"

#ifdef __cplusplus
#error "main.c must be compiled as C - if this fired, the build regressed to C++"
#endif

static dg_value make_color(unsigned argb) {
  dg_value value;
  value.size = sizeof(dg_value);
  value.type = DG_VALUE_COLOR;
  value.number = 0.0F;
  value.bits = argb;
  return value;
}

static dg_value make_length(float number) {
  dg_value value;
  value.size = sizeof(dg_value);
  value.type = DG_VALUE_LENGTH;
  value.number = number;
  value.bits = 0;
  return value;
}

/* One window, one clickable button filling it - the shape both the
 * interactive run and the headless oracle build identically. */
static dg_node_t* build_window_with_button(dg_app_t* app, const char* title, unsigned fill) {
  dg_window_opts opts;
  dg_node_t* button = NULL;
  int32_t status = 0;

  memset(&opts, 0, sizeof(opts));
  opts.size = sizeof(opts);
  opts.title = title;
  opts.width = 320;
  opts.height = 200;
  opts.background_argb = fill;

  dg_window_t* window = dg_window_create(app, &opts);
  if (window == NULL) {
    fprintf(stderr, "dg_window_create(%s) failed: %s\n", title, dg_last_error());
    return NULL;
  }

  button = dg_node_create(app, DG_NODE_TYPE_BUTTON);
  if (button == NULL) {
    fprintf(stderr, "dg_node_create failed: %s\n", dg_last_error());
    return NULL;
  }

  status = dg_window_set_root(window, button);
  if (status != DG_ERR_OK) {
    fprintf(stderr, "dg_window_set_root failed: %d (%s)\n", status, dg_last_error());
    return NULL;
  }

  return button;
}

/* -------------------------------------------------------------------------
 * The interactive run: two real windows, click either button to see it
 * change the OTHER window's background colour - proof that an event
 * (DG_EVENT_CLICK) naming a node and a window round-trips through the ABI
 * into a property write the host itself chose to make.
 * ---------------------------------------------------------------------- */

static int run_interactive(void) {
  dg_app_t* app = dg_app_create(NULL);
  dg_node_t* button_a = NULL;
  dg_node_t* button_b = NULL;

  if (app == NULL) {
    fprintf(stderr, "dg_app_create failed: %s\n", dg_last_error());
    return 1;
  }

  button_a = build_window_with_button(app, "drawgui C client - A", 0xFF14171FU);
  button_b = build_window_with_button(app, "drawgui C client - B", 0xFF1F1417U);
  if (button_a == NULL || button_b == NULL) {
    dg_app_destroy(app);
    return 1;
  }

  printf("two windows open - click either button; close both to exit\n");

  for (;;) {
    dg_event events[16];
    int32_t pending = 0;
    int32_t i = 0;
    int32_t closed_count = 0;

    pending = dg_wait_events(app, 40);
    if (pending < 0) {
      fprintf(stderr, "dg_wait_events failed: %s\n", dg_last_error());
      break;
    }

    pending = dg_poll_events(app, events, 16);
    for (i = 0; i < pending; ++i) {
      if (events[i].kind == DG_EVENT_CLICK) {
        dg_node_t* other = (events[i].node == button_a) ? button_b : button_a;
        dg_value color = make_color(0xFF2E7D32U);
        printf("click at (%d,%d) on node %p - toggling the other window\n", events[i].x,
               events[i].y, (void*)events[i].node);
        (void)dg_node_set_prop(other, DG_PROP_BACKGROUND_COLOR, &color);
      } else if (events[i].kind == DG_EVENT_WINDOW_CLOSED) {
        printf("a window closed\n");
        ++closed_count;
      }
    }
    if (closed_count >= 2) {
      break;
    }
  }

  dg_app_destroy(app);
  return 0;
}

/* -------------------------------------------------------------------------
 * The headless CTest oracle. Every check below is named and printed so a
 * failure says exactly which one it is.
 * ---------------------------------------------------------------------- */

/* A function-local static, not a namespace/file-scope global: the same
 * indirection src/abi/abi_impl.cpp's all_apps()/all_apps_alive() use for
 * the identical clang-tidy finding (cppcoreguidelines-avoid-non-const-
 * global-variables draws a line between the two shapes, even though both
 * are technically static storage duration). */
static int* failure_count(void) {
  static int failures = 0;
  return &failures;
}

static void check(int condition, const char* what) {
  if (condition) {
    printf("  ok: %s\n", what);
  } else {
    printf("  FAIL: %s (dg_last_error: %s)\n", what, dg_last_error());
    ++*failure_count();
  }
}

static int run_verify(void) {
  dg_app_t* app = NULL;
  dg_window_t* window_a = NULL;
  dg_window_t* window_b = NULL;
  dg_node_t* button_a = NULL;
  dg_node_t* button_b = NULL;
  dg_window_opts opts;
  dg_node_t* stray = NULL;
  dg_node_t* stray_child = NULL;
  int32_t status = 0;
  dg_value value;
  int32_t got_click = 0;
  int i = 0;

  printf("drawgui C client - headless verification\n");

  /* 1. Version and app/window lifecycle. */
  check(dg_abi_version() == ((DG_ABI_VERSION_MAJOR << 16) | DG_ABI_VERSION_MINOR),
        "dg_abi_version reports the build's own MAJOR/MINOR");

  app = dg_app_create(NULL);
  check(app != NULL, "dg_app_create succeeds under SDL_VIDEODRIVER=dummy");
  if (app == NULL) {
    return 1;
  }

  memset(&opts, 0, sizeof(opts));
  opts.size = sizeof(opts);
  opts.title = "verify A";
  opts.width = 320;
  opts.height = 200;
  opts.background_argb = 0xFF10141CU;
  window_a = dg_window_create(app, &opts);
  check(window_a != NULL, "dg_window_create opens window A");

  opts.title = "verify B";
  opts.background_argb = 0xFF1C1410U;
  window_b = dg_window_create(app, &opts);
  check(window_b != NULL, "dg_window_create opens window B");

  /* 2. Two windows, each with a clickable button - the acceptance shape. */
  button_a = dg_node_create(app, DG_NODE_TYPE_BUTTON);
  button_b = dg_node_create(app, DG_NODE_TYPE_BUTTON);
  check(button_a != NULL && button_b != NULL, "dg_node_create builds two pending button nodes");

  status = dg_window_set_root(window_a, button_a);
  check(status == DG_ERR_OK, "dg_window_set_root attaches button A");
  status = dg_window_set_root(window_b, button_b);
  check(status == DG_ERR_OK, "dg_window_set_root attaches button B");

  status = dg_window_set_root(window_a, button_b);
  check(status == DG_ERR_ALREADY_ATTACHED,
        "a second dg_window_set_root on the same window is refused");

  /* 3. Property writes on a live node, including a deliberately unknown id. */
  value = make_length(150.0F);
  status = dg_node_set_prop(button_a, DG_PROP_WIDTH, &value);
  check(status == DG_ERR_OK, "dg_node_set_prop(DG_PROP_WIDTH) on a live node succeeds");

  value = make_color(0xFF335577U);
  status = dg_node_set_prop(button_a, DG_PROP_BACKGROUND_COLOR, &value);
  check(status == DG_ERR_OK, "dg_node_set_prop(DG_PROP_BACKGROUND_COLOR) succeeds");

  status = dg_node_set_prop(button_a, 60000, &value);
  check(status == DG_ERR_UNKNOWN_ID, "an unassigned prop_id is DG_ERR_UNKNOWN_ID, not a crash");

  value.type = DG_VALUE_COLOR;
  value.number = 0.0F;
  value.bits = 0;
  status = dg_node_set_prop(button_a, DG_PROP_WIDTH, &value);
  check(status == DG_ERR_TYPE_MISMATCH,
        "a colour sent to a length property is DG_ERR_TYPE_MISMATCH");

  /* 4. insert_before: append, positional insert before a live sibling,
   * a move of an already-live node, and the refusals (cycle, ref that is
   * not a child of parent). */
  stray = dg_node_create(app, DG_NODE_TYPE_BOX);
  stray_child = dg_node_create(app, DG_NODE_TYPE_BOX);
  check(stray != NULL && stray_child != NULL,
        "two more pending box nodes for the insert checks");
  status = dg_node_insert_before(button_a, stray, NULL);
  check(status == DG_ERR_OK, "dg_node_insert_before(parent=live, ref=NULL) appends");
  status = dg_node_insert_before(button_a, stray_child, stray);
  check(status == DG_ERR_OK, "a non-null ref inserts the pending child before it");
  status = dg_node_insert_before(button_a, stray, stray_child);
  check(status == DG_ERR_OK, "a live child is moved before its sibling (reorder)");
  status = dg_node_insert_before(stray, button_a, NULL);
  check(status == DG_ERR_INVALID_ARGUMENT, "moving a node under its own descendant is refused");
  status = dg_node_insert_before(button_a, stray_child, button_a);
  check(status == DG_ERR_INVALID_ARGUMENT, "a ref that is not a child of parent is refused");
  status = dg_node_insert_before(button_a, button_b, NULL);
  check(status == DG_ERR_ALREADY_ATTACHED, "a node live in another window cannot be moved");

  /* 5. dg_dump_layout_tree, on a live node - if it landed, this is where
   * the JSON shape is exercised at all. */
  {
    const char* dump = dg_dump_layout_tree(button_a);
    check(dump != NULL && strstr(dump, "\"type\":\"leaf\"") != NULL,
          "dg_dump_layout_tree reports a leaf and does not crash");
  }

  /* 6. A real click, through the real SDL event queue, via the debug
   * injection hooks - the same mechanism (WindowManager::warp_pointer /
   * post_pointer_button) every other example's own --script mode already
   * uses in C++, exposed here narrowly so THIS oracle can drive a real
   * click without a display. See abi/drawgui.def.toml's own note on why
   * dg_debug_* is a deliberate, narrow exception to "只导出必要面". */
  status = dg_debug_warp_pointer(window_a, 10, 10);
  check(status == DG_ERR_OK, "dg_debug_warp_pointer succeeds");
  status = dg_debug_post_pointer_button(window_a, 1, 10, 10);
  check(status == DG_ERR_OK, "dg_debug_post_pointer_button(down) succeeds");
  status = dg_debug_post_pointer_button(window_a, 0, 10, 10);
  check(status == DG_ERR_OK, "dg_debug_post_pointer_button(up) succeeds");

  got_click = 0;
  for (i = 0; i < 20 && !got_click; ++i) {
    dg_event events[8];
    int32_t pending = dg_wait_events(app, 50);
    int32_t j = 0;
    if (pending < 0) {
      break;
    }
    pending = dg_poll_events(app, events, 8);
    for (j = 0; j < pending; ++j) {
      if (events[j].kind == DG_EVENT_CLICK && events[j].node == button_a &&
          events[j].window == window_a) {
        got_click = 1;
      }
    }
  }
  check(got_click, "a real click through the real SDL event queue arrives as DG_EVENT_CLICK");

  /* 7. Handle validation: dg_node_remove invalidates the HANDLE. Every
   * later call on it is DG_ERR_INVALID_HANDLE - not a crash, and (run under
   * -DDG_SANITIZE=ON) not a use-after-free, because the wrapper this
   * pointer names is never freed - see src/abi/abi_types.h's own comment. */
  status = dg_node_insert_before(stray, stray_child, NULL);
  check(status == DG_ERR_OK, "stray_child moved under stray before removal");
  status = dg_node_remove(stray);
  check(status == DG_ERR_OK, "dg_node_remove succeeds the first time");
  status = dg_node_set_prop(stray_child, DG_PROP_WIDTH, &value);
  check(status == DG_ERR_INVALID_HANDLE,
        "dg_node_remove also invalidates every descendant's handle");
  status = dg_node_remove(stray);
  check(status == DG_ERR_INVALID_HANDLE,
        "dg_node_remove on an already-removed node is invalid-handle");
  status = dg_node_set_prop(stray, DG_PROP_WIDTH, &value);
  check(status == DG_ERR_INVALID_HANDLE,
        "dg_node_set_prop on a removed handle is invalid-handle, not a crash");
  status = dg_node_insert_before(button_a, stray, NULL);
  check(status == DG_ERR_INVALID_HANDLE,
        "dg_node_insert_before with a removed child is invalid-handle");

  /* 8. The exception boundary (design.md section 5.17.1's own risk-register
   * mitigation): dg_debug_trigger_exception unconditionally throws inside
   * dg::abi:: impl code; the generated trampoline must catch it, report the
   * matching error code, and leave the process alive to keep running every
   * check after this one. */
  status = dg_debug_trigger_exception(1);
  check(status == DG_ERR_OOM, "a thrown std::bad_alloc crosses the ABI as DG_ERR_OOM");
  status = dg_debug_trigger_exception(2);
  check(status == DG_ERR_INTERNAL && strstr(dg_last_error(), "deliberate fault") != NULL,
        "a thrown std::runtime_error crosses as DG_ERR_INTERNAL, and dg_last_error() names it");
  status = dg_debug_trigger_exception(3);
  check(status == DG_ERR_INTERNAL,
        "a thrown non-std::exception value still crosses as DG_ERR_INTERNAL");
  printf(
      "  (process is still alive after three injected exceptions - the point of this check)\n");

  /* 9. The theme ABI (7-6, design.md section 5.8 decision 6, declined by
   * name in 6-3): dg_theme_load_memory/dg_theme_set_variant/
   * dg_theme_override/dg_app_set_theme, and DG_VALUE_TOKEN - the value kind
   * dg_node_set_prop() gained so binding a $token reuses the SAME door a
   * literal write already uses, rather than a second, bind-shaped
   * function. */
  {
    static const char kThemeJson[] =
        "{\"schema_version\":1,\"name\":\"c-client-theme\","
        "\"base\":{\"radius.sm\":2,\"radius.md\":4,\"space.sm\":2,\"space.md\":4},"
        "\"variants\":{"
        "\"light\":{\"color.surface\":\"#FFFFFFFF\",\"color.on-surface\":\"#111111FF\","
        "\"color.border\":\"#AAAAAAFF\",\"color.primary\":\"#3366CCFF\","
        "\"color.on-primary\":\"#FFFFFFFF\",\"color.primary-hover\":\"#4A78D6FF\","
        "\"color.primary-pressed\":\"#2952A3FF\",\"color.focus-ring\":\"#FF8800FF\"},"
        "\"dark\":{\"color.surface\":\"#1E1E1EFF\",\"color.on-surface\":\"#E8E8E8FF\","
        "\"color.border\":\"#3A3F45FF\",\"color.primary\":\"#6699FFFF\","
        "\"color.on-primary\":\"#0D1117FF\",\"color.primary-hover\":\"#7FAAFFFF\","
        "\"color.primary-pressed\":\"#5580D9FF\",\"color.focus-ring\":\"#FFA733FF\"}"
        "}}";
    dg_theme_err err;
    dg_theme_t* theme = NULL;
    dg_value token_value;

    memset(&err, 0, sizeof(err));
    err.size = sizeof(err);

    theme = dg_theme_load_dir(app, "/nonexistent/drawgui/theme/package", &err);
    check(theme == NULL && err.status == DG_THEME_ERR_IO_ERROR,
          "dg_theme_load_dir on a missing directory fails with DG_THEME_ERR_IO_ERROR, not a "
          "crash");

    theme = dg_theme_load_memory(app, kThemeJson, (uint32_t)strlen(kThemeJson), NULL, &err);
    check(theme != NULL, "dg_theme_load_memory loads a valid in-memory theme");

    token_value.size = sizeof(token_value);
    token_value.type = DG_VALUE_TOKEN;
    token_value.number = 0.0F;
    token_value.bits = DG_TOKEN_COLOR_PRIMARY;
    status = dg_node_set_prop(button_a, DG_PROP_BACKGROUND_COLOR, &token_value);
    check(status == DG_ERR_NO_ACTIVE_THEME,
          "binding a token before dg_app_set_theme() is DG_ERR_NO_ACTIVE_THEME, not a crash");

    status = dg_app_set_theme(app, theme);
    check(status == DG_ERR_OK, "dg_app_set_theme makes the loaded theme active");

    status = dg_node_set_prop(button_a, DG_PROP_BACKGROUND_COLOR, &token_value);
    check(status == DG_ERR_OK,
          "dg_node_set_prop(DG_VALUE_TOKEN) binds a node's property to a live theme");

    token_value.bits = 60000; /* not a real token_id */
    status = dg_node_set_prop(button_b, DG_PROP_BACKGROUND_COLOR, &token_value);
    check(status == DG_ERR_UNKNOWN_ID, "binding an unassigned token_id is DG_ERR_UNKNOWN_ID");

    status = dg_theme_set_variant(theme, "dark");
    check(status == DG_ERR_OK, "dg_theme_set_variant('dark') succeeds and re-applies live");

    token_value.type = DG_VALUE_FLOAT;
    token_value.number = 42.0F;
    token_value.bits = 0;
    status = dg_theme_override(theme, DG_TOKEN_RADIUS_MD, &token_value);
    check(status == DG_ERR_OK, "dg_theme_override patches an int token's value");

    token_value.type = DG_VALUE_COLOR;
    token_value.bits = 0xFF000000U;
    status = dg_theme_override(theme, DG_TOKEN_RADIUS_MD, &token_value);
    check(status == DG_ERR_TYPE_MISMATCH,
          "dg_theme_override with a colour value for an int token is DG_ERR_TYPE_MISMATCH");
  }

  /* 10. The shortcut/action ABI (8-3d, design.md section 5.5.3):
   * dg_node_scope_action()/dg_shortcut_label()/DG_EVENT_ACTION, exercised
   * end to end - scope a real app-scope action onto a real live node,
   * focus that node with a real click (the same debug hook check 6 already
   * used), post a real SDL key event via dg_debug_post_key(), and pump it
   * through dg::route_key_event()'s level-2 bubbling for real. */
  {
    int32_t got_action = 0;
    int32_t action_count = 0;

    /* Handle validity: a null handle and a REMOVED handle (`stray`, taken
     * out in check 7) both fail cleanly rather than crashing. */
    status = dg_node_scope_action(NULL, DG_ACTION_SCROLL_PAGE_UP);
    check(status == DG_ERR_INVALID_HANDLE, "dg_node_scope_action(NULL, ...) is invalid-handle");
    status = dg_node_scope_action(stray, DG_ACTION_SCROLL_PAGE_UP);
    check(status == DG_ERR_INVALID_HANDLE,
          "dg_node_scope_action on an already-removed node is invalid-handle");

    status = dg_node_scope_action(button_a, DG_ACTION_SCROLL_PAGE_UP);
    check(status == DG_ERR_OK, "dg_node_scope_action(button_a, SCROLL_PAGE_UP) succeeds");

    /* Focus button_a via a real click - the same debug injection check 6
     * already used, but this time the click's SIDE EFFECT this test cares
     * about is dg::Focus::set(), not the DG_EVENT_CLICK it also produces.
     * Drain that click event so it cannot be mistaken for the action event
     * this check looks for below. */
    dg_debug_warp_pointer(window_a, 10, 10);
    dg_debug_post_pointer_button(window_a, 1, 10, 10);
    dg_debug_post_pointer_button(window_a, 0, 10, 10);
    for (i = 0; i < 20; ++i) {
      dg_event drain[8];
      int32_t pending = dg_wait_events(app, 20);
      if (pending < 0) {
        break;
      }
      pending = dg_poll_events(app, drain, 8);
      if (pending == 0 && i > 2) {
        break;
      }
    }

    /* A modifier chord and an unknown key name are both refused - see
     * dg_debug_post_key's own def.toml summary for why (post_logical_key()
     * itself has no modifier parameter). */
    status = dg_debug_post_key(window_a, 1, "Mod+C");
    check(status == DG_ERR_INVALID_ARGUMENT,
          "dg_debug_post_key refuses a modifier chord it cannot inject");
    status = dg_debug_post_key(window_a, 1, "NotARealKey");
    check(status == DG_ERR_INVALID_ARGUMENT, "dg_debug_post_key refuses an unknown key name");

    status = dg_debug_post_key(window_a, 1, "PageUp");
    check(status == DG_ERR_OK, "dg_debug_post_key(down, \"PageUp\") succeeds");
    status = dg_debug_post_key(window_a, 0, "PageUp");
    check(status == DG_ERR_OK, "dg_debug_post_key(up, \"PageUp\") succeeds");

    for (i = 0; i < 20; ++i) {
      dg_event events[8];
      int32_t pending = dg_wait_events(app, 50);
      int32_t j = 0;
      if (pending < 0) {
        break;
      }
      pending = dg_poll_events(app, events, 8);
      for (j = 0; j < pending; ++j) {
        if (events[j].kind == DG_EVENT_ACTION) {
          ++action_count;
          if (events[j].action_id == DG_ACTION_SCROLL_PAGE_UP && events[j].node == button_a &&
              events[j].window == window_a) {
            got_action = 1;
          }
        }
      }
      if (action_count >= 1 && i > 5) {
        break;
      }
    }
    check(got_action,
          "a scoped action, focused and keyed for real, arrives as DG_EVENT_ACTION naming its "
          "action_id and target node");
    check(action_count == 1,
          "the key-up half of the same posted key does not fire a second DG_EVENT_ACTION "
          "(only KeyAction::kDown reaches the router)");

    /* dg_shortcut_label(): the generated binding table's own text, wrapped
     * rather than reimplemented (chord.h's format_chord(), already
     * unit-tested). An action_id no binding names returns NULL - a real
     * lookup failure, not "this action has no accelerator" (which would be
     * an empty string, a different claim this ABI does not need to make). */
    {
      const char* label = dg_shortcut_label(DG_ACTION_SCROLL_PAGE_UP);
      check(label != NULL && strcmp(label, "PageUp") == 0,
            "dg_shortcut_label(SCROLL_PAGE_UP) reports the generated binding's own text");
    }
    check(dg_shortcut_label(60000) == NULL,
          "dg_shortcut_label with an unassigned action_id returns NULL, not a crash");
  }

  /* 11. P6 sub-step a1: DG_NODE_TYPE_LABEL, dg_app_add_font() and
   * dg_node_set_text() - the C ABI's first path from a pure C caller to a
   * visible glyph on screen. Uses tests/fonts/gen_test_fonts.py's own
   * deterministic fonts (this example's own copy, generated by
   * examples/19_c_client/CMakeLists.txt into DRAWGUI_C_CLIENT_FONT_DIR) so
   * this check is reproducible on any machine, not a function of whichever
   * system fonts happen to be installed. */
  {
    dg_window_opts label_opts;
    dg_window_t* window_c = NULL;
    dg_node_t* label = NULL;
    dg_value color;
    /* "Hello, " is ASCII (DgTest Latin's own coverage, 0x20..0x7E) and
     * U+4E2D ("中") is covered ONLY by DgTest Han Hans/Han Ja, not by the
     * primary family - so a non-tofu glyph for it proves the fallback
     * chain actually walked past the named family, not merely that UTF-8
     * decoding did not crash. Encoded by hand (rather than a wide-string
     * literal, which a strict C11 TU has no portable way to re-encode as
     * UTF-8) as its three UTF-8 bytes: 0xE4 0xB8 0xAD. */
    static const char kLabelText[] = "Hello, \xE4\xB8\xAD!";

    status = dg_app_add_font(NULL, DRAWGUI_C_CLIENT_FONT_DIR, "DgTest Latin");
    check(status == DG_ERR_INVALID_HANDLE, "dg_app_add_font(NULL, ...) is invalid-handle");

    status = dg_app_add_font(app, DRAWGUI_C_CLIENT_FONT_DIR, "DgTest No Such Family");
    check(status == DG_ERR_INVALID_ARGUMENT,
          "dg_app_add_font fails for a family this machine does not have - never a silent "
          "substitution");

    status = dg_app_add_font(app, DRAWGUI_C_CLIENT_FONT_DIR, "DgTest Latin");
    check(status == DG_ERR_OK, "dg_app_add_font succeeds for a real family");

    /* This app's font catalog is captured fresh by EACH window's own
     * dg_window_create() call (abi/drawgui.def.toml's own note on
     * dg_app_add_font) - window_a/window_b, already open above, never see
     * this font; a NEW window does. */
    memset(&label_opts, 0, sizeof(label_opts));
    label_opts.size = sizeof(label_opts);
    label_opts.title = "drawgui C client - label";
    label_opts.width = 320;
    label_opts.height = 120;
    label_opts.background_argb = 0xFFFFFFFFU;
    window_c = dg_window_create(app, &label_opts);
    check(window_c != NULL, "dg_window_create opens window C, with this app's fonts attached");

    label = dg_node_create(app, DG_NODE_TYPE_LABEL);
    check(label != NULL, "dg_node_create(DG_NODE_TYPE_LABEL) builds a pending label node");

    status = dg_window_set_root(window_c, label);
    check(status == DG_ERR_OK, "dg_window_set_root attaches the label");

    status = dg_node_set_text(label, DG_PROP_TEXT, kLabelText,
                              (uint32_t)(sizeof(kLabelText) - 1));
    check(status == DG_ERR_OK,
          "dg_node_set_text sets a UTF-8 string mixing ASCII and a fallback-only CJK glyph");

    status = dg_node_set_text(label, DG_PROP_WIDTH, kLabelText,
                              (uint32_t)(sizeof(kLabelText) - 1));
    check(status == DG_ERR_TYPE_MISMATCH,
          "dg_node_set_text with a prop_id that is not DG_PROP_TEXT is type-mismatch");

    color.size = sizeof(color);
    color.type = DG_VALUE_COLOR;
    color.number = 0.0F;
    color.bits = 0xFF224488U;
    status = dg_node_set_prop(label, DG_PROP_TEXT_COLOR, &color);
    check(status == DG_ERR_OK, "dg_node_set_prop(DG_PROP_TEXT_COLOR) rides the ordinary scalar "
                               "path, beside dg_node_set_text's dedicated one");

    /* Force a real paint pass, then confirm one actually happened: the
     * window's RasterSurface only exists after at least one repaint
     * (abi_impl.cpp's ensure_surface_current()/repaint_and_present()), and
     * dg_dump_layout_tree() gives this pure-C oracle a way to see the
     * label's own resolved box without a debug-only pixel-capture hook. */
    dg_wait_events(app, 20);
    {
      const char* dump = dg_dump_layout_tree(label);
      check(dump != NULL && strstr(dump, "\"type\":\"leaf\"") != NULL,
            "the label's own subtree dumps as a leaf, after a real paint pass");
    }
  }

  /* 12. P6 sub-step a2: DG_NODE_TYPE_CHECKBOX - build one, confirm it
   * starts unchecked, click it through the same debug pointer injection
   * check 6 already uses, confirm the click both fires DG_EVENT_CLICK and
   * flips the checked bit, then click again and confirm it flips back. */
  {
    dg_window_opts checkbox_opts;
    dg_window_t* window_d = NULL;
    dg_node_t* checkbox = NULL;
    int32_t checked;

    memset(&checkbox_opts, 0, sizeof(checkbox_opts));
    checkbox_opts.size = sizeof(checkbox_opts);
    checkbox_opts.title = "drawgui C client - checkbox";
    checkbox_opts.width = 64;
    checkbox_opts.height = 64;
    checkbox_opts.background_argb = 0xFFFFFFFFU;
    window_d = dg_window_create(app, &checkbox_opts);
    check(window_d != NULL, "dg_window_create opens window D, for the checkbox");

    checkbox = dg_node_create(app, DG_NODE_TYPE_CHECKBOX);
    check(checkbox != NULL, "dg_node_create(DG_NODE_TYPE_CHECKBOX) builds a pending checkbox node");

    status = dg_window_set_root(window_d, checkbox);
    check(status == DG_ERR_OK, "dg_window_set_root attaches the checkbox");

    checked = dg_node_get_checked(checkbox);
    check(checked == 0, "a freshly attached checkbox starts unchecked");

    /* attach_pending_node()'s own DG_NODE_TYPE_CHECKBOX box is 32x32 at
     * (0,0) - the checkbox is this window's sole content root - so its
     * centre is (16, 16) regardless of window_d's own 64x64 size. */
    status = dg_debug_warp_pointer(window_d, 16, 16);
    check(status == DG_ERR_OK, "dg_debug_warp_pointer onto the checkbox succeeds");
    status = dg_debug_post_pointer_button(window_d, 1, 16, 16);
    check(status == DG_ERR_OK, "dg_debug_post_pointer_button(down) on the checkbox succeeds");
    status = dg_debug_post_pointer_button(window_d, 0, 16, 16);
    check(status == DG_ERR_OK, "dg_debug_post_pointer_button(up) on the checkbox succeeds");

    got_click = 0;
    for (i = 0; i < 20 && !got_click; ++i) {
      dg_event events[8];
      int32_t pending = dg_wait_events(app, 50);
      int32_t j = 0;
      if (pending < 0) {
        break;
      }
      pending = dg_poll_events(app, events, 8);
      for (j = 0; j < pending; ++j) {
        if (events[j].kind == DG_EVENT_CLICK && events[j].node == checkbox) {
          got_click = 1;
        }
      }
    }
    check(got_click, "a real click on the checkbox arrives as DG_EVENT_CLICK");

    checked = dg_node_get_checked(checkbox);
    check(checked == 1, "the click that produced DG_EVENT_CLICK also checked the box");

    status = dg_debug_warp_pointer(window_d, 16, 16);
    check(status == DG_ERR_OK, "dg_debug_warp_pointer onto the checkbox succeeds again");
    status = dg_debug_post_pointer_button(window_d, 1, 16, 16);
    check(status == DG_ERR_OK, "dg_debug_post_pointer_button(down) succeeds on the second click");
    status = dg_debug_post_pointer_button(window_d, 0, 16, 16);
    check(status == DG_ERR_OK, "dg_debug_post_pointer_button(up) succeeds on the second click");
    dg_wait_events(app, 50);

    checked = dg_node_get_checked(checkbox);
    check(checked == 0, "a second click unchecks the box again");

    status = dg_node_get_checked(NULL);
    check(status == DG_ERR_INVALID_HANDLE, "dg_node_get_checked(NULL) is invalid-handle");
    status = dg_node_get_checked(button_a);
    check(status == DG_ERR_UNSUPPORTED,
          "dg_node_get_checked on a non-checkbox live node is unsupported");
  }

  /* 13. P6 sub-step a2 (second half): DG_NODE_TYPE_TEXT_FIELD - build one,
   * click it (focusing it and starting real text input), type "abc" one
   * committed character at a time through dg_debug_post_text_input(),
   * confirm each commit fires its own DG_EVENT_TEXT_CHANGED, read the
   * result back with dg_node_get_text()'s two-trip shape (cap=0 to ask the
   * length, then a real buffer), then Backspace once and confirm the text
   * shortens by exactly one character. */
  {
    dg_window_opts field_opts;
    dg_window_t* window_e = NULL;
    dg_node_t* field = NULL;
    uint32_t out_len = 0;
    char buf[16];
    int text_changed_count;

    memset(&field_opts, 0, sizeof(field_opts));
    field_opts.size = sizeof(field_opts);
    field_opts.title = "drawgui C client - text field";
    field_opts.width = 280;
    field_opts.height = 56;
    field_opts.background_argb = 0xFF0E1218U;
    window_e = dg_window_create(app, &field_opts);
    check(window_e != NULL, "dg_window_create opens window E, for the text field");

    field = dg_node_create(app, DG_NODE_TYPE_TEXT_FIELD);
    check(field != NULL, "dg_node_create(DG_NODE_TYPE_TEXT_FIELD) builds a pending field node");

    status = dg_window_set_root(window_e, field);
    check(status == DG_ERR_OK, "dg_window_set_root attaches the text field");

    status = dg_node_get_text(field, NULL, 0, &out_len);
    check(status == DG_ERR_OK && out_len == 0,
          "a freshly attached text field starts empty (cap=0 length probe)");

    /* attach_pending_node()'s own DG_NODE_TYPE_TEXT_FIELD box is 260x36 at
     * (0,0) - the field is this window's sole content root - so its centre
     * is (130, 18) regardless of window_e's own 280x56 size. This click
     * focuses it (process_pointer_event()'s dg::Focus::set()) and, because
     * dg_window_create() already called WindowManager::start_text_input()
     * once for this window's whole lifetime, is what gives the
     * dg_debug_post_text_input() calls below somewhere real to land. */
    status = dg_debug_warp_pointer(window_e, 130, 18);
    check(status == DG_ERR_OK, "dg_debug_warp_pointer onto the text field succeeds");
    status = dg_debug_post_pointer_button(window_e, 1, 130, 18);
    check(status == DG_ERR_OK, "dg_debug_post_pointer_button(down) on the text field succeeds");
    status = dg_debug_post_pointer_button(window_e, 0, 130, 18);
    check(status == DG_ERR_OK, "dg_debug_post_pointer_button(up) on the text field succeeds");
    dg_wait_events(app, 50);

    text_changed_count = 0;
    {
      const char* chars[3] = {"a", "b", "c"};
      int c;
      for (c = 0; c < 3; ++c) {
        status = dg_debug_post_text_input(window_e, chars[c]);
        check(status == DG_ERR_OK, "dg_debug_post_text_input succeeds");
        for (i = 0; i < 20; ++i) {
          dg_event events[8];
          int32_t pending = dg_wait_events(app, 50);
          int32_t j = 0;
          if (pending < 0) {
            break;
          }
          pending = dg_poll_events(app, events, 8);
          for (j = 0; j < pending; ++j) {
            if (events[j].kind == DG_EVENT_TEXT_CHANGED && events[j].node == field) {
              ++text_changed_count;
            }
          }
          if (pending == 0) {
            break;
          }
        }
      }
    }
    check(text_changed_count == 3,
          "typing \"abc\" one commit at a time fires exactly 3 DG_EVENT_TEXT_CHANGED");

    out_len = 0;
    status = dg_node_get_text(field, NULL, 0, &out_len);
    check(status == DG_ERR_OK && out_len == 3,
          "dg_node_get_text(cap=0) reports the field's real length (3) without touching buf");

    memset(buf, 0, sizeof(buf));
    status = dg_node_get_text(field, buf, (uint32_t)sizeof(buf), NULL);
    check(status == DG_ERR_OK && strcmp(buf, "abc") == 0,
          "dg_node_get_text(cap=sizeof(buf)) copies the committed text (\"abc\")");

    status = dg_debug_post_text_field_key(window_e, 1, "Backspace", 0);
    check(status == DG_ERR_OK, "dg_debug_post_text_field_key(down, Backspace) on the focused text field succeeds");
    status = dg_debug_post_text_field_key(window_e, 0, "Backspace", 0);
    check(status == DG_ERR_OK, "dg_debug_post_text_field_key(up, Backspace) succeeds");
    dg_wait_events(app, 50);

    memset(buf, 0, sizeof(buf));
    out_len = 0;
    status = dg_node_get_text(field, buf, (uint32_t)sizeof(buf), &out_len);
    check(status == DG_ERR_OK && out_len == 2 && strcmp(buf, "ab") == 0,
          "Backspace shortens the field's text from \"abc\" to \"ab\"");

    status = dg_node_get_text(NULL, buf, (uint32_t)sizeof(buf), &out_len);
    check(status == DG_ERR_INVALID_HANDLE, "dg_node_get_text(NULL) is invalid-handle");
    status = dg_node_get_text(button_a, buf, (uint32_t)sizeof(buf), &out_len);
    check(status == DG_ERR_UNSUPPORTED,
          "dg_node_get_text on a non-text-field live node is unsupported");
  }

  /* 14. ABI 0.2 window operations on window B: a title round-trip, a resize
   * that comes back as DG_EVENT_WINDOW_RESIZED, and the state bitmask. */
  {
    char title[16];
    uint32_t title_len = 0;
    int32_t width = 0;
    int32_t height = 0;
    dg_event events[32];
    int32_t count = 0;
    int32_t saw_resize = 0;

    status = dg_window_set_title(window_b, "renamed B");
    check(status == DG_ERR_OK, "dg_window_set_title renames window B");
    memset(title, 0, sizeof(title));
    status = dg_window_get_title(window_b, title, (uint32_t)sizeof(title) - 1U, &title_len);
    check(status == DG_ERR_OK && title_len == 9 && strcmp(title, "renamed B") == 0,
          "dg_window_get_title reads the new title back");

    status = dg_window_set_size(window_b, 300, 180);
    check(status == DG_ERR_OK, "dg_window_set_size resizes window B");
    status = dg_window_get_size(window_b, &width, &height);
    check(status == DG_ERR_OK && width == 300 && height == 180,
          "dg_window_get_size reports the new size");
    dg_wait_events(app, 50);
    count = dg_poll_events(app, events, 32);
    for (i = 0; i < count; ++i) {
      if (events[i].kind == DG_EVENT_WINDOW_RESIZED && events[i].window == window_b &&
          events[i].x == 300 && events[i].y == 180) {
        saw_resize = 1;
      }
    }
    check(saw_resize, "the resize is reported as DG_EVENT_WINDOW_RESIZED with the new size");

    status = dg_window_get_state(window_b);
    check(status >= 0 && (status & DG_WINDOW_STATE_FULLSCREEN) == 0,
          "dg_window_get_state returns a bitmask; window B is not fullscreen");
    status = dg_window_set_min_size(window_b, 400, 10);
    check(status == DG_ERR_OK, "dg_window_set_min_size accepts a limit");
    status = dg_window_set_max_size(window_b, 200, 200);
    check(status == DG_ERR_UNSUPPORTED,
          "a maximum below the minimum is refused with DG_ERR_UNSUPPORTED");
  }

  /* 15. Teardown safety: dg_app_destroy must not crash even though window_a/
   * window_b/button_a/button_b/stray_child handles are all still held by
   * this function, and a handle used AFTER destroy must report
   * DG_ERR_INVALID_HANDLE exactly like an individually removed node does
   * (check 7) - the app-level instance of the same design. */
  dg_debug_post_pointer_button(window_a, 1, -1, -1); /* harmless: outside any node */
  dg_debug_post_pointer_button(window_a, 0, -1, -1);

  dg_app_destroy(app);
  status = dg_node_set_prop(button_a, DG_PROP_WIDTH, &value);
  check(status == DG_ERR_INVALID_HANDLE,
        "using a node handle after dg_app_destroy is invalid-handle");

  printf("%d check(s) failed\n", *failure_count());
  return *failure_count() == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
  if (argc > 1 && strcmp(argv[1], "--verify-c-client") == 0) {
    return run_verify();
  }
  if (argc > 1) {
    fprintf(stderr, "usage: %s [--verify-c-client]\n", argv[0]);
    return 2;
  }
  return run_interactive();
}
