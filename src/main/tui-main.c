#include "terminal/terminal_panel.h"
#include "terminal/terminal_text.h"
#include "terminal/terminal_anchor.h"
#include "terminal/terminal_canvas.h"
#include "terminal/terminal_color.h"
#include "terminal/terminal_style.h"
#include "terminal/terminal_surface.h"
#include "terminal/terminal_image.h"
#include "exception/throw.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/test.h"
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>

;;DEFINITION
/** The semicolon IDE shell: the design from src/tui/tui_example.txt, drawn on
 * Determinant's component tree and put on a real terminal.
 *
 * This file is the HOST, not the toolkit. Determinant owns cells, placement,
 * panels and text and knows nothing about a tty; acquiring the terminal, owning
 * the alternate screen, decoding keys and honouring a resize are this program's
 * job, exactly as the toolkit's lawbook requires. That is why the scene is built
 * here and ncurses is named only here.
 *
 * The rendering chain is the whole point of the toolkit, end to end: the tree is
 * painted into a back canvas, TerminalSurface diffs that against the record of
 * what the terminal was last told, and only the changed same-row runs are handed
 * to the ncurses sink. A frame that changes nothing sends nothing.
 *
 * The design itself is a fixed mock-up, not a working editor: the file tree is a
 * literal list, the tab strip is static, and the code area shows one sample line.
 * Nothing is read from disk and no file is edited. */
;;OVERVIEW
/** ============================================================================
 * MODULE: semicolon terminal entry (src/main/tui-main.c)
 * ============================================================================
 * OWNED STATE (file-local):
 *   gQuit          signal-safe shutdown request only.
 *   gCells/gStyles/gFg/gBg        static canvas planes, MAXW x MAXH.
 *   gFrontCells/...              static last-paint mirror for the surface diff.
 *   gRule          static dash run used for the rule textures.
 *   gPairColors[]  registered ncurses colour pairs (fg,bg) -> pair index.
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Private helpers:
 *   static void onSignal(int)            flag a shutdown request; no terminal call.
 *   static void initRule(void)           fill gRule with dashes.
 *   static TerminalText *chip(const char *)            bracketed [label] text.
 *   static TerminalText *plain(const char *, uint8_t)  styled text.
 *   static bool putText(parent, text, anchor)          adopt a text or free it.
 *   static bool putPanel(parent, child, anchor)        adopt a panel or free it.
 *   static TerminalPanel *buildTitleBar(void)          the chip bar in row 0.
 *   static TerminalPanel *buildStatusBar(void)         the chip bar in the last row.
 *   static TerminalPanel *buildRail(void)              the file tree column.
 *   static TerminalPanel *buildCentre(void)            tabs + separator + editor.
 *   static TerminalPanel *buildScene(void)             the whole tree.
 *   static short colorIndex(uint32_t)                  RGB -> ncurses colour.
 *   static short pairFor(uint32_t, uint32_t)           registered (fg,bg) pair.
 *   static attr_t attrsFor(uint8_t)                    cell style -> ncurses attrs.
 *   static bool blitRun(void *, TerminalRun)           the run sink.
 *   static int dumpFrame(int, int)                     one frame as plain text.
 *   static int runTerminal(void)                       the interactive loop.
 * Entry:
 *   int main(int, char **)   --help / --dump [WxH] / interactive.
 * Contracts:
 *   The interactive path REQUIRES a tty on stdin and stdout and refuses before
 *   ncurses otherwise. Ctrl-Q or q quits; KEY_RESIZE re-lays the whole tree
 *   because every rectangle is recomputed from the canvas size each paint.
 *   The terminal is restored on every normal exit and on SIGINT/SIGTERM; SIGKILL
 *   or a crash cannot promise restoration. No file, network or provider call.
 * ============================================================================ */

#define TUI_MAX_W 400
#define TUI_MAX_H 160
#define TUI_DEFAULT_W 103
#define TUI_DEFAULT_H 23

#define COL_TEXT  TERMINAL_RGB(0xE6, 0xE6, 0xE6)
#define COL_MUTED TERMINAL_RGB(0x88, 0x88, 0x88)
#define COL_ACCENT TERMINAL_RGB(0x7A, 0xC8, 0xFF)
#define COL_ERROR TERMINAL_RGB(0xFF, 0x6B, 0x6B)
#define COL_BG    TERMINAL_RGB(0x11, 0x11, 0x18)
#define COL_RAIL  TERMINAL_RGB(0x18, 0x18, 0x22)

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

static void onSignal(int signalNumber) {
    (void) signalNumber;
    gQuit = 1;
}

static void initRule(void) {
    memset(gRule, '-', sizeof gRule - 1);
    gRule[sizeof gRule - 1] = '\0';
}

// A bracketed chip: the whole reason text is an object is so this can be hovered
// and recoloured later without the bar knowing anything about it.
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

// Adopt a child, or free it when the parent refused it. Keeps the scene builder
// free of ownership bookkeeping at every call site.
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

// --[;]-[vexgraph (project name)]-[<][>]------------------[> run]--[settings]---
static TerminalPanel *buildTitleBar(void) {
    TerminalPanel *bar = TerminalPanel();
    if (bar == nullptr)
        return nullptr;
    // The rule texture paints first and reserves nothing, so every chip lands on
    // top of it and the gaps read as the run between chips.
    if (!putText(bar, plain(gRule, COL_MUTED), TerminalAnchor_0())) {
        TerminalPanel_free(bar);
        return nullptr;
    }
    static const char *labels[] = { ";", "vexgraph (project name)", "<", ">" };
    static const size_t extents[] = { 3, 25, 3, 3 };
    // A two-dash margin, then the chips one dash apart, exactly as sketched.
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
        if (i < 2)   // [;]-[name]-[<][>]: the two arrows sit together
            (void) putText(bar, plain(gRule, COL_MUTED),
                           TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, 1));
    }
    // From the right edge: a three-dash margin, [settings], two dashes, [> run].
    static const size_t rightExtents[] = { 3, 10, 2, 7 };
    static const char *rightLabels[] = { nullptr, "settings", nullptr, "> run" };
    for (size_t i = 0; i < sizeof rightExtents / sizeof rightExtents[0]; ++i) {
        TerminalText *child = rightLabels[i] != nullptr ? chip(rightLabels[i]) : plain(gRule, COL_MUTED);
        if (!putText(bar, child, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_RIGHT, rightExtents[i]))) {
            TerminalPanel_free(bar);
            return nullptr;
        }
    }
    (void) TerminalPanel_setForegroundColor(bar, COL_TEXT);
    (void) TerminalPanel_setBackgroundColor(bar, COL_BG);
    return bar;
}

// --[vexgraph > src > main > tui-main.c]----[c23/java]-[ln:1]-[utf-8]--
static TerminalPanel *buildStatusBar(void) {
    TerminalPanel *bar = TerminalPanel();
    if (bar == nullptr)
        return nullptr;
    if (!putText(bar, plain(gRule, COL_MUTED), TerminalAnchor_0())) {
        TerminalPanel_free(bar);
        return nullptr;
    }
    if (!putText(bar, plain(gRule, COL_MUTED), TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, 2))) {
        TerminalPanel_free(bar);
        return nullptr;
    }
    TerminalText *breadcrumb = chip("vexgraph > src > main > tui-main.c");
    size_t breadcrumbExtent = TerminalText_measure(breadcrumb);
    (void) TerminalText_setForegroundColor(breadcrumb, COL_TEXT);
    if (!putText(bar, breadcrumb, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, breadcrumbExtent))) {
        TerminalPanel_free(bar);
        return nullptr;
    }
    // From the right edge: two dashes, then [utf-8] - [ln:1] - [c23/java].
    static const size_t rightExtents[] = { 2, 7, 1, 6, 1, 6, 1, 10 };
    static const char *rightLabels[] = { nullptr, "utf-8", nullptr, "time", nullptr, "ln:1", nullptr, "c23/java" };
    for (size_t i = 0; i < sizeof rightExtents / sizeof rightExtents[0]; ++i) {
        TerminalText *child = rightLabels[i] != nullptr ? chip(rightLabels[i]) : plain(gRule, COL_MUTED);
        (void) TerminalText_setForegroundColor(child, COL_MUTED);
        if (!putText(bar, child, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_RIGHT, rightExtents[i]))) {
            TerminalPanel_free(bar);
            return nullptr;
        }
    }
    (void) TerminalPanel_setForegroundColor(bar, COL_TEXT);
    (void) TerminalPanel_setBackgroundColor(bar, COL_BG);
    return bar;
}

// The file tree: a literal list, one row per line, with the type glyphs the
// design uses ({} json, <> markup, Aa text, [] folder, C/C+/oC sources).
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
    (void) TerminalPanel_setBackgroundColor(rail, COL_BG);
    return rail;
}

// The editor: a line-number gutter, then the code column whose first row also
// carries a right-aligned diagnostics chip.
static TerminalPanel *buildEditor(void) {
    TerminalPanel *editor = TerminalPanel();
    if (editor == nullptr)
        return nullptr;
    TerminalPanel *gutter = TerminalPanel();
    if (gutter == nullptr) {
        TerminalPanel_free(editor);
        return nullptr;
    }
    static const char *numbers[] = { "    1 |", "      |", "      |", "      |", "      |"
                                   , "      |", "      |", "      |" };
    for (size_t i = 0; i < sizeof numbers / sizeof numbers[0]; ++i) {
        TerminalText *number = plain(numbers[i], COL_MUTED);
        if (!putText(gutter, number, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_TOP, 1))) {
            TerminalPanel_free(gutter);
            TerminalPanel_free(editor);
            return nullptr;
        }
    }
    (void) TerminalPanel_setBackgroundColor(gutter, COL_RAIL);
    if (!putPanel(editor, gutter, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, 7))) {
        TerminalPanel_free(editor);
        return nullptr;
    }
    TerminalPanel *code = TerminalPanel();
    if (code == nullptr) {
        TerminalPanel_free(editor);
        return nullptr;
    }
    // Row 0 stacks the code text and the diagnostics chip on the SAME row, which
    // is what LAYERED is for: one child aligns left, the other right.
    TerminalPanel *firstRow = TerminalPanel();
    if (firstRow == nullptr) {
        TerminalPanel_free(code);
        TerminalPanel_free(editor);
        return nullptr;
    }
    (void) TerminalPanel_setLayout(firstRow, TERMINAL_PANEL_LAYERED);
    TerminalText *codeText = plain("  Hello *h = Hello();", COL_TEXT);
    TerminalText *diagnostics = chip("no errors!");
    (void) TerminalText_setAlign(diagnostics, TERMINAL_TEXT_ALIGN_RIGHT);
    (void) TerminalText_setForegroundColor(diagnostics, COL_MUTED);
    if (!putText(firstRow, codeText, TerminalAnchor_0())
        || !putText(firstRow, diagnostics, TerminalAnchor_0())) {
        TerminalPanel_free(firstRow);
        TerminalPanel_free(code);
        TerminalPanel_free(editor);
        return nullptr;
    }
    if (!putPanel(code, firstRow, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_TOP, 1))) {
        TerminalPanel_free(code);
        TerminalPanel_free(editor);
        return nullptr;
    }
    if (!putPanel(editor, code, TerminalAnchor_0())) {
        TerminalPanel_free(editor);
        return nullptr;
    }
    (void) TerminalPanel_setForegroundColor(editor, COL_TEXT);
    return editor;
}

// [helloWorld.c  x] [placehodler.txt  x] [ide settings  x] over a rule, then the
// separator row, then the editor.
static TerminalPanel *buildCentre(void) {
    TerminalPanel *centre = TerminalPanel();
    if (centre == nullptr)
        return nullptr;
    TerminalPanel *tabs = TerminalPanel();
    if (tabs == nullptr) {
        TerminalPanel_free(centre);
        return nullptr;
    }
    static const char *tabLabels[] = { "helloWorld.c  x", "placehodler.txt  x", "ide settings  x" };
    if (!putText(tabs, plain(" ", COL_TEXT), TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, 1))) {
        TerminalPanel_free(tabs);
        TerminalPanel_free(centre);
        return nullptr;
    }
    for (size_t i = 0; i < sizeof tabLabels / sizeof tabLabels[0]; ++i) {
        TerminalText *tab = chip(tabLabels[i]);
        (void) TerminalText_setForegroundColor(tab, i == 0 ? COL_TEXT : COL_MUTED);
        if (!putText(tabs, tab, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, TerminalText_measure(tab) + 1))) {
            TerminalPanel_free(tabs);
            TerminalPanel_free(centre);
            return nullptr;
        }
    }
    if (!putPanel(centre, tabs, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_TOP, 1))) {
        TerminalPanel_free(centre);
        return nullptr;
    }
    TerminalPanel *separator = TerminalPanel();
    if (separator == nullptr) {
        TerminalPanel_free(centre);
        return nullptr;
    }
    if (!putText(separator, plain(gRule, COL_MUTED), TerminalAnchor_0())) {
        TerminalPanel_free(separator);
        TerminalPanel_free(centre);
        return nullptr;
    }
    if (!putPanel(centre, separator, TerminalAnchor_2(TERMINAL_ANCHOR_BAND_TOP, 1))) {
        TerminalPanel_free(centre);
        return nullptr;
    }
    if (!putPanel(centre, buildEditor(), TerminalAnchor_0())) {
        TerminalPanel_free(centre);
        return nullptr;
    }
    return centre;
}

static TerminalPanel *buildScene(void) {
    TerminalPanel *root = TerminalPanel();
    if (root == nullptr)
        return nullptr;
    if (!putPanel(root, buildTitleBar(), TerminalAnchor_2(TERMINAL_ANCHOR_BAND_TOP, 1))) {
        TerminalPanel_free(root);
        return nullptr;
    }
    if (!putPanel(root, buildStatusBar(), TerminalAnchor_2(TERMINAL_ANCHOR_BAND_BOTTOM, 1))) {
        TerminalPanel_free(root);
        return nullptr;
    }
    if (!putPanel(root, buildRail(), TerminalAnchor_2(TERMINAL_ANCHOR_BAND_LEFT, 29))) {
        TerminalPanel_free(root);
        return nullptr;
    }
    // A one-column reservation for the panel's right edge, so the chrome drawn
    // over the tree never overwrites the last cell of real content.
    if (!putText(root, plain(gRule, COL_MUTED), TerminalAnchor_2(TERMINAL_ANCHOR_BAND_RIGHT, 1))) {
        TerminalPanel_free(root);
        return nullptr;
    }
    if (!putPanel(root, buildCentre(), TerminalAnchor_0())) {
        TerminalPanel_free(root);
        return nullptr;
    }
    (void) TerminalPanel_setBackgroundColor(root, COL_BG);
    return root;
}

// --- The ncurses side: this host owns the tty, Determinant never names it ---

#define TUI_PAIR_MAX 96
typedef struct TermPair {
    uint32_t fg;
    uint32_t bg;
    short pair;
} TermPair;
static TermPair gPairs[TUI_PAIR_MAX];
static size_t gPairCount;

// Map a packed 0xRRGGBBAA colour onto the terminal's own palette. TERMINAL_COLOR
// _DEFAULT (unset) becomes -1, the terminal's default colour.
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
        return (r > b) ? COLOR_RED : COLOR_BLUE;
    if (luminance < 200)
        return (r > 200 && g > 200) ? COLOR_CYAN : COLOR_WHITE;
    return COLOR_WHITE;
}

// One ncurses colour pair per (fg,bg) the application actually uses, registered
// on first sight and reused after that.
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

// The run sink: Determinant hands over the cells that changed since the last
// paint, and this writes them. A 0 glyph is a wide glyph's continuation cell and
// is skipped because the wide glyph already occupied it.
static bool blitRun(void *context, TerminalRun run) {
    (void) context;
    for (size_t i = 0; i < run.length; ++i) {
        uint32_t glyph = run.cells != nullptr ? run.cells[i] : ' ';
        if (glyph == 0)
            continue;
        uint8_t style = run.styles != nullptr ? run.styles[i] : 0;
        uint32_t fg = run.fg != nullptr ? run.fg[i] : TERMINAL_COLOR_DEFAULT;
        uint32_t bg = run.bg != nullptr ? run.bg[i] : TERMINAL_COLOR_DEFAULT;
        wattr_set(stdscr, attrsFor(style), pairFor(fg, bg), nullptr);
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

// The window chrome the toolkit has no widget for yet: the rail's two verticals
// and the panel's right edge, drawn with the canvas primitive over the painted
// tree. A real vertical rule/segment widget is the next toolkit item; until it
// lands the host owns these three lines.
static void drawChrome(TerminalCanvas *canvas, size_t width, size_t height) {
    if (height < 3)
        return;
    TerminalPen pen = TerminalPen_1(TERMINAL_STYLE_MUTED);
    size_t rows = height - 2;
    TerminalCanvas left = TerminalCanvas_sub(canvas, 0, 1, 1, rows);
    TerminalCanvas_vline(&left, 0, '|', pen);
    TerminalCanvas divider = TerminalCanvas_sub(canvas, 28, 1, 1, rows);
    TerminalCanvas_vline(&divider, 0, '|', pen);
    TerminalCanvas right = TerminalCanvas_sub(canvas, width - 1, 1, 1, rows);
    TerminalCanvas_vline(&right, 0, '|', pen);
}

// One frame as plain text, with no tty and no ncurses. This is the test/inspection
// affordance: it proves the layout and content of the scene deterministically.
;;TEST
static int dumpFrame(int width, int height) {
    if (width < 1 || height < 1 || width > TUI_MAX_W || height > TUI_MAX_H) {
        fprintf(stderr, "[vex] semicolon: dump size %dx%d out of range (max %dx%d)\n",
                width, height, TUI_MAX_W, TUI_MAX_H);
        return 1;
    }
    TerminalPanel *root = buildScene();
    if (root == nullptr) {
        fprintf(stderr, "[vex] semicolon: scene construction failed\n");
        return 1;
    }
    TerminalCanvas canvas = TerminalCanvas(gCells, gStyles, gFg, gBg, (size_t) width, (size_t) height);
    TerminalCanvas_fill(&canvas, ' ', TerminalPen());
    TerminalPanel_paint(root, &canvas);
    drawChrome(&canvas, (size_t) width, (size_t) height);
    for (int y = 0; y < height; ++y) {
        TerminalCanvasTextRow row = TerminalCanvas_copyRow(&canvas, (size_t) y);
        fwrite(row.data, 1, row.bytes, stdout);
        fputc('\n', stdout);
    }
    TerminalPanel_free(root);
    return 0;
}

static int runTerminal(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        THROW("semicolon requires terminal stdin and stdout (use --dump for a plain render)");
        return 1;
    }
    TerminalPanel *root = buildScene();
    if (root == nullptr) {
        THROW("semicolon: scene construction failed");
        return 1;
    }
    SCREEN *screen = newterm(nullptr, stdout, stdin);
    if (screen == nullptr) {
        TerminalPanel_free(root);
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
    (void) keypad(stdscr, true);
    (void) curs_set(0);
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    TerminalSurface surface = TerminalSurface_0();
    int width = 0;
    int height = 0;
    bool ok = true;
    while (ok && !gQuit) {
        getmaxyx(stdscr, height, width);
        if (width > TUI_MAX_W)
            width = TUI_MAX_W;
        if (height > TUI_MAX_H)
            height = TUI_MAX_H;
        if (width < 20 || height < 5) {
            erase();
            mvaddstr(0, 0, "terminal too small");
            refresh();
            int key = getch();
            if (key == 'q' || key == 17)
                break;
            continue;
        }
        TerminalCanvas back = TerminalCanvas(gCells, gStyles, gFg, gBg, (size_t) width, (size_t) height);
        TerminalCanvas front = TerminalCanvas(gFrontCells, gFrontStyles, gFrontFg, gFrontBg,
                                             (size_t) width, (size_t) height);
        TerminalCanvas_fill(&back, ' ', TerminalPen());
        if (!TerminalSurface_isBound(&surface)
            || TerminalSurface_getBack(&surface).gridWidth != (size_t) width
            || TerminalSurface_getBack(&surface).gridHeight != (size_t) height) {
            // A resize invalidates the record of the old frame: start the mirror
            // blank so the whole new screen is sent once.
            TerminalCanvas_fill(&front, ' ', TerminalPen());
            if (!TerminalSurface_bind(&surface, &back, &front, blitRun, nullptr)) {
                ok = false;
                break;
            }
        }
        TerminalPanel_paint(root, &back);
        drawChrome(&back, (size_t) width, (size_t) height);
        TerminalPresentResult presented = TerminalSurface_present(&surface);
        if (!presented.ok)
            ok = false;
        if (ok)
            ok = refresh() != ERR;
        if (!ok)
            break;
        int key = getch();
        if (key == 17 || key == 'q')
            break;
        if (key == KEY_RESIZE) {
            (void) clear();
            continue;
        }
    }
    (void) curs_set(1);
    (void) endwin();
    (void) delscreen(screen);
    TerminalPanel_free(root);
    if (!ok)
        THROW("semicolon: terminal operation failed; terminal restored");
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    initRule();
    bool dump = false;
    int width = TUI_DEFAULT_W;
    int height = TUI_DEFAULT_H;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            puts("semicolon [--dump [WxH]] [--help]");
            puts("  no arguments  draw the IDE shell on this terminal (Ctrl-Q or q quits)");
            puts("  --dump        print one frame as plain text instead of using a terminal");
            return 0;
        }
        if (strcmp(argv[i], "--dump") == 0) {
            dump = true;
            if (i + 1 < argc && strchr(argv[i + 1], 'x') != nullptr) {
                char *end = nullptr;
                long parsedWidth = strtol(argv[++i], &end, 10);
                if (end == nullptr || *end != 'x') {
                    THROW("semicolon: --dump size must look like 103x23");
                    return 1;
                }
                char *heightEnd = nullptr;
                long parsedHeight = strtol(end + 1, &heightEnd, 10);
                if (heightEnd == nullptr || *heightEnd != '\0'
                    || parsedWidth < 1 || parsedHeight < 1) {
                    THROW("semicolon: --dump size must look like 103x23");
                    return 1;
                }
                width = (int) parsedWidth;
                height = (int) parsedHeight;
            }
            continue;
        }
        THROW("semicolon: unknown argument");
        return 1;
    }
    if (dump)
        return dumpFrame(width, height);
    return runTerminal();
}
