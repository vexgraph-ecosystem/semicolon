#include "terminal/terminal_panel.h"
#include "terminal/terminal_text.h"
#include "terminal/terminal_anchor.h"
#include "terminal/terminal_canvas.h"
#include "terminal/terminal_color.h"
#include "terminal/terminal_style.h"
#include "terminal/terminal_surface.h"
#include "terminal/terminal_input.h"
#include "exception/throw.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/test.h"
#include "annotation/debug.h"
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <ncurses.h>

;;DEFINITION
/** The semicolon IDE shell: the design from src/tui/tui_example.txt, drawing a
 * real file on a real terminal, with the rail draggable and the editor scrolling.
 *
 * This file is the HOST, not the toolkit. Determinant owns cells, placement,
 * panels, text and the mouse byte grammar and knows nothing about a tty;
 * acquiring the terminal, owning the alternate screen, deciding which mouse
 * events exist, reading a file and driving the whole thing are this program's
 * job. That is why ncurses is named only here.
 *
 * Two things are worth reading this file for.
 *
 * First, SCROLLING IS FREE BECAUSE TEXT IS ADDRESSABLE. The editor's rows are
 * created once and then re-pointed at different lines each frame with
 * TerminalText_setText. No row is allocated, freed or rebuilt while scrolling,
 * which is the entire argument for a text being an object rather than a string
 * handed to a draw call.
 *
 * Second, THE DIVIDER IS DRAGGED BY MOVING AN ANCHOR. The rail's width is not
 * rebuilt into the tree when the pointer moves; the same layer gets a new
 * extent through TerminalPanel_setAnchor, and the next paint lays out around it.
 * Resizing a pane and scrolling a file are therefore both ordinary state changes,
 * not scene surgery.
 *
 * Nothing is transparent-by-accident: no panel sets a background, so every cell
 * the app does not paint keeps the terminal's own background. That is deliberate
 * — the theme belongs to whoever owns the terminal.
 *
 * The design itself is still a mock-up: the tree is a literal list, the tab strip
 * is static, and nothing is edited. The one real thing is the file in the editor. */
;;OVERVIEW
/** ============================================================================
 * MODULE: semicolon terminal entry (src/main/tui-main.c)
 * ============================================================================
 * OWNED STATE (file-local):
 *   gQuit         signal-safe shutdown request only.
 *   gCells/gStyles/gFg/gBg          static canvas planes, TUI_MAX_W x TUI_MAX_H.
 *   gFrontCells/...                 static last-paint mirror for the surface diff.
 *   gRule/gSpaces/emptyLine         static textures: the rule run, blank runs.
 *   gGutter[TUI_MAX_ROWS][16]       per-row line-number text buffers.
 *   gLineChip[16]                   the status bar's line-position text buffer.
 *   gPairs[]                        registered ncurses colour pairs.
 * STRUCTS:
 *   FileText  owned file bytes, line table, and the display name.
 *   TuiRow    one editor row: its gutter text and its code text.
 *   Scene     the tree plus the handles the host mutates at runtime.
 *   Interaction  the mouse decoder, the grab flag and the last pointer cell.
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Private helpers:
 *   static void onSignal(int)
 *   static void initTextures(void)
 *   static bool fileLoad(FileText *, const char *)
 *   static void fileFree(FileText *)
 *   static TerminalText *chip(const char *)
 *   static TerminalText *plain(const char *, uint32_t)
 *   static bool putText(parent, text, anchor) / putPanel(parent, panel, anchor)
 *   static TerminalPanel *buildChromeRow(void) / buildStatusBar(Scene *, FileText *)
 *   static TerminalPanel *buildRail(void)
 *   static TerminalPanel *buildEditor(Scene *, size_t)
 *   static TerminalPanel *buildCentre(Scene *, size_t)
 *   static void sceneBuild(Scene *, FileText *, size_t)
 *   static void sceneSyncRows(Scene *, const FileText *, size_t first, size_t left, size_t visible)
 *   static size_t cellsOf/bytesForCells(const char *, size_t, ...)  cell offsets
 *   static size_t maxScrollTop/maxScrollLeft(clampScroll/applyScroll)
 *   static size_t editorFirstRow/editorVisibleRows/codeLeftColumn/codeColumns
 *   static void sceneSetRailWidth(Scene *, size_t width)
 *   static short colorIndex(uint32_t) / pairFor(fg, bg) / attrsFor(style)
 *   static bool blitRun(void *, TerminalRun)
 *   static void drawChrome(canvas, width, height, size_t railWidth)
 *   static void handlePointer(Scene *, Interaction *, int width, int height)
 *   static int dumpFrame(int, int, FileText *)
 *   static int runInputProbe(void)   DEBUG: print what the terminal sends
 *   static int runTerminal(FileText *)
 * Entry:
 *   int main(int, char **)   --help / --dump [WxH] / --file PATH / interactive.
 * Contracts:
 *   The interactive path REQUIRES a tty on stdin and stdout and refuses before
 *   ncurses otherwise. Ctrl-Q or q quits; the terminal is restored on every
 *   normal exit and on SIGINT/SIGTERM (SIGKILL or a crash cannot promise that).
 *   Mouse reporting is enabled for ANY-MOTION plus SGR encoding, so hover and
 *   drag both arrive; it is disabled again on exit, which hands the mouse back to
 *   the terminal's own selection. Wheel scrolls three lines, a pointer resting on
 *   the top or bottom editor row scrolls one line per motion event, and a press
 *   within one cell of the divider begins a drag that resizes the rail. No file
 *   is written, no network is touched and no program is executed.
 * ============================================================================ */

#define TUI_MAX_W 400
#define TUI_MAX_H 200
#define TUI_MAX_ROWS 160
#define TUI_MAX_FILE_BYTES (4u * 1024u * 1024u)
#define GUTTER_WIDTH 5u
#define TUI_POLL_MS 100
#define TUI_RAIL_MIN 12u
#define TUI_RAIL_MARGIN 24u
#define TUI_SCROLL_ROWS 3u
#define TUI_SCROLL_COLS 4u
// Scroll-past padding: how far the view may travel BEYOND the last line and the
// last column, so there is always somewhere to type at the bottom and at the right
// edge instead of the caret being pinned against it.
#define TUI_SCROLL_PAST_BOTTOM 10u
#define TUI_SCROLL_PAST_RIGHT 20u
// Events handled before a paint. A drag floods motion events, so an unbounded
// drain never lets the frame update; a bound keeps the screen live.
#define TUI_EVENTS_PER_FRAME 32u

#define COL_TEXT   TERMINAL_RGB(0xE6, 0xE6, 0xE6)
#define COL_MUTED  TERMINAL_RGB(0x77, 0x77, 0x88)
#define COL_ACCENT TERMINAL_RGB(0x7A, 0xC8, 0xFF)
#define COL_ERROR  TERMINAL_RGB(0xFF, 0x6B, 0x6B)
#define COL_GUTTER TERMINAL_RGB(0x55, 0x55, 0x66)

static volatile sig_atomic_t gQuit;
static uint32_t gCells[TUI_MAX_W * TUI_MAX_H];
static uint8_t gStyles[TUI_MAX_W * TUI_MAX_H];
static uint32_t gFg[TUI_MAX_W * TUI_MAX_H];
static uint32_t gBg[TUI_MAX_W * TUI_MAX_H];
static uint32_t gFrontCells[TUI_MAX_W * TUI_MAX_H];
static uint8_t gFrontStyles[TUI_MAX_W * TUI_MAX_H];
static uint32_t gFrontFg[TUI_MAX_W * TUI_MAX_H];
static uint32_t gFrontBg[TUI_MAX_W * TUI_MAX_H];
static char gRule[512];
static char gGutter[TUI_MAX_ROWS][16];
static char gLineChip[16];
static char gBreadcrumb[192];
static const char *gEmptyLine = "";

// ---------------------------------------------------------------- file text

typedef struct FileText {
    char *bytes;             // owned file bytes, NUL-terminated
    size_t length;
    const char **lines;      // owned pointers into `bytes`
    size_t *lineLengths;     // owned per-line lengths
    size_t lineCount;
    size_t lineCapacity;
    size_t longestCells;     // widest line in CELLS, so horizontal scroll has a bound
    char name[64];           // display name for the breadcrumb
} FileText;

typedef struct TuiRow {
    TerminalText *gutter;
    TerminalText *code;
} TuiRow;

typedef struct Scene {
    TerminalPanel *root;
    TerminalPanel *rail;
    TerminalChildId railId;
    TuiRow rows[TUI_MAX_ROWS];
    size_t rowCount;
    TerminalText *lineChip;
    size_t railWidth;
} Scene;

typedef struct Interaction {
    TerminalInput input;
    bool grabbing;          // the divider is held
    size_t grabX;           // pointer cell where the grab began
    size_t grabWidth;       // rail width when the grab began
    bool pressed;           // any button is currently down
    bool havePointer;
    size_t pointerX;
    size_t pointerY;
} Interaction;

static void onSignal(int signalNumber) {
    (void) signalNumber;
    gQuit = 1;
}

static void initTextures(void) {
    memset(gRule, '-', sizeof gRule - 1);
    gRule[sizeof gRule - 1] = '\0';
    memset(gLineChip, 0, sizeof gLineChip);
}

// Cells a byte span occupies, using the canvas's own locale-free width rule so a
// wide glyph counts two and the horizontal offset lands on a cell boundary.
static size_t cellsOf(const char *bytes, size_t length) {
    size_t cells = 0;
    size_t at = 0;
    while (at < length) {
        TerminalCanvasDecodedScalar decoded = TerminalCanvas_utf8Decode(bytes + at, length - at);
        if (decoded.bytes == 0) {
            ++cells;
            ++at;
            continue;
        }
        cells += TerminalCanvas_widthOf(decoded.codepoint);
        at += decoded.bytes;
    }
    return cells;
}

// Byte offset of a CELL offset inside a line. Cutting on a byte offset would split
// a UTF-8 sequence in half and render two pieces of one glyph.
static size_t bytesForCells(const char *bytes, size_t length, size_t cells) {
    size_t consumed = 0;
    size_t at = 0;
    while (at < length && consumed < cells) {
        TerminalCanvasDecodedScalar decoded = TerminalCanvas_utf8Decode(bytes + at, length - at);
        if (decoded.bytes == 0) {
            ++consumed;
            ++at;
            continue;
        }
        consumed += TerminalCanvas_widthOf(decoded.codepoint);
        at += decoded.bytes;
    }
    return at;
}

static void fileFree(FileText *file) {
    free(file->lines);
    free(file->lineLengths);
    free(file->bytes);
    *file = (FileText) {0};
}

// Read the whole file, replace every control byte with a space (a cell cannot
// draw one), then split on newlines. Tabs become spaces rather than '?' because
// the canvas refuses control codepoints and a file full of '?' is worse than one
// whose indentation is approximate.
static bool fileLoad(FileText *file, const char *path) {
    *file = (FileText) {0};
    const char *slash = strrchr(path, '/');
    const char *base = slash != nullptr ? slash + 1 : path;
    (void) snprintf(file->name, sizeof file->name, "%s", base);
    FILE *handle = fopen(path, "rb");
    if (handle == nullptr)
        return false;
    char *bytes = malloc(TUI_MAX_FILE_BYTES + 1u);
    if (bytes == nullptr) {
        (void) fclose(handle);
        return false;
    }
    size_t length = fread(bytes, 1, TUI_MAX_FILE_BYTES, handle);
    (void) fclose(handle);
    bytes[length] = '\0';
    size_t lineCapacity = 64;
    const char **lines = malloc(lineCapacity * sizeof *lines);
    size_t *lineLengths = malloc(lineCapacity * sizeof *lineLengths);
    if (lines == nullptr || lineLengths == nullptr) {
        free(bytes);
        free(lines);
        free(lineLengths);
        return false;
    }
    size_t count = 0;
    size_t start = 0;
    bool grew = true;
    for (size_t at = 0; at <= length; ++at) {
        unsigned char current = at < length ? (unsigned char) bytes[at] : '\n';
        if (current != '\n') {
            if (current < 32u || current == 127u)
                bytes[at] = ' ';
            continue;
        }
        if (count == lineCapacity) {
            size_t next = lineCapacity * 2u;
            const char **grownLines = realloc(lines, next * sizeof *lines);
            size_t *grownLengths = realloc(lineLengths, next * sizeof *lineLengths);
            if (grownLines == nullptr || grownLengths == nullptr) {
                grew = false;
                break;
            }
            lines = grownLines;
            lineLengths = grownLengths;
            lineCapacity = next;
        }
        size_t end = at;
        if (end > start && bytes[end - 1] == '\r')
            --end;                                   // CRLF: the carriage is not a cell
        bytes[at] = '\0';
        lines[count] = bytes + start;
        lineLengths[count] = end - start;
        ++count;
        start = at + 1;
    }
    if (!grew) {
        free(bytes);
        free(lines);
        free(lineLengths);
        return false;
    }
    file->bytes = bytes;
    file->length = length;
    file->lines = lines;
    file->lineLengths = lineLengths;
    file->lineCount = count;
    file->lineCapacity = lineCapacity;
    size_t longest = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t cells = cellsOf(lines[i], lineLengths[i]);
        if (cells > longest)
            longest = cells;
    }
    file->longestCells = longest;
    return true;
}

// ---------------------------------------------------------------- scene

static TerminalText *chip(const char *label) {
    TerminalText *text = TerminalText(label);
    if (text == nullptr)
        return nullptr;
    (void) TerminalText_setBrackets(text, '[', ']');
    return text;
}

static TerminalText *plain(const char *label, uint32_t color) {
    TerminalText *text = TerminalText(label);
    if (text == nullptr)
        return nullptr;
    (void) TerminalText_setForegroundColor(text, color);
    return text;
}

static bool putText(TerminalPanel *parent, TerminalText *text, TerminalAnchor anchor) {
    if (text == nullptr)
        return false;
    if (!TerminalPanel_isChildId(TerminalPanel_addText(parent, text, anchor))) {
        TerminalText_free(text);
        return false;
    }
    return true;
}

static bool putPanel(TerminalPanel *parent, TerminalPanel *child, TerminalAnchor anchor) {
    if (child == nullptr)
        return false;
    if (!TerminalPanel_isChildId(TerminalPanel_addPanel(parent, child, anchor))) {
        TerminalPanel_free(child);
        return false;
    }
    return true;
}

static TerminalPanel *buildChromeRow(void) {
    TerminalPanel *bar = TerminalPanel();
    if (bar == nullptr)
        return nullptr;
    static const char *labels[] = { ";", ">w< agents", "<-|->" };
    static const size_t extents[] = { 3, 12, 7 };
    // The rule texture paints first and reserves nothing, so every chip lands on
    // top of it and the gaps read as the run between chips.
    if (!putText(bar, plain(gRule, COL_MUTED), TerminalAnchor_0())) {
        TerminalPanel_free(bar);
        return nullptr;
    }
    if (!putText(bar, plain(gRule, COL_MUTED), TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, 2))) {
        TerminalPanel_free(bar);
        return nullptr;
    }
    for (size_t i = 0; i < sizeof labels / sizeof labels[0]; ++i) {
        TerminalText *label = chip(labels[i]);
        (void) TerminalText_setForegroundColor(label, i == 0 ? COL_ACCENT : COL_TEXT);
        if (!putText(bar, label, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, extents[i]))) {
            TerminalPanel_free(bar);
            return nullptr;
        }
        if (i < 2)
            (void) putText(bar, plain(gRule, COL_MUTED),
                           TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, 1));
    }
    // Dashes out to the rail's right border, so the chrome row's left group ends
    // exactly where the rail ends and the tab strip starts in the centre's columns.
    // The count is what puts the tabs at the centre's first column.
    if (!putText(bar, plain(gRule, COL_MUTED), TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, 5))) {
        TerminalPanel_free(bar);
        return nullptr;
    }
    // The compact trick: the tab strip shares the chrome row, so it costs no row.
    static const char *tabLabels[] = { "helloWorld.c  x", "placeholder.txt  x", "terminal  x", "type.h  x" };
    for (size_t i = 0; i < sizeof tabLabels / sizeof tabLabels[0]; ++i) {
        TerminalText *tab = chip(tabLabels[i]);
        (void) TerminalText_setForegroundColor(tab, i == 3 ? COL_TEXT : COL_MUTED);
        if (!putText(bar, tab, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT,
                                                TerminalText_measure(tab)))) {
            TerminalPanel_free(bar);
            return nullptr;
        }
    }
    (void) TerminalPanel_setForegroundColor(bar, COL_TEXT);
    return bar;
}

static TerminalPanel *buildStatusBar(Scene *scene, const FileText *file) {
    TerminalPanel *bar = TerminalPanel();
    if (bar == nullptr)
        return nullptr;
    // The rule underlay runs the full width; the chips and the breadcrumb paint over
    // it, so the gaps read as the bottom border's run.
    if (!putText(bar, plain(gRule, COL_MUTED), TerminalAnchor_0())) {
        TerminalPanel_free(bar);
        return nullptr;
    }
    if (!putText(bar, plain(gRule, COL_MUTED), TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, 2))) {
        TerminalPanel_free(bar);
        return nullptr;
    }
    // The chip BORROWS this string for the life of the tree, so it must outlive
    // this function: a stack buffer here renders as garbage.
    (void) snprintf(gBreadcrumb, sizeof gBreadcrumb, "vexgraph>src>%s", file->name);
    TerminalText *crumb = chip(gBreadcrumb);
    (void) TerminalText_setForegroundColor(crumb, COL_TEXT);
    if (!putText(bar, crumb, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT,
                                             TerminalText_measure(crumb)))) {
        TerminalPanel_free(bar);
        return nullptr;
    }
    static const size_t rightExtents[] = { 2, 7, 1, 6, 1, 8, 1, 10 };
    static const char *rightLabels[] = { nullptr, "utf-8", nullptr, "time", nullptr, nullptr, nullptr, "c23/java" };
    for (size_t i = 0; i < sizeof rightExtents / sizeof rightExtents[0]; ++i) {
        TerminalText *child = nullptr;
        if (rightLabels[i] != nullptr) {
            child = chip(rightLabels[i]);
        } else if (i == 5) {
            // The line-position chip: its text is re-pointed every frame, so its
            // extent is fixed and the number is always formatted to fit.
            (void) snprintf(gLineChip, sizeof gLineChip, "[ln:%3zu]", (size_t) 1);
            child = TerminalText_1(gLineChip);
            (void) TerminalText_setForegroundColor(child, COL_MUTED);
            scene->lineChip = child;
        } else {
            child = plain(gRule, COL_MUTED);
        }
        if (!putText(bar, child, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_RIGHT, rightExtents[i]))) {
            TerminalPanel_free(bar);
            return nullptr;
        }
    }
    (void) TerminalPanel_setForegroundColor(bar, COL_TEXT);
    return bar;
}

static TerminalPanel *buildRail(void) {
    static const char *rows[] = {
        "  v [] vexgraph", "    v [] src", "        {} opencode.json", "        Aa hello.txt",
        "         J example.java", "         C whatever.c", "        C+ whatever.cpp",
        "        oC shim.m", "    > [] new folder", "    > [] another folder"
    };
    TerminalPanel *rail = TerminalPanel();
    if (rail == nullptr)
        return nullptr;
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
        bool isFolder = strstr(rows[i], "[]") != nullptr;
        TerminalText *row = plain(rows[i], isFolder ? COL_ACCENT : COL_TEXT);
        if (!putText(rail, row, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_TOP, 1))) {
            TerminalPanel_free(rail);
            return nullptr;
        }
    }
    (void) TerminalPanel_setForegroundColor(rail, COL_TEXT);
    return rail;
}

// The editor: a gutter column, then the code column whose first row also carries
// the right-aligned action chips. Every row is a text that gets re-pointed rather
// than rebuilt, which is what makes scrolling allocation-free.
static TerminalPanel *buildEditor(Scene *scene, size_t rowCount) {
    TerminalPanel *editor = TerminalPanel();
    TerminalPanel *gutter = TerminalPanel();
    TerminalPanel *code = TerminalPanel();
    TerminalPanel *rows = TerminalPanel();
    TerminalPanel *actions = TerminalPanel();
    if (editor == nullptr || gutter == nullptr || code == nullptr || rows == nullptr || actions == nullptr) {
        TerminalPanel_free(editor);
        TerminalPanel_free(gutter);
        TerminalPanel_free(code);
        TerminalPanel_free(rows);
        TerminalPanel_free(actions);
        return nullptr;
    }
    for (size_t i = 0; i < rowCount; ++i) {
        TerminalText *number = plain(gEmptyLine, COL_GUTTER);
        TerminalText *line = plain(gEmptyLine, COL_TEXT);
        if (!putText(gutter, number, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_TOP, 1))
            || !putText(rows, line, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_TOP, 1))) {
            TerminalPanel_free(editor);
            TerminalPanel_free(gutter);
            TerminalPanel_free(code);
            TerminalPanel_free(rows);
            TerminalPanel_free(actions);
            return nullptr;
        }
        scene->rows[i].gutter = number;
        scene->rows[i].code = line;
    }
    scene->rowCount = rowCount;
    // The action chips are a LAYERED sibling of the rows, so they paint on top of
    // the first editor row without taking a row of their own.
    if (!putText(actions, plain(" ", COL_MUTED), TerminalAnchor_2(TERMINAL_ANCHOR_BAND_RIGHT, 1))
        || !putText(actions, chip("20!!, 20!"), TerminalAnchor_2(TERMINAL_ANCHOR_BAND_RIGHT, 11))
        || !putText(actions, chip("> run"), TerminalAnchor_2(TERMINAL_ANCHOR_BAND_RIGHT, 8))) {
        TerminalPanel_free(editor);
        TerminalPanel_free(gutter);
        TerminalPanel_free(code);
        TerminalPanel_free(rows);
        TerminalPanel_free(actions);
        return nullptr;
    }
    (void) TerminalPanel_setForegroundColor(actions, COL_MUTED);
    if (!putPanel(code, rows, TerminalAnchor_0()) || !putPanel(code, actions, TerminalAnchor_0())) {
        TerminalPanel_free(editor);
        TerminalPanel_free(gutter);
        TerminalPanel_free(code);
        return nullptr;
    }
    (void) TerminalPanel_setLayout(code, TERMINAL_PANEL_LAYERED);
    // The run chip sits left of the diagnostics chip in the same row.
    if (!putPanel(editor, gutter, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, GUTTER_WIDTH))
        || !putPanel(editor, code, TerminalAnchor_0())) {
        TerminalPanel_free(editor);
        return nullptr;
    }
    return editor;
}

// The centre is only the editor: the tab strip and its separator moved into the
// chrome row, which is the compaction.
static TerminalPanel *buildCentre(Scene *scene, size_t rowCount) {
    TerminalPanel *centre = TerminalPanel();
    if (centre == nullptr)
        return nullptr;
    if (!putPanel(centre, buildEditor(scene, rowCount), TerminalAnchor_0())) {
        TerminalPanel_free(centre);
        return nullptr;
    }
    return centre;
}

static void sceneSyncRows(Scene *scene, const FileText *file, size_t first, size_t left,
                          size_t visible);

// Build the whole tree. Every child is built first and handed over afterwards, so
// a refusal is detected before anything is partially wired. A failure here ends
// the process with a diagnostic, which is why the not-yet-handed-over locals are
// simply reclaimed by exit rather than untangled.
static void sceneBuild(Scene *scene, const FileText *file, size_t rowCount) {
    *scene = (Scene) {0};
    scene->railWidth = 31;
    TerminalPanel *title = buildChromeRow();
    TerminalPanel *status = buildStatusBar(scene, file);
    TerminalPanel *rail = buildRail();
    TerminalPanel *centre = buildCentre(scene, rowCount);
    TerminalText *border = plain(gRule, COL_MUTED);
    scene->root = TerminalPanel();
    if (title == nullptr || status == nullptr || rail == nullptr || centre == nullptr
        || border == nullptr || scene->root == nullptr) {
        TerminalPanel_free(scene->root);
        TerminalPanel_free(title);
        TerminalPanel_free(status);
        TerminalPanel_free(rail);
        TerminalPanel_free(centre);
        TerminalText_free(border);
        *scene = (Scene) {0};
        return;
    }
    if (!putPanel(scene->root, title, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_TOP, 1))
        || !putPanel(scene->root, status, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_BOTTOM, 1))) {
        TerminalPanel_free(scene->root);
        TerminalPanel_free(rail);
        TerminalPanel_free(centre);
        TerminalText_free(border);
        *scene = (Scene) {0};
        return;
    }
    scene->rail = rail;
    scene->railId = TerminalPanel_addPanel(scene->root, rail,
                                          TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, scene->railWidth));
    if (!TerminalPanel_isChildId(scene->railId)) {
        TerminalPanel_free(scene->root);
        TerminalPanel_free(rail);
        TerminalPanel_free(centre);
        TerminalText_free(border);
        *scene = (Scene) {0};
        return;
    }
    if (!putText(scene->root, border, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_RIGHT, 1))
        || !putPanel(scene->root, centre, TerminalAnchor_0())) {
        TerminalPanel_free(scene->root);
        *scene = (Scene) {0};
        return;
    }
    sceneSyncRows(scene, file, 0, 0, rowCount);
}

// Point every visible row at the line it should show. No allocation, no tree
// change: this is the whole of scrolling.
static void sceneSyncRows(Scene *scene, const FileText *file, size_t first, size_t left,
                          size_t visible) {
    for (size_t i = 0; i < scene->rowCount; ++i) {
        TuiRow *row = &scene->rows[i];
        if (i >= visible || first + i >= file->lineCount) {
            // Past the end: the gutter goes blank with the code, so the scroll-past
            // space reads as empty rather than as a continuation of the last line.
            (void) TerminalText_setText(row->gutter, gEmptyLine, 0);
            (void) TerminalText_setText(row->code, gEmptyLine, 0);
            continue;
        }
        size_t line = first + i;
        int written = snprintf(gGutter[i], sizeof gGutter[i], "%4zu|", line + 1);
        if (written < 0)
            written = 0;
        (void) TerminalText_setText(row->gutter, gGutter[i], (size_t) written);
        const char *bytes = file->lines[line];
        size_t length = file->lineLengths[line];
        size_t skip = bytesForCells(bytes, length, left);
        (void) TerminalText_setText(row->code, bytes + skip, length - skip);
    }
    if (scene->lineChip != nullptr) {
        (void) snprintf(gLineChip, sizeof gLineChip, "[ln:%3zu]", first + 1);
        (void) TerminalText_setText(scene->lineChip, gLineChip, strlen(gLineChip));
    }
}

static void sceneSetRailWidth(Scene *scene, size_t width) {
    if (width < TUI_RAIL_MIN)
        width = TUI_RAIL_MIN;
    if (width == scene->railWidth)
        return;
    scene->railWidth = width;
    (void) TerminalPanel_setAnchor(scene->root, scene->railId,
                                   TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, width));
}

static void sceneFree(Scene *scene) {
    TerminalPanel_free(scene->root);   // frees every child, text and panel alike
    scene->root = nullptr;
}

// ---------------------------------------------------------------- ncurses side

#define TUI_PAIR_MAX 96
typedef struct TermPair {
    uint32_t fg;
    uint32_t bg;
    short pair;
} TermPair;
static TermPair gPairs[TUI_PAIR_MAX];
static size_t gPairCount;

static short colorIndex(uint32_t color) {
    if (!TERMINAL_COLOR_IS_SET(color))
        return -1;
    int r = TERMINAL_COLOR_RED(color);
    int g = TERMINAL_COLOR_GREEN(color);
    int b = TERMINAL_COLOR_BLUE(color);
    if (COLORS >= 256) {
        int cr = (r * 5 + 127) / 255;
        int cg = (g * 5 + 127) / 255;
        int cb = (b * 5 + 127) / 255;
        return (short) (16 + 36 * cr + 6 * cg + cb);
    }
    int luminance = (r * 30 + g * 59 + b * 11) / 100;
    if (luminance < 64)
        return COLOR_BLACK;
    if (luminance < 128)
        return COLOR_BLUE;
    if (luminance < 200)
        return COLOR_CYAN;
    return COLOR_WHITE;
}

static short pairFor(uint32_t fg, uint32_t bg) {
    for (size_t i = 0; i < gPairCount; ++i)
        if (gPairs[i].fg == fg && gPairs[i].bg == bg)
            return gPairs[i].pair;
    if (gPairCount + 1 >= (size_t) COLOR_PAIRS || gPairCount + 1 >= TUI_PAIR_MAX)
        return 0;
    short pair = (short) (gPairCount + 1);
    init_pair(pair, colorIndex(fg), colorIndex(bg));
    gPairs[gPairCount].fg = fg;
    gPairs[gPairCount].bg = bg;
    gPairs[gPairCount].pair = pair;
    ++gPairCount;
    return pair;
}

static attr_t attrsFor(uint8_t style) {
    attr_t attrs = A_NORMAL;
    if (style & TERMINAL_STYLE_BOLD)
        attrs |= A_BOLD;
    if (style & TERMINAL_STYLE_DIM || style & TERMINAL_STYLE_MUTED)
        attrs |= A_DIM;
    if (style & TERMINAL_STYLE_UNDERLINE)
        attrs |= A_UNDERLINE;
    if (style & TERMINAL_STYLE_REVERSE)
        attrs |= A_REVERSE;
    return attrs;
}

static bool blitRun(void *context, TerminalRun run) {
    (void) context;
    bool haveAttrs = false;
    attr_t currentAttrs = A_NORMAL;
    short currentPair = 0;
    for (size_t i = 0; i < run.length; ++i) {
        uint32_t glyph = run.cells != nullptr ? run.cells[i] : ' ';
        if (glyph == 0)
            continue;                                  // wide glyph continuation
        uint8_t style = run.styles != nullptr ? run.styles[i] : 0;
        uint32_t fg = run.fg != nullptr ? run.fg[i] : TERMINAL_COLOR_DEFAULT;
        uint32_t bg = run.bg != nullptr ? run.bg[i] : TERMINAL_COLOR_DEFAULT;
        attr_t attrs = attrsFor(style);
        short pair = pairFor(fg, bg);
        // A run usually shares one pen, so set the attributes once and only touch
        // them again at a boundary: one wattr_set per cell is pure overhead.
        if (!haveAttrs || attrs != currentAttrs || pair != currentPair) {
            wattr_set(stdscr, attrs, pair, nullptr);
            currentAttrs = attrs;
            currentPair = pair;
            haveAttrs = true;
        }
        char encoded[8];
        size_t bytes = 0;
        if (glyph < 0x80u) {
            encoded[bytes++] = (char) glyph;
        } else if (glyph < 0x800u) {
            encoded[bytes++] = (char) (0xC0u | (glyph >> 6));
            encoded[bytes++] = (char) (0x80u | (glyph & 0x3Fu));
        } else if (glyph < 0x10000u) {
            encoded[bytes++] = (char) (0xE0u | (glyph >> 12));
            encoded[bytes++] = (char) (0x80u | ((glyph >> 6) & 0x3Fu));
            encoded[bytes++] = (char) (0x80u | (glyph & 0x3Fu));
        } else {
            encoded[bytes++] = (char) (0xF0u | (glyph >> 18));
            encoded[bytes++] = (char) (0x80u | ((glyph >> 12) & 0x3Fu));
            encoded[bytes++] = (char) (0x80u | ((glyph >> 6) & 0x3Fu));
            encoded[bytes++] = (char) (0x80u | (glyph & 0x3Fu));
        }
        encoded[bytes] = '\0';
        mvwaddstr(stdscr, (int) run.y, (int) (run.x + i), encoded);
    }
    return true;
}

// The chrome the toolkit has no widget for yet: the rail's two verticals and the
// panel's right edge, drawn over the painted tree. The divider moves with the
// rail, which is why it takes the width rather than a constant.
static void drawChrome(TerminalCanvas *canvas, size_t width, size_t height, size_t railWidth) {
    if (height < 3 || width < 4)
        return;
    size_t rows = height - 2;
    TerminalPen pen = TerminalPen_1(TERMINAL_STYLE_MUTED);
    TerminalCanvas left = TerminalCanvas_sub(canvas, 0, 1, 1, rows);
    TerminalCanvas_vline(&left, 0, '|', pen);
    TerminalCanvas divider = TerminalCanvas_sub(canvas, railWidth - 1, 1, 1, rows);
    TerminalCanvas_vline(&divider, 0, '|', pen);
    TerminalCanvas right = TerminalCanvas_sub(canvas, width - 1, 1, 1, rows);
    TerminalCanvas_vline(&right, 0, '|', pen);
    // The four corners. The horizontal runs are the panels' own content (the chips
    // and the rule texture), so only the corners are left to draw — and the corner
    // at the rail's border is what closes the left panel's top and bottom edges.
    uint32_t dividerColumn = (uint32_t) (railWidth - 1);
    uint32_t lastColumn = (uint32_t) (width - 1);
    uint32_t lastRow = (uint32_t) (height - 1);
    TerminalCanvas_glyph(canvas, 0, 0, 0x250Cu, pen);                       // top-left
    TerminalCanvas_glyph(canvas, dividerColumn, 0, 0x2510u, pen);           // rail's top-right
    TerminalCanvas_glyph(canvas, lastColumn, 0, 0x2510u, pen);              // top-right
    TerminalCanvas_glyph(canvas, 0, lastRow, 0x2514u, pen);                 // bottom-left
    TerminalCanvas_glyph(canvas, dividerColumn, lastRow, 0x2534u, pen);     // the tee the rail makes
    TerminalCanvas_glyph(canvas, lastColumn, lastRow, 0x2518u, pen);        // bottom-right
}

// Row 0 is the chrome (title, undo/redo and the tab strip together) and the last
// row is the status border; everything between is editor.
static size_t editorFirstRow(void) { return 1; }

static size_t editorRowCount(int height) {
    if (height < (int) (editorFirstRow() + 2u))
        return 0;
    return (size_t) height - editorFirstRow() - 1u;
}

// The scene owns at most TUI_MAX_ROWS row texts, so a very tall terminal shows
// the rows it has rather than reaching past them.
// The code area's columns, derived from the same layout the anchors produce: the
// centre starts at the rail's border, the gutter takes GUTTER_WIDTH, and the frame's
// right edge column is never content.
static size_t codeLeftColumn(size_t railWidth) { return railWidth + GUTTER_WIDTH; }

static size_t codeColumns(size_t railWidth, int width) {
    if (width <= 0 || (size_t) width <= codeLeftColumn(railWidth) + 1u)
        return 0;
    return (size_t) width - codeLeftColumn(railWidth) - 1u;
}

static size_t editorVisibleRows(int height) {
    size_t rows = editorRowCount(height);
    return rows > TUI_MAX_ROWS ? TUI_MAX_ROWS : rows;
}

#define TUI_MOUSE_SGR (KEY_MAX + 1)
#define TUI_MOUSE_X10 (KEY_MAX + 2)

// One normalized key-scroll step, as a value (the Value Return Law: no destination
// parameters).
//
// SLOT RECORD: ScrollStep
typedef struct ScrollStep {
    bool keyed;    // the key was a scroll key at all
    int rows;      // vertical cells
    int columns;   // horizontal cells
} ScrollStep;

// Arrow keys are the point: the horizontal wheel is a pair most terminals never
// send, Shift+wheel is routinely eaten by the terminal's own scrollback, and a
// one-column drag edge is unhittable. The arrows exist on every keyboard.
static ScrollStep scrollStepFor(int key) {
    switch (key) {
        case KEY_UP: return (ScrollStep) { true, -1, 0 };
        case KEY_DOWN: return (ScrollStep) { true, 1, 0 };
        case KEY_LEFT: return (ScrollStep) { true, 0, -(int) TUI_SCROLL_COLS };
        case KEY_RIGHT: return (ScrollStep) { true, 0, (int) TUI_SCROLL_COLS };
        case KEY_SLEFT: return (ScrollStep) { true, 0, -(int) TUI_SCROLL_COLS };
        case KEY_SRIGHT: return (ScrollStep) { true, 0, (int) TUI_SCROLL_COLS };
        case KEY_PPAGE: return (ScrollStep) { true, -((int) TUI_SCROLL_ROWS * 4), 0 };
        case KEY_NPAGE: return (ScrollStep) { true, (int) TUI_SCROLL_ROWS * 4, 0 };
        case KEY_HOME: return (ScrollStep) { true, -1000000, 0 };
        case KEY_END: return (ScrollStep) { true, 1000000, 0 };
        default: return (ScrollStep) { false, 0, 0 };
    }
}

// ncurses matches the mouse introducer itself (it is registered with define_key),
// so the bytes it consumed are re-injected before the suffix is read. This keeps
// ncurses as the KEY decoder and Determinant as the MOUSE decoder, which is the
// layering the toolkit's lawbook asks for.
static bool readMouseSuffix(TerminalInput *input, bool sgr) {
    static const char *sgrPrefix = "\x1b[<";
    static const char *x10Prefix = "\x1b[M";
    const char *prefix = sgr ? sgrPrefix : x10Prefix;
    for (size_t i = 0; prefix[i] != '\0'; ++i)
        (void) TerminalInput_feed(input, (unsigned char) prefix[i]);
    for (size_t i = 0; i < 24; ++i) {
        int byte = getch();
        if (byte == ERR)
            return false;
        if (TerminalInput_feed(input, byte))
            return true;
        if (!sgr && i == 2)
            return false;   // X10 is exactly three bytes; anything else is malformed
    }
    return false;
}

static bool mouseReporting(bool enabled) {
    return fputs(enabled ? "\033[?1003h\033[?1006h" : "\033[?1003l\033[?1006l", stdout) >= 0
        && fflush(stdout) == 0;
}

// Vertical bound: the last line may sit anywhere down to the last row, and
// TUI_SCROLL_PAST_BOTTOM lines of empty space follow it, so there is always
// somewhere below the end to work.
static size_t maxScrollTop(const FileText *file, size_t visible) {
    size_t total = file->lineCount + TUI_SCROLL_PAST_BOTTOM;
    return total > visible ? total - visible : 0;
}

// Horizontal bound: the longest line may sit anywhere left of the last column, and
// TUI_SCROLL_PAST_RIGHT columns of empty space follow it.
static size_t maxScrollLeft(const FileText *file, size_t codeWidth) {
    size_t total = file->longestCells + TUI_SCROLL_PAST_RIGHT;
    return total > codeWidth ? total - codeWidth : 0;
}

static size_t clampScroll(size_t value, size_t maximum) {
    return value > maximum ? maximum : value;
}

static size_t applyScroll(size_t value, int delta, size_t maximum) {
    if (delta < 0) {
        size_t amount = (size_t) (-delta);
        return amount > value ? 0 : value - amount;
    }
    return clampScroll(value + (size_t) delta, maximum);
}

;;TEST
static int dumpFrame(int width, int height, const FileText *file) {
    if (width < 1 || height < 1 || width > TUI_MAX_W || height > TUI_MAX_H) {
        fprintf(stderr, "[vex] semicolon: dump size %dx%d out of range (max %dx%d)\n",
                width, height, TUI_MAX_W, TUI_MAX_H);
        return 1;
    }
    Scene scene;
    sceneBuild(&scene, file, TUI_MAX_ROWS);
    if (scene.root == nullptr) {
        fprintf(stderr, "[vex] semicolon: scene construction failed\n");
        return 1;
    }
    sceneSyncRows(&scene, file, 0, 0, editorVisibleRows(height));
    TerminalCanvas canvas = TerminalCanvas(gCells, gStyles, gFg, gBg, (size_t) width, (size_t) height);
    TerminalCanvas_fill(&canvas, ' ', TerminalPen());
    TerminalPanel_paint(scene.root, &canvas);
    drawChrome(&canvas, (size_t) width, (size_t) height, scene.railWidth);
    for (int y = 0; y < height; ++y) {
        TerminalCanvasTextRow row = TerminalCanvas_copyRow(&canvas, (size_t) y);
        fwrite(row.data, 1, row.bytes, stdout);
        fputc('\n', stdout);
    }
    sceneFree(&scene);
    return 0;
}

// A diagnostic that prints what the terminal ACTUALLY sends. Whether a trackpad
// emits a horizontal wheel (buttons 66/67) is a property of the terminal emulator,
// not of this program, so arguing about it is pointless: swipe and read the answer.
;;DEBUG
static int runInputProbe(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        THROW("semicolon --input-probe requires a terminal");
        return 1;
    }
    SCREEN *screen = newterm(nullptr, stdout, stdin);
    if (screen == nullptr)
        return 1;
    (void) cbreak();
    (void) noecho();
    (void) curs_set(0);
    (void) keypad(stdscr, true);
    set_escdelay(25);
    (void) mousemask(0, nullptr);
    (void) define_key("\033[<", TUI_MOUSE_SGR);
    (void) define_key("\033[M", TUI_MOUSE_X10);
    (void) define_key("\033[A", KEY_UP);
    (void) define_key("\033[B", KEY_DOWN);
    (void) define_key("\033[C", KEY_RIGHT);
    (void) define_key("\033[D", KEY_LEFT);
    (void) mouseReporting(true);
    erase();
    mvaddstr(0, 0, "input probe: wheel, swipe sideways, drag, press keys. q quits.");
    mvaddstr(1, 0, "a horizontal swipe shows wheel-left / wheel-right if the terminal sends it.");
    refresh();
    TerminalInput input = TerminalInput_0();
    int line = 3;
    while (!gQuit) {
        int key = getch();
        if (key == ERR)
            continue;
        if (key == 'q' || key == 17)
            break;
        bool havePointer = false;
        if (key == TUI_MOUSE_SGR)
            havePointer = readMouseSuffix(&input, true);
        else if (key == TUI_MOUSE_X10)
            havePointer = readMouseSuffix(&input, false);
        if (line >= LINES - 1)
            line = 3;
        if (havePointer) {
            TerminalPointerEvent event = TerminalInput_getEvent(&input);
            const char *kind = "?";
            switch (event.kind) {
                case TERMINAL_POINTER_MOVE: kind = "move"; break;
                case TERMINAL_POINTER_DRAG: kind = "drag"; break;
                case TERMINAL_POINTER_PRESS: kind = "press"; break;
                case TERMINAL_POINTER_RELEASE: kind = "release"; break;
                case TERMINAL_POINTER_WHEEL_UP: kind = "wheel-up"; break;
                case TERMINAL_POINTER_WHEEL_DOWN: kind = "wheel-down"; break;
                case TERMINAL_POINTER_WHEEL_LEFT: kind = "wheel-left"; break;
                case TERMINAL_POINTER_WHEEL_RIGHT: kind = "wheel-right"; break;
                default: break;
            }
            mvprintw(line++, 0, "%-12s x=%-4zu y=%-4zu button=%d %s%s%s        ",
                     kind, event.x, event.y, (int) event.button,
                     event.shift ? "shift " : "", event.meta ? "meta " : "",
                     event.ctrl ? "ctrl" : "");
        } else {
            mvprintw(line++, 0, "key %-5d (no mouse event)        ", key);
        }
        refresh();
    }
    (void) mouseReporting(false);
    (void) curs_set(1);
    (void) endwin();
    (void) delscreen(screen);
    return 0;
}

static int runTerminal(FileText *file) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        THROW("semicolon requires terminal stdin and stdout (use --dump for a plain render)");
        return 1;
    }
    SCREEN *screen = newterm(nullptr, stdout, stdin);
    if (screen == nullptr) {
        THROW("semicolon: terminal initialization failed");
        return 1;
    }
    (void) setlocale(LC_CTYPE, "");
    if (has_colors()) {
        start_color();
        use_default_colors();
    }
    (void) cbreak();
    (void) noecho();
    (void) nonl();
    (void) curs_set(0);
    (void) mousemask(0, nullptr);   // ncurses' own mouse ABI is not used here
    // keypad decodes the arrow and page keys; without it a keypress is just bytes
    // and there is no way to scroll from the keyboard at all.
    (void) keypad(stdscr, true);
    (void) notimeout(stdscr, false);
    set_escdelay(25);
    // Bind the two mouse introducers so ncurses hands them over as one code instead
    // of trying to match the whole sequence against terminfo.
    (void) define_key("\033[<", TUI_MOUSE_SGR);
    (void) define_key("\033[M", TUI_MOUSE_X10);
    // The cursor keys need binding too. A terminal in its normal mode sends CSI
    // (ESC [ C), but many terminfo entries only list the application-mode SS3 form
    // (ESC O C), so ncurses failed to match the arrows it actually receives and they
    // fell through as raw bytes. Both forms are bound to the same key codes.
    (void) define_key("\033[A", KEY_UP);
    (void) define_key("\033[B", KEY_DOWN);
    (void) define_key("\033[C", KEY_RIGHT);
    (void) define_key("\033[D", KEY_LEFT);
    (void) define_key("\033OA", KEY_UP);
    (void) define_key("\033OB", KEY_DOWN);
    (void) define_key("\033OC", KEY_RIGHT);
    (void) define_key("\033OD", KEY_LEFT);
    timeout(TUI_POLL_MS);
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    (void) mouseReporting(true);

    int height = 0;
    int width = 0;
    getmaxyx(stdscr, height, width);
    if (width > TUI_MAX_W)
        width = TUI_MAX_W;
    if (height > TUI_MAX_H)
        height = TUI_MAX_H;
    Scene scene;
    sceneBuild(&scene, file, TUI_MAX_ROWS);
    bool ok = scene.root != nullptr;
    Interaction interaction = { .input = TerminalInput_0() };
    TerminalSurface surface = TerminalSurface_0();
    size_t scrollTop = 0;
    size_t scrollLeft = 0;
    int knownRows = height;
    int knownColumns = width;
    if (!ok)
        THROW("semicolon: scene construction failed");

    while (ok && !gQuit) {
        // ncurses caches the screen size and only refreshes it on resizeterm - and
        // resizeterm(0, 0) is not accepted, while KEY_RESIZE is not guaranteed to
        // arrive at all. So the size is read from the tty directly. It is applied
        // ONLY when it actually changed: calling resizeterm unconditionally dirties
        // the screen every pass and ncurses repaints forever, which is a busy loop
        // disguised as housekeeping.
        struct winsize terminalSize;
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &terminalSize) == 0 && terminalSize.ws_row > 0
            && terminalSize.ws_col > 0
            && ((int) terminalSize.ws_row != knownRows || (int) terminalSize.ws_col != knownColumns)) {
            knownRows = (int) terminalSize.ws_row;
            knownColumns = (int) terminalSize.ws_col;
            (void) resizeterm(knownRows, knownColumns);
            (void) clear();
        }
        int rows = 0;
        int columns = 0;
        getmaxyx(stdscr, rows, columns);
        if (columns > TUI_MAX_W)
            columns = TUI_MAX_W;
        if (rows > TUI_MAX_H)
            rows = TUI_MAX_H;
        width = columns;
        height = rows;
        size_t visible = editorVisibleRows(height);
        size_t codeLeft = codeLeftColumn(scene.railWidth);
        size_t codeRight = width > 0 ? (size_t) width - 2u : 0;
        size_t maxTop = maxScrollTop(file, visible);
        size_t maxLeft = maxScrollLeft(file, codeColumns(scene.railWidth, width));
        scrollTop = clampScroll(scrollTop, maxTop);
        scrollLeft = clampScroll(scrollLeft, maxLeft);
        if (visible == 0 || codeRight <= codeLeft) {
            erase();
            mvaddstr(0, 0, "terminal too small");
            refresh();
            timeout(TUI_POLL_MS);
            int small = getch();
            if (small == 'q' || small == 17)
                break;
            continue;
        }
        // All input arrives here: keys, and the mouse bytes the decoder turns into
        // normalized events.
        // One read may wait up to the poll timeout, so an idle shell sleeps
        // instead of spinning. Every later read in this frame is NON-BLOCKING:
        // with a timeout still set, the read that finds the buffer empty would
        // stall the paint by the whole timeout, which is exactly the lag a drag
        // exposes.
        timeout(TUI_POLL_MS);
        int key = getch();
        timeout(0);
        size_t handled = 0;
        while (key != ERR && handled < TUI_EVENTS_PER_FRAME) {
            ++handled;
            if (key == 17 || key == 'q') {
                gQuit = 1;
                break;
            }
            bool havePointer = false;
            if (key == KEY_RESIZE) {
                (void) clear();
            } else if (key == TUI_MOUSE_SGR) {
                havePointer = readMouseSuffix(&interaction.input, true);
            } else if (key == TUI_MOUSE_X10) {
                havePointer = readMouseSuffix(&interaction.input, false);
            } else {
                // Deliberately NOT fed to TerminalInput: it owns "ESC [", so an arrow
                // it did not recognise would be swallowed as a malformed mouse
                // sequence instead of reaching the scroll step below.

                ScrollStep step = scrollStepFor(key);
                if (step.keyed) {
                    scrollTop = applyScroll(scrollTop, step.rows, maxTop);
                    scrollLeft = applyScroll(scrollLeft, step.columns, maxLeft);
                }
            }
            if (havePointer) {
                TerminalPointerEvent event = TerminalInput_getEvent(&interaction.input);
                interaction.havePointer = true;
                interaction.pointerX = event.x;
                interaction.pointerY = event.y;
                size_t divider = scene.railWidth - 1u;
                switch (event.kind) {
                    case TERMINAL_POINTER_WHEEL_UP:
                    case TERMINAL_POINTER_WHEEL_DOWN: {
                        bool down = event.kind == TERMINAL_POINTER_WHEEL_DOWN;
                        // Shift turns a vertical wheel into a horizontal pan, which
                        // is how a plain mouse can reach the columns at all.
                        if (event.shift)
                            scrollLeft = applyScroll(scrollLeft,
                                                     down ? (int) TUI_SCROLL_COLS : -(int) TUI_SCROLL_COLS,
                                                     maxLeft);
                        else
                            scrollTop = applyScroll(scrollTop,
                                                    down ? (int) TUI_SCROLL_ROWS : -(int) TUI_SCROLL_ROWS,
                                                    maxTop);
                        break;
                    }
                    case TERMINAL_POINTER_WHEEL_LEFT:
                        scrollLeft = applyScroll(scrollLeft, -(int) TUI_SCROLL_COLS, maxLeft);
                        break;
                    case TERMINAL_POINTER_WHEEL_RIGHT:
                        scrollLeft = applyScroll(scrollLeft, (int) TUI_SCROLL_COLS, maxLeft);
                        break;
                    case TERMINAL_POINTER_PRESS:
                        interaction.pressed = true;
                        // Three cells of tolerance, and the rail follows the pointer
                        // RELATIVE to where the grab began. Resizing to the absolute
                        // pointer cell made the divider jump whenever the grab
                        // landed a cell off centre.
                        if (event.x + 1u >= divider && event.x <= divider + 1u) {
                            interaction.grabbing = true;
                            interaction.grabX = event.x;
                            interaction.grabWidth = scene.railWidth;
                        }
                        break;
                    case TERMINAL_POINTER_RELEASE:
                        interaction.grabbing = false;
                        interaction.pressed = false;
                        break;
                    case TERMINAL_POINTER_DRAG: {
                        if (interaction.grabbing) {
                            long moved = (long) event.x - (long) interaction.grabX;
                            long wanted = (long) interaction.grabWidth + moved;
                            if (wanted >= (long) TUI_RAIL_MIN
                                && wanted <= (long) columns - (long) TUI_RAIL_MARGIN) {
                                sceneSetRailWidth(&scene, (size_t) wanted);
                                // The code area moved with the rail, so its bounds did.
                                codeLeft = codeLeftColumn(scene.railWidth);
                                maxLeft = maxScrollLeft(file, codeColumns(scene.railWidth, columns));
                                scrollLeft = clampScroll(scrollLeft, maxLeft);
                            }
                        }
                        // Edge autoscroll belongs to a DRAG, not to hovering: acting
                        // on plain motion made the view scroll whenever the pointer
                        // passed over the first or last editor row.
                        if (event.y == editorFirstRow())
                            scrollTop = applyScroll(scrollTop, -1, maxTop);
                        else if (event.y == height - 2u)
                            scrollTop = applyScroll(scrollTop, 1, maxTop);
                        if (event.x <= codeLeft)
                            scrollLeft = applyScroll(scrollLeft, -1, maxLeft);
                        else if (event.x >= codeRight)
                            scrollLeft = applyScroll(scrollLeft, 1, maxLeft);
                        break;
                    }
                    case TERMINAL_POINTER_MOVE:
                        break;   // hover is reported and deliberately does nothing yet
                    default:
                        break;
                }
            }
            key = getch();
        }

        TerminalCanvas back = TerminalCanvas(gCells, gStyles, gFg, gBg, (size_t) width, (size_t) height);
        TerminalCanvas front = TerminalCanvas(gFrontCells, gFrontStyles, gFrontFg, gFrontBg,
                                             (size_t) width, (size_t) height);
        TerminalCanvas_fill(&back, ' ', TerminalPen());
        if (!TerminalSurface_isBound(&surface)
            || TerminalSurface_getBack(&surface).gridWidth != (size_t) width
            || TerminalSurface_getBack(&surface).gridHeight != (size_t) height) {
            TerminalCanvas_fill(&front, ' ', TerminalPen());
            if (!TerminalSurface_bind(&surface, &back, &front, blitRun, nullptr)) {
                ok = false;
                break;
            }
        }
        sceneSyncRows(&scene, file, scrollTop, scrollLeft, visible);
        TerminalPanel_paint(scene.root, &back);
        drawChrome(&back, (size_t) width, (size_t) height, scene.railWidth);
        TerminalPresentResult presented = TerminalSurface_present(&surface);
        if (!presented.ok)
            ok = false;
        if (ok)
            ok = refresh() != ERR;
        if (!ok)
            break;
    }
    (void) mouseReporting(false);
    (void) curs_set(1);
    (void) endwin();
    (void) delscreen(screen);
    sceneFree(&scene);
    if (!ok)
        THROW("semicolon: terminal operation failed; terminal restored");
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    initTextures();
    bool dump = false;
    bool fileGiven = false;
    int width = 0;
    int height = 0;
    char path[1024];
    path[0] = '\0';
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            puts("semicolon [--dump [WxH]] [--file PATH] [--help]");
            puts("  no arguments  draw the IDE shell on this terminal (Ctrl-Q or q quits)");
            puts("  --file PATH   show PATH in the editor (default: vexspoke's oop/type.h)");
            puts("  --dump        print one frame as plain text instead of using a terminal");
            puts("  --input-probe print the mouse and key events this terminal actually sends");
            return 0;
        }
        if (strcmp(argv[i], "--dump") == 0) {
            dump = true;
            if (i + 1 < argc && strchr(argv[i + 1], 'x') != nullptr) {
                char *end = nullptr;
                long parsedWidth = strtol(argv[++i], &end, 10);
                char *heightEnd = nullptr;
                long parsedHeight = end != nullptr ? strtol(end + 1, &heightEnd, 10) : 0;
                if (end == nullptr || *end != 'x' || heightEnd == nullptr || *heightEnd != '\0'
                    || parsedWidth < 1 || parsedHeight < 1) {
                    THROW("semicolon: --dump size must look like 103x23");
                    return 1;
                }
                width = (int) parsedWidth;
                height = (int) parsedHeight;
            }
            continue;
        }
        if (strcmp(argv[i], "--input-probe") == 0) {
            initTextures();
            return runInputProbe();
        }
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            (void) snprintf(path, sizeof path, "%s", argv[++i]);
            fileGiven = true;
            continue;
        }
        THROW("semicolon: unknown argument");
        return 1;
    }
    if (!fileGiven) {
        char directory[1000];
        (void) snprintf(directory, sizeof directory, "%s", argv[0]);
        char *slash = strrchr(directory, '/');
        if (slash != nullptr)
            *slash = '\0';
        (void) snprintf(path, sizeof path,
                        "%s/../../../../suite/ecosystem/vexspoke/src/oop/type.h", directory);
    }
    FileText file;
    if (!fileLoad(&file, path)) {
        fprintf(stderr, "[vex] semicolon: cannot read %s\n", path);
        return 1;
    }
    int status = 0;
    if (dump) {
        if (width < 1)
            width = 103;
        if (height < 1)
            height = 23;
        status = dumpFrame(width, height, &file);
    } else {
        status = runTerminal(&file);
    }
    fileFree(&file);
    return status;
}
