/*
 * examples/28_c_components - the ABI 0.2 phase B component layer from pure
 * C11: a context menu, a dropdown, tooltips, a modal dialog, a slider, a radio
 * group, a progress bar, tabs holding a tree, and a sortable table, all
 * driven by the poll-based event loop (no callbacks).
 *
 *   drawgui_c_components            an interactive window
 *   drawgui_c_components --verify   the headless CTest oracle (SDL dummy
 *                                   driver, set by CTest): drives every
 *                                   component with dg_debug_* input and
 *                                   checks the events and node counts
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drawgui/abi/drawgui.h"

#define ROWS 40
#define COLUMNS 2

static int g_failures = 0;

static void check(int ok, const char* what) {
  printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) {
    ++g_failures;
  }
}

static dg_value length(float px) {
  dg_value v;
  memset(&v, 0, sizeof v);
  v.size = sizeof v;
  v.type = DG_VALUE_LENGTH;
  v.number = px;
  return v;
}

static dg_value enum_value(uint32_t ordinal, uint32_t type) {
  dg_value v;
  memset(&v, 0, sizeof v);
  v.size = sizeof v;
  v.type = type;
  v.bits = ordinal;
  return v;
}

static dg_value float_value(float number) {
  dg_value v;
  memset(&v, 0, sizeof v);
  v.size = sizeof v;
  v.type = DG_VALUE_FLOAT;
  v.number = number;
  return v;
}

static void set(dg_node_t* node, uint16_t prop, dg_value value) {
  (void)dg_node_set_prop(node, prop, &value);
}

static dg_node_t* add(dg_app_t* app, dg_node_t* parent, uint16_t type) {
  dg_node_t* node = dg_node_create(app, type);
  (void)dg_node_insert_before(parent, node, NULL);
  return node;
}

/* The table's data, owned by the app: drawgui copies it on every push. */
typedef struct {
  char name[ROWS][16];
  char size[ROWS][16];
  int order[ROWS];
  int sort_column;
  int ascending;
} TableData;

static int row_size(int row) {
  return (row * 7919) % 1000;
}

static void push_rows(dg_node_t* table, TableData* data) {
  const char* cells[ROWS * COLUMNS];
  int i;
  for (i = 0; i < ROWS; ++i) {
    cells[(i * COLUMNS) + 0] = data->name[data->order[i]];
    cells[(i * COLUMNS) + 1] = data->size[data->order[i]];
  }
  (void)dg_table_set_cells(table, cells, ROWS);
}

static void sort_rows(dg_node_t* table, TableData* data, int column) {
  int i;
  int j;
  data->ascending = data->sort_column == column ? !data->ascending : 1;
  data->sort_column = column;
  for (i = 1; i < ROWS; ++i) { /* insertion sort, stable */
    const int key = data->order[i];
    for (j = i - 1; j >= 0; --j) {
      const int a = data->order[j];
      int greater = column == 0 ? a > key : row_size(a) > row_size(key);
      if (!data->ascending) {
        greater = column == 0 ? a < key : row_size(a) < row_size(key);
      }
      if (!greater) {
        break;
      }
      data->order[j + 1] = a;
    }
    data->order[j + 1] = key;
  }
  push_rows(table, data);
  (void)dg_table_set_sort_indicator(table, column, data->ascending);
}

typedef struct {
  dg_app_t* app;
  dg_window_t* window;
  dg_node_t* root;
  dg_node_t* menu_button;
  dg_node_t* dialog_button;
  dg_node_t* dropdown;
  dg_node_t* slider;
  dg_node_t* radios[2];
  dg_node_t* progress;
  dg_node_t* tabs;
  dg_node_t* tree;
  dg_node_t* table;
  TableData data;
} Scene;

static const char* const kMenuLabels[] = {"Copy", "", "Paste", "Select all"};
static const uint32_t kMenuFlags[] = {0, DG_MENU_ITEM_SEPARATOR, DG_MENU_ITEM_DISABLED,
                                      DG_MENU_ITEM_CHECKABLE};

static int build(Scene* s, const char* font_dir) {
  dg_window_opts opts;
  dg_node_t* row;
  dg_node_t* tree_panel;
  dg_node_t* about_panel;
  dg_node_t* about;
  int i;
  static const char* const fruits[] = {"Apple", "Banana", "Cherry"};
  static const char* const tree_labels[] = {"src", "main.c", "util.c", "doc", "guide.md"};
  static const int32_t tree_depths[] = {0, 1, 1, 0, 1};
  static const uint32_t tree_flags[] = {DG_TREE_ITEM_EXPANDED, 0, 0, 0, 0};
  static const char* const titles[] = {"Name", "Size"};
  static const float widths[] = {60.0F, 0.0F};
  static const uint32_t units[] = {DG_LENGTH_UNIT_PERCENT, DG_LENGTH_UNIT_PX};
  static const uint32_t column_flags[] = {DG_TABLE_COLUMN_SORTABLE, DG_TABLE_COLUMN_SORTABLE};

  s->app = dg_app_create(NULL);
  if (s->app == NULL || dg_app_add_font(s->app, font_dir, "DgTest Latin") != DG_ERR_OK) {
    fprintf(stderr, "could not start: %s\n", dg_last_error());
    return 0;
  }
  memset(&opts, 0, sizeof opts);
  opts.size = sizeof opts;
  opts.title = "drawgui C components";
  opts.width = 760;
  opts.height = 420;
  opts.background_argb = 0xFF1B1F27U;
  s->window = dg_window_create(s->app, &opts);
  if (s->window == NULL) {
    fprintf(stderr, "could not open a window: %s\n", dg_last_error());
    return 0;
  }
  s->root = dg_node_create(s->app, DG_NODE_TYPE_BOX);
  (void)dg_window_set_root(s->window, s->root);
  set(s->root, DG_PROP_WIDTH, length(760));
  set(s->root, DG_PROP_HEIGHT, length(420));
  set(s->root, DG_PROP_DIRECTION, enum_value(DG_DIRECTION_COLUMN, DG_VALUE_ENUM));
  set(s->root, DG_PROP_GAP, float_value(10));

  row = add(s->app, s->root, DG_NODE_TYPE_BOX);
  set(row, DG_PROP_DIRECTION, enum_value(DG_DIRECTION_ROW, DG_VALUE_ENUM));
  set(row, DG_PROP_GAP, float_value(10));
  set(row, DG_PROP_WIDTH, length(740));
  set(row, DG_PROP_HEIGHT, length(40));
  s->menu_button = add(s->app, row, DG_NODE_TYPE_BUTTON);
  set(s->menu_button, DG_PROP_WIDTH, length(140));
  set(s->menu_button, DG_PROP_HEIGHT, length(32));
  (void)dg_node_set_text(s->menu_button, DG_PROP_TEXT, "Menu", 4);
  (void)dg_node_set_tooltip(s->menu_button, "Opens a menu (right-click anywhere too)");
  s->dialog_button = add(s->app, row, DG_NODE_TYPE_BUTTON);
  set(s->dialog_button, DG_PROP_WIDTH, length(140));
  set(s->dialog_button, DG_PROP_HEIGHT, length(32));
  (void)dg_node_set_text(s->dialog_button, DG_PROP_TEXT, "Dialog", 6);
  s->dropdown = add(s->app, row, DG_NODE_TYPE_DROPDOWN);
  (void)dg_dropdown_set_items(s->dropdown, fruits, 3);
  (void)dg_node_set_text(s->dropdown, DG_PROP_TEXT, "Pick a fruit", 12);
  /* The slider drives the progress bar; the two radios share group 0. */
  s->slider = add(s->app, row, DG_NODE_TYPE_SLIDER);
  set(s->slider, DG_PROP_WIDTH, length(120));
  set(s->slider, DG_PROP_VALUE, float_value(25.0F));
  for (i = 0; i < 2; ++i) {
    s->radios[i] = add(s->app, row, DG_NODE_TYPE_RADIO);
  }
  (void)dg_radio_select(s->radios[0]);

  s->progress = add(s->app, s->root, DG_NODE_TYPE_PROGRESS);
  set(s->progress, DG_PROP_WIDTH, length(740));
  set(s->progress, DG_PROP_VALUE, float_value(0.25F));

  row = add(s->app, s->root, DG_NODE_TYPE_BOX);
  set(row, DG_PROP_DIRECTION, enum_value(DG_DIRECTION_ROW, DG_VALUE_ENUM));
  set(row, DG_PROP_GAP, float_value(10));
  set(row, DG_PROP_WIDTH, length(740));
  set(row, DG_PROP_HEIGHT, length(320));
  s->tabs = add(s->app, row, DG_NODE_TYPE_TABS);
  set(s->tabs, DG_PROP_HEIGHT, length(320));
  tree_panel = dg_tabs_add(s->tabs, "Files", 0);
  (void)dg_tabs_add(s->tabs, "Locked", 1);
  about_panel = dg_tabs_add(s->tabs, "About", 0);
  s->tree = add(s->app, tree_panel, DG_NODE_TYPE_TREE);
  set(s->tree, DG_PROP_HEIGHT, length(270));
  (void)dg_tree_set_items(s->tree, tree_labels, tree_depths, tree_flags, 5);
  about = add(s->app, about_panel, DG_NODE_TYPE_LABEL);
  (void)dg_node_set_text(about, DG_PROP_TEXT, "Pure C, poll-based", 18);

  s->table = add(s->app, row, DG_NODE_TYPE_TABLE);
  set(s->table, DG_PROP_HEIGHT, length(320));
  set(s->table, DG_PROP_WIDTH, length(400));
  (void)dg_table_set_columns(s->table, titles, widths, units, column_flags, COLUMNS);
  for (i = 0; i < ROWS; ++i) {
    snprintf(s->data.name[i], sizeof s->data.name[i], "item %02d", i);
    snprintf(s->data.size[i], sizeof s->data.size[i], "%d", row_size(i));
    s->data.order[i] = i;
  }
  s->data.sort_column = -1;
  push_rows(s->table, &s->data);
  return 1;
}

/* What the app does with one event - shared by the interactive loop and
 * --verify. Returns 0 when the window closed. */
static int react(Scene* s, const dg_event* e, int verbose) {
  static const char* const kButtons[] = {"Save", "Discard", "Cancel"};
  switch (e->kind) {
    case DG_EVENT_WINDOW_CLOSED:
      return 0;
    case DG_EVENT_CLICK:
      if (e->node == s->menu_button) {
        (void)dg_menu_open(s->window, s->menu_button, 0, 0, kMenuLabels, kMenuFlags, 4);
      } else if (e->node == s->dialog_button) {
        (void)dg_dialog_open(s->window, "Unsaved changes", "Save before closing?", kButtons, 3,
                             0, 2);
      }
      break;
    case DG_EVENT_CONTEXT_CLICK:
      (void)dg_menu_open(s->window, NULL, e->x, e->y, kMenuLabels, kMenuFlags, 4);
      break;
    case DG_EVENT_TABLE_SORT:
      sort_rows(s->table, &s->data, e->x);
      break;
    case DG_EVENT_VALUE_CHANGED:
      set(s->progress, DG_PROP_VALUE, float_value((float)(e->x + 1) / 3.0F));
      break;
    case DG_EVENT_SLIDER_CHANGED: {
      float value = 0.0F;
      if (dg_slider_get_value(e->node, &value) == DG_ERR_OK) {
        set(s->progress, DG_PROP_VALUE, float_value(value / 100.0F));
      }
      break;
    }
    default:
      break;
  }
  if (verbose && e->kind >= DG_EVENT_MENU_ACTIVATE) {
    printf("event %u: x=%d y=%d\n", (unsigned)e->kind, (int)e->x, (int)e->y);
  }
  return 1;
}

/* Pumps a few rounds; returns how many events of `kind` arrived (and the
 * last one in *last). */
static int pump(Scene* s, uint32_t kind, dg_event* last) {
  int seen = 0;
  int round;
  for (round = 0; round < 3; ++round) {
    dg_event events[32];
    int32_t n;
    int32_t i;
    (void)dg_wait_events(s->app, 5);
    n = dg_poll_events(s->app, events, 32);
    for (i = 0; i < n; ++i) {
      (void)react(s, &events[i], 0);
      if (events[i].kind == kind) {
        ++seen;
        if (last != NULL) {
          *last = events[i];
        }
      }
    }
  }
  return seen;
}

static void press(Scene* s, int x, int y, int32_t button) {
  (void)dg_debug_warp_pointer(s->window, x, y);
  (void)dg_debug_post_pointer_button_ex(s->window, 1, x, y, button, 0);
  (void)dg_debug_post_pointer_button_ex(s->window, 0, x, y, button, 0);
}

static void key(Scene* s, const char* name) {
  (void)dg_debug_post_text_field_key(s->window, 1, name, 0);
  (void)dg_debug_post_text_field_key(s->window, 0, name, 0);
}

static int run_verify(const char* font_dir) {
  Scene s;
  dg_event e;
  int baseline;
  memset(&s, 0, sizeof s);
  printf("C components verify\n");
  if (!build(&s, font_dir)) {
    return 1;
  }
  (void)pump(&s, 0, NULL);
  baseline = dg_debug_node_count(s.window);

  /* The Menu button opens an anchored menu; Down + Enter picks "Select all". */
  press(&s, 70, 16, DG_POINTER_BUTTON_PRIMARY);
  (void)pump(&s, 0, NULL);
  check(dg_debug_node_count(s.window) > baseline, "the menu button opened a menu");
  key(&s, "Down");
  key(&s, "Enter");
  check(pump(&s, DG_EVENT_MENU_ACTIVATE, &e) == 1 && e.x == 3 && e.node == s.menu_button,
        "Down + Enter activates item 3, reported against its anchor");
  check(dg_debug_node_count(s.window) == baseline, "closing the menu frees its nodes");

  /* A right-click opens a context menu; Escape dismisses it. */
  press(&s, 700, 400, DG_POINTER_BUTTON_SECONDARY);
  (void)pump(&s, 0, NULL);
  key(&s, "Escape");
  check(pump(&s, DG_EVENT_MENU_DISMISS, NULL) == 1, "Escape dismisses the context menu");

  /* The dropdown: open, choose; the app moves the progress bar. */
  press(&s, 400, 16, DG_POINTER_BUTTON_PRIMARY);
  (void)pump(&s, 0, NULL);
  key(&s, "Down");
  key(&s, "Enter");
  check(pump(&s, DG_EVENT_VALUE_CHANGED, &e) == 1 && e.node == s.dropdown && e.y == -1,
        "choosing an option reports VALUE_CHANGED");
  check(dg_dropdown_get_selected(s.dropdown) == e.x, "dg_dropdown_get_selected agrees");

  /* The dialog is modal: Escape answers its cancel button. */
  press(&s, 220, 16, DG_POINTER_BUTTON_PRIMARY);
  (void)pump(&s, 0, NULL);
  press(&s, 70, 16, DG_POINTER_BUTTON_PRIMARY);
  check(pump(&s, DG_EVENT_CLICK, NULL) == 0, "the dialog blocks clicks behind it");
  key(&s, "Escape");
  check(pump(&s, DG_EVENT_DIALOG_RESULT, &e) == 1 && e.x == 2,
        "Escape reports the cancel button");
  check(dg_debug_node_count(s.window) == baseline, "every popup is gone again");

  /* The slider (x 510..630): a press near its end reports SLIDER_CHANGED;
   * dg_slider_get_value() reads the value the app copies to the progress
   * bar. */
  press(&s, 625, 10, DG_POINTER_BUTTON_PRIMARY);
  check(pump(&s, DG_EVENT_SLIDER_CHANGED, &e) == 1 && e.node == s.slider,
        "a press on the slider reports SLIDER_CHANGED");
  {
    float value = 0.0F;
    check(dg_slider_get_value(s.slider, &value) == DG_ERR_OK && value > 90.0F,
          "dg_slider_get_value reads the new value");
  }
  key(&s, "Home");
  check(pump(&s, DG_EVENT_SLIDER_CHANGED, NULL) == 1, "Home on the focused slider moves it");

  /* The radios (x 640 and 682): a click selects one and clears the other;
   * Left moves the selection back, reported as a CLICK with clicks = 0. */
  check(dg_node_get_checked(s.radios[0]) == 1, "dg_radio_select chose the first radio");
  press(&s, 708, 16, DG_POINTER_BUTTON_PRIMARY);
  check(pump(&s, DG_EVENT_CLICK, &e) == 1 && e.node == s.radios[1] &&
            dg_node_get_checked(s.radios[1]) == 1 && dg_node_get_checked(s.radios[0]) == 0,
        "a click selects the second radio and clears the first");
  key(&s, "Left");
  check(pump(&s, DG_EVENT_CLICK, &e) == 1 && e.node == s.radios[0] && e.clicks == 0 &&
            dg_node_get_checked(s.radios[0]) == 1,
        "Left moves the selection back to the first radio");

  /* Tabs: Right from the first tab skips the disabled one. */
  (void)dg_node_focus(s.tabs);
  key(&s, "Right");
  check(pump(&s, DG_EVENT_TAB_SELECT, &e) == 1 && e.x == 2, "Right skips the disabled tab");
  (void)dg_tabs_set_selected(s.tabs, 0);

  /* Tree: keys select and collapse. */
  (void)dg_node_focus(s.tree);
  key(&s, "Down");
  key(&s, "Left");
  check(pump(&s, DG_EVENT_TREE_COLLAPSE, &e) == 1 && e.x == 0, "Left collapses 'src'");

  /* Table: a header press asks the app to sort; it does and pushes again. */
  press(&s, 400, 80, DG_POINTER_BUTTON_PRIMARY);
  check(pump(&s, DG_EVENT_TABLE_SORT, &e) == 1 && e.x == 0, "the Name header requests a sort");
  check(s.data.order[0] == 0 && s.data.ascending, "the app sorted ascending");
  (void)dg_node_focus(s.table);
  key(&s, "Down");
  check(pump(&s, DG_EVENT_TABLE_SELECT, &e) == 1, "Down selects a table row");

  (void)dg_node_remove(s.tabs);
  (void)dg_node_remove(s.table);
  (void)pump(&s, 0, NULL);
  check(dg_tree_get_selected(s.tree) == DG_ERR_INVALID_HANDLE,
        "removing the tabs removed the tree inside them");

  dg_app_destroy(s.app);
  printf(g_failures == 0 ? "PASS\n" : "FAIL\n");
  return g_failures == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
  Scene s;
  int running = 1;
  if (argc > 1 && strcmp(argv[1], "--verify") == 0) {
    return run_verify(DRAWGUI_C_COMPONENTS_FONT_DIR);
  }
  memset(&s, 0, sizeof s);
  if (!build(&s, DRAWGUI_C_COMPONENTS_FONT_DIR)) {
    return 1;
  }
  while (running) {
    dg_event events[32];
    int32_t n;
    int32_t i;
    (void)dg_wait_events(s.app, -1);
    n = dg_poll_events(s.app, events, 32);
    for (i = 0; i < n && running; ++i) {
      running = react(&s, &events[i], 1);
    }
  }
  dg_app_destroy(s.app);
  return 0;
}
