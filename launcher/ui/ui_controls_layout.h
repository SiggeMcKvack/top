/* Where Controls puts its pieces in the scene of a layout (ui_layout.h),
 * without SDL.  16:9: the title and the capture's prompt, the game buttons'
 * panel on the left, Swimming at its foot, the item hotkeys' on the right,
 * the fixed shortcuts under it, on two lines.  4:3: the title and the tabs,
 * Buttons, Hotkeys and In game (the prompt in their place during a
 * capture), over the shown tab alone: the game buttons' grid and Reset, the
 * Item hotkeys line and its grid, or the fixed shortcuts as a list and
 * Swimming; no panels, no slots' note.  The fixed shortcuts begin with Menu:
 * Esc, then the cell of the controller's button that opens it too. */
#ifndef ORACLES_UI_CONTROLS_LAYOUT_H
#define ORACLES_UI_CONTROLS_LAYOUT_H

#include "ui_controls_nav.h"
#include "ui_home_layout.h"
#include "ui_page_layout.h"

typedef struct OraclesUiChipLayout {
    OraclesUiBox box;
    OraclesUiLine name;
} OraclesUiChipLayout;

typedef struct OraclesUiControlsLayout {
    OraclesUiLine title, prompt;
    OraclesUiBox left_panel, right_panel;
    OraclesUiLine heading_buttons, heading_keyboard, heading_gamepad;
    OraclesUiLine button_names[ORACLES_HOME_BUTTONS];
    OraclesUiBox cells[ORACLES_CONTROLS_COLUMNS][ORACLES_HOME_BUTTONS];   /* by column and row; zero where none */
    OraclesUiLine cell_texts[ORACLES_CONTROLS_COLUMNS][ORACLES_HOME_BUTTONS];
    OraclesUiBox reset;
    OraclesUiLine reset_text, reset_dot;
    OraclesUiBox mode;                          /* the Item hotkeys line's box */
    OraclesUiLine mode_title, mode_text, mode_note, mode_later;
    OraclesUiChipLayout mode_choices[2];
    OraclesUiLine heading_hotkeys, heading_hotkeys_keyboard, heading_hotkeys_gamepad, heading_items;
    OraclesUiLine hotkey_names[ORACLES_HOME_HOTKEY_ROWS];
    OraclesUiLine items[ORACLES_HOME_SLOTS];
    OraclesUiLine slots_note;
    OraclesUiLine shortcuts_label;
    const char *shortcuts_text;                 /* the label's: "In game", 4:3's "Fixed keys in game" */
    /* The fixed keys, framed, and their meanings, F2 to F11 then Esc, whose Menu comes first, after Esc's frame and
     * the cell of Menu's controller button (its box, the pointer's target, and its text). */
    OraclesUiBox shortcut_keys[6];
    OraclesUiLine shortcut_key_texts[6], shortcut_labels[6];
    OraclesUiBox menu_pad;
    OraclesUiLine menu_pad_text;
    /* 4:3: each tab's box (the pointer's target), its label, and the line along its bottom, zero during a capture;
     * zero in 16:9, and the pieces of the tabs not shown.  Under the Item hotkeys' text, a fan game's line that its
     * hotkeys stay off. */
    OraclesUiBox tabs[ORACLES_CONTROLS_TABS], tab_lines[ORACLES_CONTROLS_TABS];
    OraclesUiLine tab_labels[ORACLES_CONTROLS_TABS];
    OraclesUiLine mode_fan;
    char mode_fan_text[ORACLES_HOME_TEXT_LENGTH];
    /* Swimming: its box (its highlight, the pointer's target), its title and Tap and Hold on one line, its explanation
     * broken into lines under them, in 4:3 a fan game's line that it does not apply there, and in a game the line that
     * it waits for the next Play (zero when not shown).  Zero in 4:3's Buttons and Hotkeys. */
    OraclesUiBox swim;
    OraclesUiLine swim_title, swim_fan, swim_later;
    OraclesUiChipLayout swim_choices[2];
    OraclesUiWrapped swim_text;
} OraclesUiControlsLayout;

extern const OraclesUiTextStyle oracles_ui_controls_title, oracles_ui_controls_prompt, oracles_ui_controls_heading;
extern const OraclesUiTextStyle oracles_ui_controls_name, oracles_ui_controls_cell, oracles_ui_controls_cell_locked, oracles_ui_controls_cell_waiting;
extern const OraclesUiTextStyle oracles_ui_controls_item, oracles_ui_controls_item_empty, oracles_ui_controls_slots_note;
extern const OraclesUiTextStyle oracles_ui_controls_shortcut_key, oracles_ui_controls_shortcut_label, oracles_ui_controls_reset;
extern const OraclesUiTextStyle oracles_ui_controls_mode_title, oracles_ui_controls_mode_text_style, oracles_ui_controls_note, oracles_ui_controls_chip;
/* 4:3's, larger for a 640x480 screen: the cells' 32 px, the d-pad's 27. */
extern const OraclesUiTextStyle oracles_ui_controls_title_4_3, oracles_ui_controls_heading_4_3, oracles_ui_controls_cell_4_3;
extern const OraclesUiTextStyle oracles_ui_controls_cell_locked_4_3, oracles_ui_controls_cell_waiting_4_3, oracles_ui_controls_shortcut_key_4_3;
/* Menu's controller cell, smaller than the grids' (20 px in 16:9, 30 in 4:3), italic while it waits. */
extern const OraclesUiTextStyle oracles_ui_controls_menu_cell, oracles_ui_controls_menu_cell_waiting;
extern const OraclesUiTextStyle oracles_ui_controls_menu_cell_4_3, oracles_ui_controls_menu_cell_waiting_4_3;
extern const char oracles_ui_controls_mode_text[], oracles_ui_controls_mode_note[], oracles_ui_controls_slots[];
extern const char *const oracles_ui_controls_mode_choices[2];
extern const char oracles_ui_controls_swim_text[], oracles_ui_controls_swim_fan[];
extern const char *const oracles_ui_controls_swim_choices[2];   /* "Tap", "Hold" */

/* The styles a layout draws Controls in; 4:3 has no slots' note (NULL), 16:9 no tabs (NULL). */
typedef struct OraclesUiControlsStyles {
    const OraclesUiTextStyle *title, *prompt, *heading, *name, *cell, *cell_locked, *cell_waiting, *item, *item_empty, *slots_note;
    const OraclesUiTextStyle *shortcut_key, *shortcut_label, *reset, *mode_title, *mode_text, *note, *chip, *tab;
    const OraclesUiTextStyle *menu_cell, *menu_cell_waiting;
} OraclesUiControlsStyles;
const OraclesUiControlsStyles *oracles_ui_controls_styles(OraclesUiLayout layout);

/* The screen for `nav` in `layout` (its cells' texts, the prompt, the game shown's items; in a game, the lines that
 * say the Item hotkeys and Swimming wait for the next Play; in 4:3, nav's tab). */
void oracles_ui_layout_controls(OraclesUiLayout layout, const OraclesHomeNav *nav, OraclesUiControlsLayout *out);
/* The text of an item's cell: the item's name, or "Empty". */
const char *oracles_ui_controls_item_text(const OraclesHomeNav *nav, int slot);

#endif
