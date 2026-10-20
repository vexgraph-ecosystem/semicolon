#define _XOPEN_SOURCE_EXTENDED 1
#include "terminal/terminal_panel.h"
#include "terminal/terminal_text.h"
#include "terminal/terminal_anchor.h"
#include "terminal/terminal_canvas.h"
#include "terminal/terminal_color.h"
#include "terminal/terminal_style.h"
#include "terminal/terminal_surface.h"
#include "terminal/terminal_tree_view.h"
#include "terminal/terminal_input.h"
#include "exception/throw.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/test.h"
#include "annotation/debug.h"
#include <locale.h>
#include <wchar.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <strings.h>
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
 *   gRule/gSpaces/emptyLine         static textures: separators and blank runs.
 *   gGutter[TUI_MAX_ROWS][16]       per-row line-number text buffers.
 *   gLineChip[16]                   the status bar's line-position text buffer.
 *   gPairs[]                        registered ncurses colour pairs.
 * STRUCTS:
 *   FileText  owned file bytes, line table, and the display name.
 *   TuiRow    one editor row: its gutter text and its code text.
 *   Scene     the tree, dynamic line-number field width, and runtime handles.
 *     root, header, rail, railId, tabSpacerId, modeWidth, rows[], rowCount,
 *     lineChip, railWidth, numberWidth, railRows[], railRowCount, railText[],
 *     breadcrumb, activeTab.
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
 *   static void railBuild(const char *)   the project as a tree, one level at a time
 *   static bool railReadChildren(id) / railRevealPath(path)
 *   static void railClick(y) / railScrollBy(rows)   the rail's pointer contract
 *   static void sceneSyncRail(Scene *)   point the row pool at the visible rows
 *   static bool openFile(path, node)   swap the editor's file, not the scene
 *   static void cacheLoad/cacheSave(void)   the project's session, kept outside the tree
 *   static TerminalPanel *buildRail(Scene *, size_t rowCount)
 *   static TerminalPanel *buildEditor(Scene *, size_t)
 *   static TerminalPanel *buildCentre(Scene *, size_t)
 *   static void sceneBuild(Scene *, FileText *, size_t)
 *   static void sceneSyncRows(Scene *, const FileText *, size_t first, size_t left, size_t visible)
 *   static size_t cellsOf/bytesForCells(const char *, size_t, ...)  cell offsets
 *   static size_t maxScrollTop/maxScrollLeft(clampScroll/applyScroll)
 *   static bool glideActive(const ScrollGlide *)   a scroll is still in flight
 *   static void glideImpulse/glideHalt/glideClamp(const ScrollGlide *, ...)
 *   static double glideStep(double *shown, double target)   one frame of easing
 *   static size_t editorFirstRow/editorVisibleRows/codeLeftColumn/codeColumns
  *     codeLeftColumn includes the two-cell leading and trailing number gaps.
 *   static void sceneSetRailWidth(Scene *, size_t width)
 *   static void resolveDeviceBackground(void)   what a fade fades TO
 *   static bool queryDeviceBackground(uint32_t *) / parseDeviceBackground
 *   static bool parsePackedHex(const char *, uint32_t *)
 *   static short colorIndex(uint32_t) / pairFor(fg, bg) / attrsFor(style)
 *   static bool blitRun(void *, TerminalRun)   wide-character ncurses output
 *   static void drawChrome(canvas, width, height, size_t railWidth)
 *   static void handlePointer(Scene *, Interaction *, int width, int height)
 *   static int dumpFrame(int, int, FileText *)
 *   static void frameBracket(void *, TerminalPresentPhase)  the frame transaction
 *   static int runInputProbe(void)   DEBUG: print what the terminal sends
 *   static int runBackgroundProbe(void)   DEBUG: what is this terminal's background?
 *   static int runTerminal(FileText *)
 * Entry:
 *   int main(int, char **)   --help / --dump [WxH] / --file PATH / interactive.
 * Contracts:
 *   The interactive path REQUIRES a tty on stdin and stdout and refuses before
 *   ncurses otherwise. It selects the environment's LC_CTYPE before newterm and
 *   sends canvas cells through ncurses' wide-character API. Ctrl-C is the ONLY
 *   quit key - a letter key must not quit,
 *   because the terminal is a text editor and letters are text; the terminal is
 *   restored on every normal exit and on SIGINT/SIGTERM (SIGKILL or a crash
 *   cannot promise that).
 *   Mouse reporting is enabled for ANY-MOTION plus SGR encoding, so hover and
 *   drag both arrive; it is disabled again on exit, which hands the mouse back to
 *   the terminal's own selection. Wheel scrolls three lines, a pointer resting on
 *   the top or bottom editor row scrolls one line per motion event, and a press
 *   within one cell of the divider begins a drag that resizes the rail. No file
 *   is written, no network is touched and no program is executed.
 *   Colour carries REAL alpha: a translucent pen is composited over its cell's
 *   background, and that in turn over the device background, before the device is
 *   handed anything - a terminal has no alpha channel, so this is the only place a
 *   fade can happen. The device background is resolved at startup and is what a
 *   fade actually fades toward; fully opaque artwork passes through untouched.
 * ============================================================================ */

#define TUI_MAX_W 400
#define TUI_MAX_H 200
#define TUI_MAX_ROWS 160
// One rail row's composed text: indent, fold marker, name. Sized so the deepest name a
// 30-cell rail can show still fits rather than being silently cut.
#define TUI_RAIL_TEXT 96
// Where a project's session lives, relative to the home directory. Machine-scoped and
// outside the project on purpose: browsing a checkout must never write into it.
#if defined(__APPLE__)
#define TUI_STATE_DIR "Library/Application Support/vexgraph/semicolon"
#else
#define TUI_STATE_DIR ".local/state/vexgraph/semicolon"
#endif
#define TUI_MAX_FILE_BYTES (4u * 1024u * 1024u)
#define GUTTER_MIN_NUMBER_WIDTH 1u
// The design puts TWO cells between the number field and the first code cell. It is a reserved band in the
// editor's flow rather than padding copied into every line, which keeps the code text a
// borrowed pointer into the file instead of a per-frame copy.
#define TUI_CODE_PAD 2u
// The breadcrumb's band. It is a FIXED width rather than the width of whatever it says,
// because the crumb is re-pointed whenever the open file changes: a band measured once
// would clip every later path, and the design draws the crumb area wide enough for exactly
// that reason.
#define TUI_CRUMB_CELLS 44u
#define TUI_POLL_MS 100
#define TUI_RAIL_MIN 12u
#define TUI_RAIL_MARGIN 24u
// One wheel notch asks for ONE line, not three. A trackpad quantises a single gesture
// into dozens of notches, so three each was a screen-and-a-half per flick.
#define TUI_SCROLL_ROWS 1u
#define TUI_SCROLL_COLS 1u
// The frame interval that makes the glide 60fps, and the two numbers that give it
// friction. Each frame covers TUI_SCROLL_EASE of the distance still to travel, so a
// burst of notches arrives as a decelerating glide and settles; below
// TUI_SCROLL_EPSILON of remaining distance it snaps, which is what stops the motion
// instead of leaving it creeping toward a target forever.
#define TUI_FRAME_MS 16
#define TUI_SCROLL_EASE 0.18
#define TUI_SCROLL_EPSILON 0.4
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
// FADED, not merely dark. A translucent colour lands between itself and whatever is
// behind it, so the gutter recedes on a dark theme and on a light one from ONE value
// instead of two. Alpha 0 is the unset sentinel (inherit), so a fade keeps a non-zero
// alpha - there is deliberately no such thing here as "fully transparent".
#define COL_GUTTER TERMINAL_RGBA(0xAA, 0xAA, 0xC4, 0x7A)
// The shell's one HIGHLIGHT, and it is a tint rather than a block: at 22% the accent
// reads as "this one" over any theme's own background instead of covering it up.
#define COL_SELECT TERMINAL_RGBA(0x7A, 0xC8, 0xFF, 0x38)
// Which tab is the open one. The design lights exactly one tab; naming it keeps that
// from being a bare literal in a loop.
#define TUI_ACTIVE_TAB 3u

// The terminal's OWN background: what shows through every transparent cell, and so what
// a translucent colour is composited against. Resolved at startup (resolveDeviceBackground)
// and this is only the last resort, for a terminal that neither declares nor answers.
// It only ever affects TRANSLUCENT colours - opaque artwork is identical either way.
#define TUI_DEVICE_BG_FALLBACK TERMINAL_RGB(0x1A, 0x1A, 0x1A)
// How long the device background query waits, and how much of the answer is kept. A
// terminal that answers does so in a millisecond or two; one that ignores the query
// never answers at all, so this window is the whole cost of asking.
#define TUI_PROBE_MS 120
#define TUI_PROBE_BYTES 64
static uint32_t gDeviceBg = TUI_DEVICE_BG_FALLBACK;
static const char *gDeviceBgSource = "the built-in fallback";

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
    TerminalPanel *header;
    TerminalPanel *rail;
    TerminalChildId railId;
    TerminalChildId tabSpacerId;
    size_t modeWidth;
    TuiRow rows[TUI_MAX_ROWS];
    size_t rowCount;
    TerminalText *lineChip;
    size_t railWidth;
    size_t numberWidth;
    // The rail's rows. One Text per VISIBLE row, created once and re-pointed, so folding
    // the tree and scrolling it are both allocation-free - exactly the trick the editor
    // rows use, and the reason the row count is the panel's height rather than the
    // tree's size.
    TerminalText *railRows[TUI_MAX_ROWS];
    size_t railRowCount;
    char railText[TUI_MAX_ROWS][TUI_RAIL_TEXT];
    // Re-pointed when the open file changes, so the header cannot describe a file the
    // editor is not showing.
    TerminalText *breadcrumb;
    TerminalText *activeTab;
} Scene;

// SLOT RECORD: RailEntry
typedef struct RailEntry {
    char label[TUI_RAIL_TEXT];      // borrowed by the tree view, so it lives here
    char path[1024];                // absolute, for reading and for opening
    TerminalTreeNodeId parent;
    bool loaded;                    // children have been read (exactly once)
} RailEntry;

static TerminalTreeView *gRail;
static RailEntry *gRailEntries;
static size_t gRailEntryCount;
static size_t gRailEntryCapacity;
static char gRailRoot[1024];
static TerminalTreeNodeId gRailRootNode;
static TerminalTreeNodeId gRailOpenNode;
static char gOpenPath[1024];
static FileText *gOpenFile;         // the editor's file, owned by runTerminal
static Scene *gScene;               // the live scene, or null outside an interactive run
static bool gResetView;             // a newly opened file starts at its top-left
static char gActiveTab[TUI_RAIL_TEXT];
static char gCachedOpen[1024];      // restored from the project's session
static size_t gCachedRail;

typedef struct Interaction {
    TerminalInput input;
    bool grabbing;          // the divider is held
    bool tracking;          // the bottom editor rows' horizontal slider is held
    size_t trackX;          // pointer cell where the tracking drag began
    size_t trackLeft;       // horizontal offset when the tracking drag began
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

static TerminalPanel *buildChromeRow(Scene *scene) {
    TerminalPanel *bar = TerminalPanel();
    if (bar == nullptr)
        return nullptr;
    // TWO MODES and nothing else: no rule underlay, no dash runs, no undo/redo. The
    // closed one is dim and the open one carries the shell's one highlight - a tint rather
    // than a block, so the theme still shows through it.
    static const char *modes[] = { "(); Code", ">w< agents" };
    size_t used = 0;
    for (size_t i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
        TerminalText *mode = chip(modes[i]);
        (void) TerminalText_setForegroundColor(mode, i == 0 ? COL_ACCENT : COL_MUTED);
        if (i == 0)
            (void) TerminalText_setBackgroundColor(mode, COL_SELECT);
        size_t cells = TerminalText_measure(mode);
        if (!putText(bar, mode, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, cells))) {
            TerminalPanel_free(bar);
            return nullptr;
        }
        used += cells;
        if (i == 0) {
            if (!putText(bar, plain(" ", COL_TEXT),
                         TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, 1))) {
                TerminalPanel_free(bar);
                return nullptr;
            }
            ++used;
        }
    }
    // The tab strip follows the seam: this band reserves the current rail remainder and
    // sceneSetRailWidth moves it when the user drags the divider.
    TerminalText *tabSpacer = plain(" ", COL_TEXT);
    if (tabSpacer == nullptr) {
        TerminalPanel_free(bar);
        return nullptr;
    }
    scene->tabSpacerId = TerminalPanel_addText(
        bar, tabSpacer,
        TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT,
                         scene->railWidth > used ? scene->railWidth - used : 0u));
    if (!TerminalPanel_isChildId(scene->tabSpacerId)) {
        TerminalText_free(tabSpacer);
        TerminalPanel_free(bar);
        return nullptr;
    }
    scene->header = bar;
    scene->modeWidth = used;
    // The compact trick: the tab strip shares the chrome row, so it costs no row.
    static const char *tabLabels[] = { "helloWorld.c  x", "placeholder.txt  x", "terminal  x", "type.h  x" };
    for (size_t i = 0; i < sizeof tabLabels / sizeof tabLabels[0]; ++i) {
        bool open = i == TUI_ACTIVE_TAB;
        TerminalText *tab = chip(open && gActiveTab[0] != '\0' ? gActiveTab : tabLabels[i]);
        if (open)
            scene->activeTab = tab;
        (void) TerminalText_setForegroundColor(tab, open ? COL_TEXT : COL_MUTED);
        if (open)
            (void) TerminalText_setBackgroundColor(tab, COL_SELECT);
        if (!putText(bar, tab, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT,
                                                TerminalText_measure(tab)))) {
            TerminalPanel_free(bar);
            return nullptr;
        }
    }
    (void) TerminalPanel_setForegroundColor(bar, COL_TEXT);
    return bar;
}

// Defined with the rest of the rail: the header names the file on screen, so it needs
// the project root and the open path.
static void breadcrumbFor(const char *path, char *out, size_t size);

static TerminalPanel *buildStatusBar(Scene *scene, const FileText *file) {
    TerminalPanel *bar = TerminalPanel();
    if (bar == nullptr)
        return nullptr;
    // NO RULE and NO DASH RUNS. The footer is the breadcrumb on the left and the chips on
    // the right, with an otherwise transparent row between them - which is also what keeps
    // the whole floor of the window draggable without drawing anything to drag.

    // The chip BORROWS this string for the life of the tree, so it must outlive
    // this function: a stack buffer here renders as garbage. The crumb describes the file
    // on screen - a header naming a different file is a lie, not a placeholder.
    breadcrumbFor(gOpenPath[0] != '\0' ? gOpenPath : file->name, gBreadcrumb,
                  sizeof gBreadcrumb);
    TerminalText *crumb = chip(gBreadcrumb);
    scene->breadcrumb = crumb;
    (void) TerminalText_setForegroundColor(crumb, COL_TEXT);
    // A FIXED band, not the width of whatever the crumb says today: the crumb is
    // re-pointed whenever the open file changes, and a band measured once would clip every
    // later path. The design draws this area wide for exactly that reason.
    if (!putText(bar, crumb, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, TUI_CRUMB_CELLS))) {
        TerminalPanel_free(bar);
        return nullptr;
    }
    static const size_t rightExtents[] = { 1, 7, 1, 6, 1, 8, 1, 10 };
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

// ---------------------------------------------------------------- the rail's tree
//
// The rail IS the project. It is read ONE DIRECTORY AT A TIME, when a folder opens, so
// starting in a big checkout costs the listing of its top level and nothing else: no
// recursive walk, no watch, no index to go stale. A symlink loop cannot hang it either,
// because following one only adds another directory that is read when it is opened.
//
// The tree BORROWS every label, so each name must live as long as its node. It lives in
// RailEntry, indexed by (id - 1): ids are handed out in order and nodes are never
// removed, which makes that index stable for the life of the process.

// One listing, sorted before insertion. readdir order is not a contract, and a tree that
// reshuffles between runs is not a tree.
// SLOT RECORD: DirEntry
typedef struct DirEntry {
    char name[TUI_RAIL_TEXT];
    bool folder;
} DirEntry;

static int compareDirEntries(const void *left, const void *right) {
    const DirEntry *a = left;
    const DirEntry *b = right;
    if ((*a).folder != (*b).folder)
        return (*a).folder ? -1 : 1;              // folders first, like every browser
    return strcasecmp((*a).name, (*b).name);
}

static RailEntry *railEntryFor(TerminalTreeNodeId id) {
    if (id == TERMINAL_TREE_NO_NODE || (size_t) id > gRailEntryCount)
        return nullptr;
    return &gRailEntries[(size_t) id - 1u];
}

static bool railReserveEntry(void) {
    if (gRailEntryCount < gRailEntryCapacity)
        return true;
    size_t next = gRailEntryCapacity == 0 ? 32u : gRailEntryCapacity * 2u;
    RailEntry *grown = realloc(gRailEntries, next * sizeof(RailEntry));
    if (grown == nullptr)
        return false;
    gRailEntries = grown;
    gRailEntryCapacity = next;
    return true;
}

// Add a node and give it a home for its borrowed label. The tree is given an empty label
// first and re-pointed afterwards, because the entry array may move: handing it the
// address of a temporary would leave the tree reading freed memory.
static TerminalTreeNodeId railAddNode(TerminalTreeNodeId parent, const char *name,
                                      const char *path, bool folder) {
    if (gRail == nullptr || !railReserveEntry())
        return TERMINAL_TREE_NO_NODE;
    TerminalTreeNodeId id = TerminalTreeView_addChild(gRail, parent, "", folder);
    if (id == TERMINAL_TREE_NO_NODE)
        return TERMINAL_TREE_NO_NODE;
    RailEntry *entry = &gRailEntries[gRailEntryCount];
    (void) snprintf(entry->label, sizeof entry->label, "%s", name);
    (void) snprintf(entry->path, sizeof entry->path, "%s", path);
    entry->parent = parent;
    entry->loaded = false;
    ++gRailEntryCount;
    (void) TerminalTreeView_setLabel(gRail, id, entry->label);
    return id;
}

// Read one directory, once. A read that fails is remembered as "loaded": retrying on
// every frame would hammer the same unreadable path for no gain.
static bool railReadChildren(TerminalTreeNodeId id) {
    RailEntry *entry = railEntryFor(id);
    if (entry == nullptr)
        return false;
    if ((*entry).loaded)
        return true;
    (*entry).loaded = true;
    char parentPath[1024];
    (void) snprintf(parentPath, sizeof parentPath, "%s", (*entry).path);
    DIR *directory = opendir(parentPath);
    if (directory == nullptr)
        return false;
    DirEntry *found = nullptr;
    size_t count = 0;
    size_t capacity = 0;
    struct dirent *item = nullptr;
    while ((item = readdir(directory)) != nullptr) {
        if (strcmp((*item).d_name, ".") == 0 || strcmp((*item).d_name, "..") == 0)
            continue;
        if (strlen((*item).d_name) >= TUI_RAIL_TEXT)
            continue;
        if ((*item).d_name[0] == '.')
            continue;                             // dotfiles hidden, like every file pane
        char child[1024];
        (void) snprintf(child, sizeof child, "%s/%s", parentPath, (*item).d_name);
        struct stat info;
        if (stat(child, &info) != 0)
            continue;
        bool folder = S_ISDIR(info.st_mode);
        if (!folder && !S_ISREG(info.st_mode))
            continue;                             // no fifos, no devices, no sockets
        if (count == capacity) {
            size_t next = capacity == 0 ? 32u : capacity * 2u;
            DirEntry *grown = realloc(found, next * sizeof(DirEntry));
            if (grown == nullptr)
                break;
            found = grown;
            capacity = next;
        }
        (void) snprintf(found[count].name, sizeof found[count].name, "%s", (*item).d_name);
        found[count].folder = folder;
        ++count;
    }
    (void) closedir(directory);
    if (count > 1u)
        qsort(found, count, sizeof(DirEntry), compareDirEntries);
    // The entry array may move as children are added, so nothing taken above is used here.
    for (size_t index = 0; index < count; ++index) {
        char child[1024];
        (void) snprintf(child, sizeof child, "%s/%s", parentPath, found[index].name);
        (void) railAddNode(id, found[index].name, child, found[index].folder);
    }
    free(found);
    return true;
}

static TerminalTreeNodeId railFindChild(TerminalTreeNodeId parent, const char *name) {
    for (size_t index = 0; index < gRailEntryCount; ++index)
        if (gRailEntries[index].parent == parent && strcmp(gRailEntries[index].label, name) == 0)
            return (TerminalTreeNodeId) (index + 1u);
    return TERMINAL_TREE_NO_NODE;
}

// Open the path to one node by walking it from the root, reading as it goes. Returns the
// node, or NO_NODE when the path is outside this project.
static TerminalTreeNodeId railRevealPath(const char *path) {
    if (gRail == nullptr || path == nullptr || gRailRootNode == TERMINAL_TREE_NO_NODE)
        return TERMINAL_TREE_NO_NODE;
    size_t rootLength = strlen(gRailRoot);
    if (rootLength == 0 || strncmp(path, gRailRoot, rootLength) != 0 || path[rootLength] != '/')
        return TERMINAL_TREE_NO_NODE;
    TerminalTreeNodeId current = gRailRootNode;
    const char *at = path + rootLength + 1u;
    while (*at != '\0') {
        char component[TUI_RAIL_TEXT];
        size_t length = 0;
        while (at[length] != '\0' && at[length] != '/' && length + 1u < sizeof component)
            ++length;
        memcpy(component, at, length);
        component[length] = '\0';
        at += length;
        if (*at == '/')
            ++at;
        (void) railReadChildren(current);
        TerminalTreeNodeId child = railFindChild(current, component);
        if (child == TERMINAL_TREE_NO_NODE)
            return TERMINAL_TREE_NO_NODE;
        current = child;
        if (*at != '\0')
            (void) TerminalTreeView_setExpanded(gRail, current, true);
    }
    return current;
}

// The breadcrumb is `root>dir>file` - the shape the design uses, and the only one that
// fits beside the status chips. A full path would push them off the row.
static void breadcrumbFor(const char *path, char *out, size_t size) {
    const char *relative = path;
    bool inside = false;
    size_t rootLength = strlen(gRailRoot);
    if (rootLength != 0 && strncmp(path, gRailRoot, rootLength) == 0 && path[rootLength] == '/') {
        relative = path + rootLength + 1u;
        inside = true;
    }
    size_t slashes = 0;
    for (const char *at = relative; *at != '\0'; ++at)
        if (*at == '/')
            ++slashes;
    const char *tail = relative;
    size_t keep = slashes > 1u ? slashes - 1u : 0u;
    for (const char *at = relative; *at != '\0' && keep != 0u; ++at)
        if (*at == '/') {
            tail = at + 1;
            --keep;
        }
    // A file opened from OUTSIDE the project - the default file, or an explicit --file -
    // is shown as outside it. Prefixing it with the project's name would claim a path that
    // does not exist, and a header that lies about where a file is, is worse than a short
    // one.
    const char *rootName = strrchr(gRailRoot, '/');
    rootName = rootName == nullptr ? gRailRoot : rootName + 1u;
    // The tail is copied with its separators rewritten, so the crumb uses ONE separator
    // throughout: mixing `>` and `/` in the same string reads as two different things.
    char normalised[512];
    size_t at = 0;
    for (const char *source = tail; *source != '\0' && at + 1u < sizeof normalised; ++source)
        normalised[at++] = *source == '/' ? '>' : *source;
    normalised[at] = '\0';
    // `project > file`: spaced separators, because the crumb is read as words rather than
    // parsed as a path. The design also shows a trailing `::symbol`; nothing here resolves
    // a symbol yet, so the crumb stops at the file rather than inventing one.
    char spaced[512];
    size_t spacedAt = 0;
    for (size_t index = 0; normalised[index] != '\0' && spacedAt + 4u < sizeof spaced;
         ++index) {
        if (normalised[index] == '>') {
            spaced[spacedAt++] = ' ';
            spaced[spacedAt++] = '>';
            spaced[spacedAt++] = ' ';
        } else {
            spaced[spacedAt++] = normalised[index];
        }
    }
    spaced[spacedAt] = '\0';
    if (inside)
        (void) snprintf(out, size, "%s > %s", rootName, spaced);
    else
        (void) snprintf(out, size, "… > %s", spaced);
}

// Open a file in the editor. The OLD file stays open when the new one cannot be read: a
// failed click must not blank the view and throw away the scroll position for nothing.
static bool openFile(const char *path, TerminalTreeNodeId node) {
    if (gOpenFile == nullptr || path == nullptr)
        return false;
    FileText opened;
    if (!fileLoad(&opened, path))
        return false;
    fileFree(gOpenFile);
    *gOpenFile = opened;
    (void) snprintf(gOpenPath, sizeof gOpenPath, "%s", path);
    gRailOpenNode = node;
    if (gRail != nullptr) {
        (void) TerminalTreeView_setSelected(gRail, node);
        (void) TerminalTreeView_reveal(gRail, node);
    }
    gResetView = true;
    if (gScene != nullptr) {
        breadcrumbFor(path, gBreadcrumb, sizeof gBreadcrumb);
        if (gScene->breadcrumb != nullptr)
            (void) TerminalText_setText(gScene->breadcrumb, gBreadcrumb, strlen(gBreadcrumb));
        (void) snprintf(gActiveTab, sizeof gActiveTab, "%s  x", opened.name);
        if (gScene->activeTab != nullptr)
            (void) TerminalText_setText(gScene->activeTab, gActiveTab, strlen(gActiveTab));
    }
    return true;
}

// The project's session, kept OUTSIDE the project. Writing state into the checkout would
// litter every repository the shell is pointed at, so it lives in the machine's state
// directory, one file per root, named by a hash of that root and carrying the root inside
// so a collision is caught instead of restoring somebody else's session. SEMICOLON_STATE
// overrides the directory, which is also how the owner test avoids touching a real one.
static void cachePathFor(const char *root, char *out, size_t size) {
    uint64_t hash = 1469598103934665603ull;
    for (const char *at = root; *at != '\0'; ++at) {
        hash ^= (unsigned char) *at;
        hash *= 1099511628211ull;
    }
    const char *override = getenv("SEMICOLON_STATE");
    if (override != nullptr && *override != '\0') {
        (void) snprintf(out, size, "%s/%016llx.state", override, (unsigned long long) hash);
        return;
    }
    const char *xdg = getenv("XDG_STATE_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        (void) snprintf(out, size, "%s/semicolon/%016llx.state", xdg,
                        (unsigned long long) hash);
        return;
    }
    const char *home = getenv("HOME");
    if (home == nullptr)
        home = ".";
    (void) snprintf(out, size, "%s/%s/%016llx.state", home, TUI_STATE_DIR,
                    (unsigned long long) hash);
}

// mkdir -p, without a shell: a state directory that does not exist yet is not an error.
static bool ensureDirectory(const char *path) {
    char buffer[1024];
    (void) snprintf(buffer, sizeof buffer, "%s", path);
    for (char *at = buffer + 1; *at != '\0'; ++at) {
        if (*at != '/')
            continue;
        *at = '\0';
        if (mkdir(buffer, 0700) != 0 && errno != EEXIST)
            return false;
        *at = '/';
    }
    return mkdir(buffer, 0700) == 0 || errno == EEXIST;
}

static void cacheLoad(const char *root) {
    gCachedOpen[0] = '\0';
    gCachedRail = 0;
    char path[1024];
    cachePathFor(root, path, sizeof path);
    FILE *handle = fopen(path, "r");
    if (handle == nullptr)
        return;
    char line[1300];
    bool sameRoot = false;
    char opened[1024];
    opened[0] = '\0';
    size_t width = 0;
    while (fgets(line, sizeof line, handle) != nullptr) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "root ", 5) == 0)
            sameRoot = strcmp(line + 5, root) == 0;
        else if (strncmp(line, "open ", 5) == 0)
            (void) snprintf(opened, sizeof opened, "%s", line + 5);
        else if (strncmp(line, "rail ", 5) == 0)
            width = (size_t) strtoul(line + 5, nullptr, 10);
    }
    (void) fclose(handle);
    if (!sameRoot)
        return;                                   // a hash collision, or a reused directory
    if (opened[0] != '\0')
        (void) snprintf(gCachedOpen, sizeof gCachedOpen, "%s", opened);
    gCachedRail = width;
}

// Staged and renamed, so an interrupted write cannot leave a half session that the next
// run would believe.
static void cacheSave(void) {
    if (gRailRoot[0] == '\0')
        return;
    char path[1024];
    cachePathFor(gRailRoot, path, sizeof path);
    char directory[1024];
    (void) snprintf(directory, sizeof directory, "%s", path);
    char *slash = strrchr(directory, '/');
    if (slash != nullptr) {
        *slash = '\0';
        if (!ensureDirectory(directory))
            return;
    }
    char staged[1100];
    (void) snprintf(staged, sizeof staged, "%s.tmp", path);
    FILE *handle = fopen(staged, "w");
    if (handle == nullptr)
        return;
    fprintf(handle, "root %s\n", gRailRoot);
    fprintf(handle, "open %s\n", gOpenPath);
    fprintf(handle, "rail %zu\n", gScene != nullptr ? gScene->railWidth : 0u);
    bool ok = fflush(handle) == 0;
    ok = fclose(handle) == 0 && ok;
    if (!ok || rename(staged, path) != 0)
        (void) remove(staged);
}

// The file to open when nothing else names one: the root's first regular file in sorted
// order. Deterministic, so two runs on the same project open the same thing.
static bool firstFileIn(const char *root, char *out, size_t size) {
    DIR *directory = opendir(root);
    if (directory == nullptr)
        return false;
    char best[TUI_RAIL_TEXT];
    best[0] = '\0';
    struct dirent *item = nullptr;
    while ((item = readdir(directory)) != nullptr) {
        if ((*item).d_name[0] == '.' || strlen((*item).d_name) >= TUI_RAIL_TEXT)
            continue;
        char child[1024];
        (void) snprintf(child, sizeof child, "%s/%s", root, (*item).d_name);
        struct stat info;
        if (stat(child, &info) != 0 || !S_ISREG(info.st_mode))
            continue;
        if (best[0] == '\0' || strcasecmp((*item).d_name, best) < 0)
            (void) snprintf(best, sizeof best, "%s", (*item).d_name);
    }
    (void) closedir(directory);
    if (best[0] == '\0')
        return false;
    (void) snprintf(out, size, "%s/%s", root, best);
    return true;
}

// Build the rail at startup: one root node, its top level read, and the open file
// revealed down the tree. Nothing recursive happens here - the rest of the project is
// read when a folder is opened.
static void railBuild(const char *root) {
    (void) snprintf(gRailRoot, sizeof gRailRoot, "%s", root);
    gRail = TerminalTreeView();
    if (gRail == nullptr)
        return;
    const char *name = strrchr(root, '/');
    name = name == nullptr ? root : name + 1u;
    if (*name == '\0')
        name = "/";
    gRailRootNode = railAddNode(TERMINAL_TREE_NO_NODE, name, root, true);
    if (gRailRootNode == TERMINAL_TREE_NO_NODE)
        return;
    (void) railReadChildren(gRailRootNode);
    (void) TerminalTreeView_setExpanded(gRail, gRailRootNode, true);
}

// Compose one rail row: indentation, the fold marker, and the name. Presentation lives
// here and structure lives in the tree, which is why the tree never had to know what a
// folder looks like.
static void railFormat(char *out, size_t size, TerminalTreeRow row, size_t width) {
    // The rail sits UNDER the frame's left border, which is painted over the first cell of
    // every row - so the margin is not decoration, it is what keeps the tree off the
    // border. The design's rows have always carried it. One extra level of indent is
    // reserved for that margin; beyond that the indent stops growing so a deep node still
    // shows a name instead of nothing but spaces.
    size_t deepest = width > 8u ? (width - 6u) / 2u : 1u;
    size_t levels = row.depth + 1u;
    if (levels > deepest)
        levels = deepest;
    size_t at = 0;
    for (size_t level = 0; level < levels && at + 3u < size; ++level) {
        out[at++] = ' ';
        out[at++] = ' ';
    }
    // Disclosure shapes stay Unicode through the wide-character curses device boundary.
    if (row.folder)
        (void) snprintf(out + at, size - at, "%s [] %s", row.expanded ? "⌄" : "›",
                        row.label);
    else
        (void) snprintf(out + at, size - at, "  %s", row.label);
    // Truncate on a CELL boundary and say so with one ellipsis: a row that silently loses
    // its tail cannot be told apart from a row with a shorter name.
    size_t available = width > 2u ? width - 2u : 0u;
    size_t length = strlen(out);
    if (cellsOf(out, length) > available) {
        size_t keep = available > 1u ? available - 1u : 0u;
        length = bytesForCells(out, length, keep);
        if (length + 3u < size) {
            memcpy(out + length, "…", 3u);
            length += 3u;
            out[length] = '\0';
        }
    }
}

// Point every rail row at the tree row it shows. One Text per VISIBLE row is what makes
// folding and scrolling allocation-free: the pool is the panel's height, so a row with
// nothing to show shows nothing.
static void sceneSyncRail(Scene *scene, size_t visible) {
    if (gRail == nullptr)
        return;
    // The viewport is what the panel can actually SHOW, not the size of the row pool: a
    // viewport as tall as the pool would let nothing scroll, because everything would
    // already fit.
    (void) TerminalTreeView_setViewportRows(gRail, visible);
    // `first` and `shown` are the TREE's own coordinates; `visible` above is how many rows
    // the panel has to give it. Two different questions, so two different names.
    size_t first = TerminalTreeView_scrollRow(gRail);
    size_t shown = TerminalTreeView_visibleCount(gRail);
    for (size_t index = 0; index < scene->railRowCount; ++index) {
        TerminalText *row = scene->railRows[index];
        if (row == nullptr)
            continue;
        if (first + index >= shown) {
            (void) TerminalText_setText(row, gEmptyLine, 0);
            (void) TerminalText_setBackgroundColor(row, TERMINAL_COLOR_DEFAULT);
            continue;
        }
        TerminalTreeRow record = TerminalTreeView_rowAt(gRail, first + index);
        railFormat(scene->railText[index], sizeof scene->railText[index], record,
                   scene->railWidth);
        (void) TerminalText_setText(row, scene->railText[index],
                                    strlen(scene->railText[index]));
        (void) TerminalText_setForegroundColor(row, record.folder ? COL_ACCENT : COL_TEXT);
        // The open file is the selection, and the tint is the one highlight in the shell.
        (void) TerminalText_setBackgroundColor(row, record.selected ? COL_SELECT
                                                                   : TERMINAL_COLOR_DEFAULT);
    }
}

static TerminalPanel *buildRail(Scene *scene, size_t rowCount) {
    TerminalPanel *rail = TerminalPanel();
    if (rail == nullptr)
        return nullptr;
    if (rowCount > TUI_MAX_ROWS)
        rowCount = TUI_MAX_ROWS;
    for (size_t i = 0; i < rowCount; ++i) {
        // Every row starts EMPTY and is pointed at a tree row by sceneSyncRail. The pool
        // is the panel's height, not the tree's size, which is what keeps a fold and a
        // scroll free of allocation: a row that has nothing to show shows nothing.
        TerminalText *row = plain(gEmptyLine, COL_TEXT);
        if (row == nullptr || !putText(rail, row, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_TOP, 1))) {
            TerminalPanel_free(rail);
            return nullptr;
        }
        scene->railRows[i] = row;
    }
    scene->railRowCount = rowCount;
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
    if (!putText(actions, plain("  ", COL_MUTED), TerminalAnchor_2(TERMINAL_ANCHOR_BAND_RIGHT, 2))
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
    // The gutter is two spaces, a right-aligned per-file number, and a period; the reserved
    // two-cell gap after it keeps code aligned without rendering a divider at rest.
    TerminalText *gap = plain("  ", COL_TEXT);
    if (!putPanel(editor, gutter,
                  TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT,
                                   scene->numberWidth + 2u + 1u))
        || !putText(editor, gap, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, TUI_CODE_PAD))
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
    scene->railWidth = 30;
    scene->numberWidth = GUTTER_MIN_NUMBER_WIDTH;
    for (size_t largest = file->lineCount; largest >= 10u; largest /= 10u)
        ++scene->numberWidth;
    TerminalPanel *title = buildChromeRow(scene);
    TerminalPanel *status = buildStatusBar(scene, file);
    TerminalPanel *rail = buildRail(scene, rowCount);
    TerminalPanel *centre = buildCentre(scene, rowCount);
    TerminalText *border = nullptr;   // borderless: the right edge is not a line
    scene->root = TerminalPanel();
    if (title == nullptr || status == nullptr || rail == nullptr || centre == nullptr
        || scene->root == nullptr) {
        TerminalPanel_free(scene->root);
        TerminalPanel_free(title);
        TerminalPanel_free(status);
        TerminalPanel_free(rail);
        TerminalPanel_free(centre);
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
    if (!putPanel(scene->root, centre, TerminalAnchor_0())) {
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
        // Match every number to the largest line number in this file. Two leading spaces
        // follow the invisible rail seam; the period is adjacent to the number.
        int written = snprintf(gGutter[i], sizeof gGutter[i], "  %*zu.",
                               (int) scene->numberWidth, line + 1);
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
    size_t oldWidth = scene->railWidth;
    if (!TerminalPanel_setAnchor(scene->root, scene->railId,
                                 TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, width)))
        return;
    size_t spacerWidth = width > scene->modeWidth ? width - scene->modeWidth : 0u;
    if (!TerminalPanel_setAnchor(scene->header, scene->tabSpacerId,
                                 TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, spacerWidth))) {
        (void) TerminalPanel_setAnchor(scene->root, scene->railId,
                                       TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, oldWidth));
        return;
    }
    scene->railWidth = width;
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

static int hexDigitValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// SEMICOLON_BG is written the way the rest of the ecosystem writes colours: #RRGGBB
// (opaque) or #RRGGBBAA. Anything else is refused rather than guessed at, because a
// silently mis-parsed background would fade every colour in the shell toward a value
// that was never asked for.
static bool parsePackedHex(const char *text, uint32_t *color) {
    if (text == nullptr)
        return false;
    if (*text == '#')
        ++text;
    else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text += 2;
    uint32_t value = 0u;
    size_t digits = 0;
    while (digits < 8u) {
        int digit = hexDigitValue(text[digits]);
        if (digit < 0)
            break;
        value = value * 16u + (uint32_t) digit;
        ++digits;
    }
    if ((digits != 6u && digits != 8u) || text[digits] != '\0')
        return false;
    *color = digits == 6u ? ((value << 8) | 0xFFu) : value;
    return true;
}

// The device answers the OSC 11 query with an X11 colour: `rgb:RRRR/GGGG/BBBB` (16 bits
// per channel) or the short `rgb:RR/GG/BB`. Both scale to the same high Byte, so only
// the first two hex digits of each component are read and the two forms need no special
// case. The reply is scanned rather than indexed because the framing around it varies
// (BEL or ST, with or without an ESC that ncurses has already consumed).
static bool parseDeviceBackground(const char *text, size_t length, uint32_t *color) {
    size_t at = 0;
    while (at + 4u <= length
           && !(text[at] == 'r' && text[at + 1] == 'g' && text[at + 2] == 'b' && text[at + 3] == ':'))
        ++at;
    if (at + 4u > length)
        return false;
    at += 4u;
    uint32_t channels[3] = { 0u, 0u, 0u };
    for (size_t channel = 0; channel < 3u; ++channel) {
        int high = -1;
        int low = -1;
        size_t digits = 0;
        while (at < length && digits < 4u) {
            int value = hexDigitValue(text[at]);
            if (value < 0)
                break;
            if (digits == 0u)
                high = value;
            else if (digits == 1u)
                low = value;
            ++digits;
            ++at;
        }
        if (digits < 2u)
            return false;
        channels[channel] = (uint32_t) (high * 16 + low);
        if (at < length && text[at] == '/')
            ++at;
    }
    *color = TERMINAL_RGB(channels[0], channels[1], channels[2]);
    return true;
}

// Ask the device for its background (OSC 11) and read the answer inside ONE bounded
// window. A terminal that does not answer - and plenty ignore the query outright - is
// not an error: it means the value stays whatever was already resolved. Nothing here is
// load-bearing for rendering, so nothing waits longer than TUI_PROBE_MS for it.
static bool queryDeviceBackground(uint32_t *color) {
    static const char request[] = "\x1b]11;?\x1b\\";
    if (write(STDOUT_FILENO, request, sizeof request - 1u) < 0)
        return false;
    char reply[TUI_PROBE_BYTES];
    size_t length = 0;
    timeout(TUI_PROBE_MS);
    int key = 0;
    while (length + 1u < sizeof reply && (key = getch()) != ERR)
        reply[length++] = (char) (key & 0xFF);
    reply[length] = '\0';
    return parseDeviceBackground(reply, length, color);
}

// Decide what is behind every transparent cell: what a fade fades TO. Stated beats
// guessed, so an explicit SEMICOLON_BG wins; otherwise the device is asked; otherwise
// the fallback stands. The caller owns the screen state (this reads bytes from it, so
// it must run with the terminal already in raw mode and before the event loop starts).
static void resolveDeviceBackground(void) {
    const char *configured = getenv("SEMICOLON_BG");
    if (configured != nullptr && *configured != '\0') {
        uint32_t declared = 0u;
        if (!parsePackedHex(configured, &declared)) {
            THROW("semicolon: SEMICOLON_BG '%s' is not #RRGGBB; using the fallback", configured);
        } else {
            gDeviceBg = declared;
            gDeviceBgSource = "SEMICOLON_BG";
            return;
        }
    }
    uint32_t answered = 0u;
    if (queryDeviceBackground(&answered)) {
        gDeviceBg = answered;
        gDeviceBgSource = "the terminal answered OSC 11";
        return;
    }
    gDeviceBgSource = "the built-in fallback (this terminal did not answer)";
}

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
        // Resolve the cell against the device before mapping it, because SGR sets ONE
        // colour and a fade has to be composed by US. The background plane goes over
        // the device background; the ink then goes over THAT.
        //
        // An unset background is left unset and the device paints its own, which keeps
        // a wrong backdrop guess a subtle tint instead of repainting the whole screen.
        // An unset foreground is left unset too: it means "the device's default ink",
        // and compositing it would paint the glyph in the backdrop colour and make the
        // text silently vanish - the difference between faded ink and invisible ink.
        uint32_t cellBg = TERMINAL_COLOR_IS_SET(bg)
                              ? TerminalColor_over(bg, gDeviceBg)
                              : TERMINAL_COLOR_DEFAULT;
        uint32_t backdrop = TERMINAL_COLOR_IS_SET(cellBg) ? cellBg : gDeviceBg;
        uint32_t cellFg = TERMINAL_COLOR_IS_SET(fg)
                              ? TerminalColor_over(fg, backdrop)
                              : TERMINAL_COLOR_DEFAULT;
        short pair = pairFor(cellFg, cellBg);
        // A run usually shares one pen, so set the attributes once and only touch
        // them again at a boundary: one wattr_set per cell is pure overhead.
        if (!haveAttrs || attrs != currentAttrs || pair != currentPair) {
            wattr_set(stdscr, attrs, pair, nullptr);
            currentAttrs = attrs;
            currentPair = pair;
            haveAttrs = true;
        }
        if (glyph > (uint32_t) WCHAR_MAX || (glyph >= 0xD800u && glyph <= 0xDFFFu))
            glyph = '?';
        wchar_t characters[CCHARW_MAX] = { (wchar_t) glyph, L'\0' };
        cchar_t cell;
        bool validCell = setcchar(&cell, characters, attrs, pair, nullptr) != ERR;
        if (!validCell) {
            characters[0] = L'?';
            validCell = setcchar(&cell, characters, attrs, pair, nullptr) != ERR;
        }
        // ncurses reports ERR after drawing the terminal's bottom-right cell because
        // advancing beyond that cell is impossible. The canvas run is still delivered.
        int result = validCell
                   ? mvwadd_wch(stdscr, (int) run.y, (int) (run.x + i), &cell)
                   : ERR;
        if (result == ERR && (glyph == 0x2304u || glyph == 0x203Au))
            (void) mvwaddch(stdscr, (int) run.y, (int) (run.x + i),
                            glyph == 0x2304u ? 'v' : '>');
    }
    return true;
}

// The chrome the toolkit has no widget for yet: a compact hover grip at the rail seam.
static void drawChrome(TerminalCanvas *canvas, size_t width, size_t height, size_t railWidth,
                       bool dividerHovered) {
    if (height < 5 || width < 4)
        return;
    if (!dividerHovered)
        return;
    size_t gripRows = height % 2u == 0u ? 4u : 3u;
    size_t firstRow = (height - gripRows) / 2u;
    TerminalPen pen = TerminalPen_1(TERMINAL_STYLE_BOLD | TERMINAL_STYLE_MUTED);
    // The seam stays invisible; hover reveals only a centered, parity-balanced handle.
    for (size_t row = 0; row < gripRows; ++row)
        TerminalCanvas_glyph(canvas, railWidth - 1u, firstRow + row, '|', pen);
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
// centre starts at the rail seam, the number field follows the file's largest line,
// and the period and two-cell code gap are reserved before code begins.
static size_t codeLeftColumn(size_t railWidth, size_t numberWidth) {
    return railWidth + numberWidth + 2u + 1u + TUI_CODE_PAD;
}

static size_t codeColumns(size_t railWidth, size_t numberWidth, int width) {
    if (width <= 0 || (size_t) width <= codeLeftColumn(railWidth, numberWidth))
        return 0;
    return (size_t) width - codeLeftColumn(railWidth, numberWidth);
}

static size_t editorVisibleRows(int height) {
    size_t rows = editorRowCount(height);
    return rows > TUI_MAX_ROWS ? TUI_MAX_ROWS : rows;
}

// One click, one decision: a folder opens or closes, a file opens in the editor. The row
// is resolved by the tree from the viewport offset, so the rail's scroll is already
// accounted for and a click past the last row is not a click on anything.
static void railClick(size_t y) {
    if (gRail == nullptr || y < editorFirstRow())
        return;
    TerminalTreeNodeId id = TerminalTreeView_nodeAtOffset(gRail, y - editorFirstRow());
    if (id == TERMINAL_TREE_NO_NODE)
        return;
    if (TerminalTreeView_isFolder(gRail, id)) {
        if (TerminalTreeView_isExpanded(gRail, id)) {
            (void) TerminalTreeView_setExpanded(gRail, id, false);
            return;
        }
        // Read BEFORE opening, so the rows exist by the time they become visible.
        (void) railReadChildren(id);
        (void) TerminalTreeView_setExpanded(gRail, id, true);
        return;
    }
    RailEntry *entry = railEntryFor(id);
    if (entry != nullptr)
        (void) openFile((*entry).path, id);
}

static void railScrollBy(long rows) {
    if (gRail != nullptr)
        (void) TerminalTreeView_scrollBy(gRail, rows);
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

// The frame transaction, mapped onto the terminal.
//
// DEC private mode 2026 ("synchronized output") asks the device to hold an update and
// apply it as ONE step, which is the terminal's counterpart of a compositor
// transaction: a divider drag or a resize repaints a great many cells, and without this
// the device is free to show the middle of that work. Terminals that do not know the
// mode ignore it, so this needs no probe and no capability gate.
static void frameBracket(void *frameContext, TerminalPresentPhase phase) {
    (void) frameContext;
    if (phase == TERMINAL_PRESENT_BEGIN) {
        (void) fputs("\033[?2026h", stdout);
        (void) fflush(stdout);
        return;
    }
    // The cells are still sitting in ncurses' buffer, so they must be flushed BEFORE
    // the transaction closes - otherwise the close arrives ahead of the content it is
    // supposed to bracket, and the bracket means nothing.
    (void) refresh();
    (void) fputs("\033[?2026l", stdout);
    (void) fflush(stdout);
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

// The tracking line: the bottom editor row is one long horizontal slider. A drag on
// it maps the pointer's position straight onto the offset, so dragging to either end
// reaches that end of the document rather than nudging by a step. Clicking there does
// nothing at all - press and drag are separable gestures, and only the drag pans.
static size_t applyScroll(size_t value, int delta, size_t maximum) {
    if (delta < 0) {
        size_t amount = (size_t) (-delta);
        return amount > value ? 0 : value - amount;
    }
    return clampScroll(value + (size_t) delta, maximum);
}

// A scroll with MOMENTUM, per axis: a position (`shown`), where it is heading
// (`target`), and the easing between them.
//
// A queue that is spent a fixed step at a time is a conveyor belt - it runs at constant
// speed until it empties, so there is no friction and nothing to stop. This instead
// covers a fraction of the remaining distance every frame, which decelerates the closer
// it gets, and `glideHalt` is what a press does: it drops the target onto the position
// and the motion is over at once.
//
// SLOT RECORD: ScrollGlide
typedef struct ScrollGlide {
    double rowShown;      // rows currently displayed, fractional
    double rowTarget;     // rows headed for
    double columnShown;
    double columnTarget;
} ScrollGlide;

static double clampDouble(double value, double minimum, double maximum) {
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

static double glideGap(double shown, double target) {
    double difference = target - shown;
    return difference < 0.0 ? -difference : difference;
}

// Still travelling? Drives the frame cadence: 60fps while true, a long sleep once false,
// so a settled view costs nothing at all.
static bool glideActive(const ScrollGlide *glide) {
    return glideGap((*glide).rowShown, (*glide).rowTarget) >= TUI_SCROLL_EPSILON
        || glideGap((*glide).columnShown, (*glide).columnTarget) >= TUI_SCROLL_EPSILON;
}

// Input only ever moves the TARGET. Clamping it here means the glide can never aim past
// a bound and grind against it.
static void glideImpulse(ScrollGlide *glide, long rows, long columns,
                         size_t maxTop, size_t maxLeft) {
    (*glide).rowTarget = clampDouble((*glide).rowTarget + (double) rows, 0.0, (double) maxTop);
    (*glide).columnTarget = clampDouble((*glide).columnTarget + (double) columns, 0.0, (double) maxLeft);
}

// Stop where we are: a press does this, which is why a running scroll can be caught.
static void glideHalt(ScrollGlide *glide) {
    (*glide).rowTarget = (*glide).rowShown;
    (*glide).columnTarget = (*glide).columnShown;
}

static void glideClamp(ScrollGlide *glide, size_t maxTop, size_t maxLeft) {
    (*glide).rowTarget = clampDouble((*glide).rowTarget, 0.0, (double) maxTop);
    (*glide).rowShown = clampDouble((*glide).rowShown, 0.0, (double) maxTop);
    (*glide).columnTarget = clampDouble((*glide).columnTarget, 0.0, (double) maxLeft);
    (*glide).columnShown = clampDouble((*glide).columnShown, 0.0, (double) maxLeft);
}

// Cover TUI_SCROLL_EASE of what is left, and snap once the rest is imperceptible.
static double glideStep(double *shown, double target) {
    if (glideGap(*shown, target) < TUI_SCROLL_EPSILON) {
        *shown = target;
        return target;
    }
    *shown += (target - *shown) * TUI_SCROLL_EASE;
    return *shown;
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
    // The dump renders the same scene, so it needs the same project: a rail that is empty
    // here would be a different frame from the one the shell shows.
    railBuild(gRailRoot);
    (void) railRevealPath(gOpenPath);
    sceneSyncRail(&scene, editorVisibleRows(height));
    if (scene.root == nullptr) {
        fprintf(stderr, "[vex] semicolon: scene construction failed\n");
        return 1;
    }
    sceneSyncRows(&scene, file, 0, 0, editorVisibleRows(height));
    TerminalCanvas canvas = TerminalCanvas(gCells, gStyles, gFg, gBg, (size_t) width, (size_t) height);
    TerminalCanvas_fill(&canvas, ' ', TerminalPen());
    TerminalPanel_paint(scene.root, &canvas);
    drawChrome(&canvas, (size_t) width, (size_t) height, scene.railWidth, false);
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
    if (setlocale(LC_CTYPE, "") == nullptr) {
        THROW("semicolon: cannot select the environment character locale");
        return 1;
    }
    SCREEN *screen = newterm(nullptr, stdout, stdin);
    if (screen == nullptr)
        return 1;
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
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
    mvaddstr(0, 0, "input probe: wheel, swipe sideways, drag, press keys. Ctrl-C quits.");
    mvaddstr(1, 0, "a horizontal swipe shows wheel-left / wheel-right if the terminal sends it.");
    refresh();
    TerminalInput input = TerminalInput_0();
    int line = 3;
    while (!gQuit) {
        int key = getch();
        if (key == ERR)
            continue;
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

// A diagnostic that answers one question with the terminal's own words: what colour is
// your background? Every fade in the shell is composited against that value, so when a
// fade looks wrong, this is the number to read first and SEMICOLON_BG is the fix.
;;DEBUG
static int runBackgroundProbe(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        THROW("semicolon --bg-probe requires a terminal");
        return 1;
    }
    SCREEN *screen = newterm(nullptr, stdout, stdin);
    if (screen == nullptr) {
        THROW("semicolon: terminal initialization failed");
        return 1;
    }
    (void) cbreak();
    (void) noecho();
    (void) notimeout(stdscr, false);
    set_escdelay(25);
    resolveDeviceBackground();
    endwin();
    delscreen(screen);
    printf("device background: #%02X%02X%02X\n",
           (unsigned) TERMINAL_COLOR_RED(gDeviceBg), (unsigned) TERMINAL_COLOR_GREEN(gDeviceBg),
           (unsigned) TERMINAL_COLOR_BLUE(gDeviceBg));
    printf("source: %s\n", gDeviceBgSource);
    printf("set SEMICOLON_BG=#RRGGBB to override it.\n");
    fflush(stdout);
    return 0;
}

static int runTerminal(FileText *file) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        THROW("semicolon requires terminal stdin and stdout (use --dump for a plain render)");
        return 1;
    }
    if (setlocale(LC_CTYPE, "") == nullptr) {
        THROW("semicolon: cannot select the environment character locale");
        return 1;
    }
    SCREEN *screen = newterm(nullptr, stdout, stdin);
    if (screen == nullptr) {
        THROW("semicolon: terminal initialization failed");
        return 1;
    }
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
    // Before the event loop owns the input stream: this reads the terminal's reply, so
    // it has to happen while nothing else is watching the same bytes.
    resolveDeviceBackground();
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
    gScene = &scene;
    gOpenFile = file;
    // The cached divider width, applied where the terminal's width is known so it can be
    // checked rather than trusted.
    if (gCachedRail >= TUI_RAIL_MIN && gCachedRail + TUI_RAIL_MARGIN < (size_t) width)
        sceneSetRailWidth(&scene, gCachedRail);
    // The project, one level at a time, then the open file revealed down it.
    railBuild(gRailRoot);
    TerminalTreeNodeId opening = railRevealPath(gOpenPath);
    if (opening != TERMINAL_TREE_NO_NODE) {
        gRailOpenNode = opening;
        (void) TerminalTreeView_setSelected(gRail, opening);
    }
    bool ok = scene.root != nullptr;
    Interaction interaction = { .input = TerminalInput_0() };
    TerminalSurface surface = TerminalSurface_0();
    ok = ok && TerminalSurface_setFrame(&surface, frameBracket, nullptr);
    ScrollGlide glide = {0};
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
        size_t codeLeft = codeLeftColumn(scene.railWidth, scene.numberWidth);
        size_t codeRight = width > 0 ? (size_t) width - 2u : 0;
        size_t maxTop = maxScrollTop(file, visible);
        size_t maxLeft = maxScrollLeft(file, codeColumns(scene.railWidth, scene.numberWidth, width));
        glideClamp(&glide, maxTop, maxLeft);
        if (visible == 0 || codeRight <= codeLeft) {
            erase();
            mvaddstr(0, 0, "terminal too small");
            refresh();
            timeout(TUI_POLL_MS);
            (void) getch();
            continue;
        }
        // All input arrives here: keys, and the mouse bytes the decoder turns into
        // normalized events.
        // One read may wait up to the poll timeout, so an idle shell sleeps
        // instead of spinning. Every later read in this frame is NON-BLOCKING:
        // with a timeout still set, the read that finds the buffer empty would
        // stall the paint by the whole timeout, which is exactly the lag a drag
        // exposes.
        // 60fps while a scroll is in flight, and a long sleep when nothing is moving.
        // Present-on-demand is preserved: an idle shell still emits nothing at all.
        timeout(glideActive(&glide) ? TUI_FRAME_MS : TUI_POLL_MS);
        int key = getch();
        timeout(0);
        size_t handled = 0;
        while (key != ERR && handled < TUI_EVENTS_PER_FRAME) {
            ++handled;
            // Ctrl-C (SIGINT) is the only way out; there is deliberately no letter
            // shortcut, because a keystroke is input and 'q' is a legitimate thing
            // to want to type.

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
                if (step.keyed)
                    glideImpulse(&glide, step.rows, step.columns, maxTop, maxLeft);
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
                        // A wheel over the RAIL scrolls the rail, not the editor: the
                        // pointer says which list the user means, so no mode is needed.
                        if (event.x < divider) {
                            railScrollBy(down ? 1 : -1);
                            break;
                        }
                        // ANY modified wheel pans sideways, because terminals differ
                        // over which modifier they let through: Shift is the one most
                        // often stolen for the terminal's own scrollback, while
                        // Alt/Option usually is not. Accepting all three means a
                        // trackpad reaches the columns on whichever modifier the
                        // emulator happens to forward - and a modifier is the user
                        // asking for something other than a plain scroll.
                        // A modified wheel drives the horizontal axis: terminals differ
                        // over which modifier they forward, so all three count.
                        long amount = down ? 1 : -1;
                        if (event.shift || event.meta || event.ctrl)
                            glideImpulse(&glide, 0, amount * (long) TUI_SCROLL_COLS, maxTop, maxLeft);
                        else
                            glideImpulse(&glide, amount * (long) TUI_SCROLL_ROWS, 0, maxTop, maxLeft);
                        break;
                    }
                    case TERMINAL_POINTER_WHEEL_LEFT:
                        glideImpulse(&glide, 0, -(long) TUI_SCROLL_COLS, maxTop, maxLeft);
                        break;
                    case TERMINAL_POINTER_WHEEL_RIGHT:
                        glideImpulse(&glide, 0, (long) TUI_SCROLL_COLS, maxTop, maxLeft);
                        break;
                    case TERMINAL_POINTER_PRESS:
                        interaction.pressed = true;
                        // CATCH IT: a press ends any scroll in flight, which is what makes
                        // momentum safe to have - you can always stop it by touching.
                        glideHalt(&glide);
                        // A press inside the rail is a file-browser click. Checked before
                        // the divider because the divider is its own edge: a rail row
                        // beside it must still open.
                        if (event.x < divider) {
                            railClick(event.y);
                            break;
                        }
                        // The tracking line is the bottom TWO rows: the last editor row
                        // and the frame's bottom border, so the whole floor of the
                        // window is draggable. It wins there; the divider is still
                        // grabbable on every row above.
                        if (event.y + 1u >= (size_t) height - 1u) {
                            interaction.tracking = true;
                            interaction.trackX = event.x;
                            interaction.trackLeft = (size_t) (glide.columnShown + 0.5);
                            break;
                        }
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
                        interaction.tracking = false;
                        interaction.pressed = false;
                        break;
                    case TERMINAL_POINTER_DRAG: {
                        if (interaction.tracking) {
                            // RELATIVE, by the pointer's own delta. Mapping the absolute
                            // pointer position onto the offset flung the view to
                            // wherever the press landed; a drag has to move the content
                            // by as much as the finger moved, so the point under the
                            // finger stays under the finger.
                            double wanted = (double) applyScroll(interaction.trackLeft,
                                                                 (int) event.x - (int) interaction.trackX,
                                                                 maxLeft);
                            glide.columnShown = wanted;
                            glide.columnTarget = wanted;
                            break;
                        }
                        if (interaction.grabbing) {
                            long moved = (long) event.x - (long) interaction.grabX;
                            long wanted = (long) interaction.grabWidth + moved;
                            if (wanted >= (long) TUI_RAIL_MIN
                                && wanted <= (long) columns - (long) TUI_RAIL_MARGIN) {
                                sceneSetRailWidth(&scene, (size_t) wanted);
                                // The code area moved with the rail, so its bounds did.
                                codeLeft = codeLeftColumn(scene.railWidth, scene.numberWidth);
                                maxLeft = maxScrollLeft(file, codeColumns(scene.railWidth,
                                                                         scene.numberWidth,
                                                                         columns));
                                glideClamp(&glide, maxTop, maxLeft);
                            }
                        }
                        // Edge autoscroll belongs to a DRAG, not to hovering: acting
                        // on plain motion made the view scroll whenever the pointer
                        // passed over the first or last editor row.
                        // Both bottom rows belong to the tracking line, so the vertical
                        // edge zone sits two rows above them.
                        if (event.y == editorFirstRow())
                            glideImpulse(&glide, -1, 0, maxTop, maxLeft);
                        else if (event.y == height - 3u)
                            glideImpulse(&glide, 1, 0, maxTop, maxLeft);
                        if (event.x <= codeLeft)
                            glideImpulse(&glide, 0, -1, maxTop, maxLeft);
                        else if (event.x >= codeRight)
                            glideImpulse(&glide, 0, 1, maxTop, maxLeft);
                        break;
                    }
                    case TERMINAL_POINTER_MOVE:
                        break;
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
        // Cover a fraction of what is left. The position is a whole number of cells, so
        // the fractional glide is rounded at the moment it is drawn.
        // A newly opened file starts at its top-left, and the glide is dropped with it:
        // momentum carried over from the previous file would scroll the new one away.
        if (gResetView) {
            glide.rowShown = 0.0;
            glide.rowTarget = 0.0;
            glide.columnShown = 0.0;
            glide.columnTarget = 0.0;
            gResetView = false;
        }
        size_t scrollTop = (size_t) (glideStep(&glide.rowShown, glide.rowTarget) + 0.5);
        size_t scrollLeft = (size_t) (glideStep(&glide.columnShown, glide.columnTarget) + 0.5);
        sceneSyncRows(&scene, file, scrollTop, scrollLeft, visible);
        sceneSyncRail(&scene, visible);
        TerminalPanel_paint(scene.root, &back);
        size_t divider = scene.railWidth - 1u;
        bool dividerHovered = interaction.havePointer
                           && interaction.pointerX + 1u >= divider
                           && interaction.pointerX <= divider + 1u;
        drawChrome(&back, (size_t) width, (size_t) height, scene.railWidth,
                   dividerHovered);
        TerminalPresentResult presented = TerminalSurface_present(&surface);
        if (!presented.ok)
            ok = false;
        // A changed frame was already flushed inside the transaction by the bracket.
        // A frame that changed nothing produced no transaction at all, so ncurses may
        // still be holding a resize's clear() - one flush here covers that case.
        if (ok && !presented.transactional)
            ok = refresh() != ERR;
        if (!ok)
            break;
    }
    (void) mouseReporting(false);
    // The session is written on the way out, so the next run in this project comes back
    // to the same file and the same divider.
    cacheSave();
    gScene = nullptr;
    gOpenFile = nullptr;
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
            puts("  no arguments  draw the IDE shell on this terminal (Ctrl-C quits)");
            puts("  --root PATH   browse this project (default: the current directory)");
            puts("  --file PATH   open PATH in the editor (default: this project's session,"
                 " else its first file)");
            puts("  --dump        print one frame as plain text instead of using a terminal");
            puts("  --input-probe print the mouse and key events this terminal actually sends");
            puts("  --bg-probe    print the background colour this terminal reports (fades use it)");
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
        if (strcmp(argv[i], "--bg-probe") == 0)
            return runBackgroundProbe();
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            (void) snprintf(gRailRoot, sizeof gRailRoot, "%s", argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            (void) snprintf(path, sizeof path, "%s", argv[++i]);
            fileGiven = true;
            continue;
        }
        THROW("semicolon: unknown argument");
        return 1;
    }
    // The project is where the shell was started, unless one was named. That is what
    // makes "cd anywhere && semicolon" mean the project you are standing in.
    if (gRailRoot[0] == '\0') {
        if (getcwd(gRailRoot, sizeof gRailRoot) == nullptr) {
            THROW("semicolon: cannot determine the current directory; pass --root");
            return 1;
        }
    }
    size_t rootLength = strlen(gRailRoot);
    while (rootLength > 1u && gRailRoot[rootLength - 1u] == '/')
        gRailRoot[--rootLength] = '\0';
    cacheLoad(gRailRoot);
    if (!fileGiven) {
        // The project's own session first, then its first file. A cached path that is no
        // longer readable is simply not used, so a deleted file is not a startup failure.
        if (gCachedOpen[0] != '\0' && access(gCachedOpen, R_OK) == 0)
            (void) snprintf(path, sizeof path, "%s", gCachedOpen);
        else if (!firstFileIn(gRailRoot, path, sizeof path)) {
            THROW("semicolon: no file to open in %s; pass --file", gRailRoot);
            return 1;
        }
    }
    (void) snprintf(gOpenPath, sizeof gOpenPath, "%s", path);
    FileText file;
    if (!fileLoad(&file, path)) {
        fprintf(stderr, "[vex] semicolon: cannot read %s\n", path);
        return 1;
    }
    // The active tab names the file that is on screen. Set here, once, so the dumped frame
    // and the interactive shell cannot disagree about which file they are showing.
    (void) snprintf(gActiveTab, sizeof gActiveTab, "%s  x", file.name);
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
