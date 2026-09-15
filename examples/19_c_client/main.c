/* examples/19_c_client - a genuinely pure C program against drawgui's C ABI.
 *
 * This is P5's own acceptance criterion (design.md section 5.8, .omo/plans/
 * drawgui-phase6.md's 6-3): create two windows, respond to clicks, all
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
 * failure says exactly which one - see doc/abi.md section 8 for the full
 * list this mirrors.
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

  /* 4. insert_before is append-only this slice: a real `ref` is refused by
   * name (DG_ERR_UNSUPPORTED) rather than silently reordering the wrong
   * way - see abi/drawgui.def.toml's own header comment. */
  stray = dg_node_create(app, DG_NODE_TYPE_BOX);
  stray_child = dg_node_create(app, DG_NODE_TYPE_BOX);
  check(stray != NULL && stray_child != NULL,
        "two more pending box nodes for the insert checks");
  status = dg_node_insert_before(button_a, stray, NULL);
  check(status == DG_ERR_OK, "dg_node_insert_before(parent=live, ref=NULL) appends");
  status = dg_node_insert_before(button_a, stray_child, stray);
  check(status == DG_ERR_UNSUPPORTED, "a non-null ref is DG_ERR_UNSUPPORTED (append-only)");
  /* stray_child is still pending; attach it so the arena has no dangling
   * pending node left over (not required for correctness, just tidy). */
  status = dg_node_insert_before(button_a, stray_child, NULL);
  check(status == DG_ERR_OK, "the same child attaches fine once ref is NULL");

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
  status = dg_node_remove(stray);
  check(status == DG_ERR_OK, "dg_node_remove succeeds the first time");
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

  /* 9. Teardown safety: dg_app_destroy must not crash even though window_a/
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
