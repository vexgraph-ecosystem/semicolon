#include "editor/semicolon_syntax.h"
#include "adapters/c/c23_lexer.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "exception/throw.h"
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/** SemicolonSyntax is the R6 consumer bridge between Language's C23 lexer and
 * Determinant pens. It copies editor rows into one immutable normalized snapshot
 * so multiline comments are classified as a document, never independent lines.
 * Only cold document replacement lexes/allocates. Hot byte projections binary
 * search flat ordered spans; scroll and caret movement need no new analysis.
 * Palette choices belong here, not inside either R3 owner. This is lexical
 * coloring, not parsing, diagnostics, symbol resolution or inlay generation. */

;;OVERVIEW
/** ============================================================================
 * CLASS: SemicolonSyntax (editor/semicolon_syntax.c)
 * STRUCT FIELDS (Mirroring this opaque class)
 *   char *source                  owned immutable normalized source
 *   size_t length                 source bytes, excluding NUL
 *   size_t *starts                owned line-start offsets plus end sentinel
 *   size_t lineCount              initialized row count
 *   SyntaxSpan *spans             owned ordered Language span values
 *   size_t spanCount              initialized span count
 *   TerminalPen pens[SYNTAX_ROLE_IDENTIFIER + 1u] current role palette
 *   size_t refusals               storage refusals in this owner's lifetime
 * FUNCTION REGISTRY
 *   SemicolonSyntax *SemicolonSyntax_0(void), SemicolonSyntax(), zero()
 *   void SemicolonSyntax_free(SemicolonSyntax *self)
 *   bool SemicolonSyntax_setLines(self,const char *const *lines,
 *                                const size_t *lengths,size_t count)
 *   bool SemicolonSyntax_acceptsPath(const char *path)
 *   bool SemicolonSyntax_setPen(self,SyntaxRole role,TerminalPen pen)
 *   TerminalPen SemicolonSyntax_getPen(const self,SyntaxRole role)
 *   TerminalPen SemicolonSyntax_penAt(const self,size_t line,size_t byte)
 *   size_t SemicolonSyntax_getLineCount/getSpanCount/exhaustionCount(const self)
 *   TerminalProjection SemicolonSyntax_toString/toStringStruct(const self)
 * PRIVATE FUNCTIONS
 *   refuse(self,request)           report first storage refusal; count every one
 * CONTRACT
 *   External serialization; no borrowed input survives setLines. Configuration
 *   and failed replacement preserve all previously admitted bytes and colors.
 *   Counts/owned geometry change only through admission/lifecycle, not setters
 *   that forge initialized storage (Conflict Triage Law managed accessor seam).
 *   No public source projection: terminal controls remain the host's sanitized
 *   glyphs. INVALID and unclassified bytes use ordinary text, not error styling.
 *   Capacity is growable with checked arithmetic; refusal count resets on new
 *   construction, not successful replacement. Null queries return safe defaults.
 * ============================================================================ */

struct SemicolonSyntax {
    char *source;
    size_t length;
    size_t *starts;
    size_t lineCount;
    SyntaxSpan *spans;
    size_t spanCount;
    TerminalPen pens[SYNTAX_ROLE_IDENTIFIER + 1u];
    size_t refusals;
};

// Defaults initialize configurable pens, never override a consumer's current pen.
#define SYNTAX_TEXT_DEFAULT TERMINAL_RGB(0xF0, 0xF0, 0xF0)
#define SYNTAX_KEYWORD_DEFAULT TERMINAL_RGB(0x00, 0x80, 0x00)
#define SYNTAX_LITERAL_DEFAULT TERMINAL_RGB(0xA0, 0xF5, 0xA0)
#define SYNTAX_COMMENT_DEFAULT TERMINAL_RGB(0x60, 0x85, 0x6B)
#define SYNTAX_SPAN_INITIAL_CAPACITY 64u

static bool refuse(SemicolonSyntax *self, size_t request) {
    if ((*self).refusals == 0u)
        THROW("SemicolonSyntax storage refused: request %zu bytes; allocation/extent bound reached", request);
    if ((*self).refusals != SIZE_MAX)
        ++(*self).refusals;
    return false;
}

SemicolonSyntax *SemicolonSyntax_0(void) {
    SemicolonSyntax *self = calloc(1u, sizeof *self);
    if (self == nullptr) {
        THROW("SemicolonSyntax owner allocation refused: request %zu bytes", sizeof *self);
        return nullptr;
    }
    // Screenshot-inspired defaults, not exact theme extraction. Every role pen
    // is replaceable through setPen; the terminal may quantize these RGB values.
    for (size_t role = 0u; role <= SYNTAX_ROLE_IDENTIFIER; ++role)
        (*self).pens[role] = TerminalPen(TERMINAL_STYLE_NONE, SYNTAX_TEXT_DEFAULT);
    (*self).pens[SYNTAX_ROLE_KEYWORD] = TerminalPen(0u, SYNTAX_KEYWORD_DEFAULT);
    (*self).pens[SYNTAX_ROLE_STRING] = TerminalPen(0u, SYNTAX_LITERAL_DEFAULT);
    (*self).pens[SYNTAX_ROLE_CHARACTER] = (*self).pens[SYNTAX_ROLE_STRING];
    (*self).pens[SYNTAX_ROLE_NUMBER] = (*self).pens[SYNTAX_ROLE_STRING];
    (*self).pens[SYNTAX_ROLE_COMMENT] = TerminalPen(0u, SYNTAX_COMMENT_DEFAULT);
    (*self).pens[SYNTAX_ROLE_DOC_COMMENT] = (*self).pens[SYNTAX_ROLE_COMMENT];
    (*self).pens[SYNTAX_ROLE_BRACKET].style = TERMINAL_STYLE_BOLD;
    return self;
}

void SemicolonSyntax_free(SemicolonSyntax *self) {
    if (self == nullptr)
        return;
    free((*self).source);
    free((*self).starts);
    free((*self).spans);
    free(self);
}

bool SemicolonSyntax_acceptsPath(const char *path) {
    if (path == nullptr)
        return false;
    const char *extension = strrchr(path, '.');
    return extension != nullptr
        && (strcmp(extension, ".c") == 0 || strcmp(extension, ".h") == 0);
}

bool SemicolonSyntax_setLines(SemicolonSyntax *self, const char *const *lines,
                              const size_t *lengths, size_t count) {
    if (self == nullptr || (count != 0u && (lines == nullptr || lengths == nullptr))) {
        THROW("SemicolonSyntax rejected missing editor rows");
        return false;
    }
    if (count >= SIZE_MAX / sizeof(size_t))
        return refuse(self, SIZE_MAX);
    size_t length = 0u;
    for (size_t line = 0u; line < count; ++line) {
        if (lines[line] == nullptr && lengths[line] != 0u) {
            THROW("SemicolonSyntax rejected missing row bytes");
            return false;
        }
        if (lengths[line] >= SIZE_MAX - length)
            return refuse(self, SIZE_MAX);
        length += lengths[line] + 1u;
    }
    if (length == SIZE_MAX)
        return refuse(self, SIZE_MAX);
    char *source = malloc(length + 1u);
    size_t *starts = malloc((count + 1u) * sizeof *starts);
    if (source == nullptr || starts == nullptr) {
        free(source);
        free(starts);
        return refuse(self, length + 1u);
    }
    size_t at = 0u;
    for (size_t line = 0u; line < count; ++line) {
        starts[line] = at;
        if (lengths[line] != 0u)
            memcpy(source + at, lines[line], lengths[line]);
        at += lengths[line];
        source[at++] = '\n';
    }
    starts[count] = at;
    source[at] = '\0';
    C23Lexer *lexer = C23Lexer(1u, source, length);
    SyntaxSpan *spans = nullptr;
    size_t used = 0u;
    size_t capacity = 0u;
    if (lexer == nullptr) {
        free(source);
        free(starts);
        if ((*self).refusals != SIZE_MAX)
            ++(*self).refusals;
        // The Language owner already diagnosed its failed construction.
        return false;
    }
    bool success = true;
    {
        C23LexStep step = C23Lexer_next(lexer);
        while (step.hasSpan) {
            if (used == capacity) {
                size_t next = capacity == 0u ? SYNTAX_SPAN_INITIAL_CAPACITY : capacity * 2u;
                if (next < capacity || next > SIZE_MAX / sizeof *spans) {
                    success = false;
                    break;
                }
                SyntaxSpan *grown = realloc(spans, next * sizeof *spans);
                if (grown == nullptr) {
                    success = false;
                    break;
                }
                spans = grown;
                capacity = next;
            }
            spans[used++] = step.span;
            step = C23Lexer_next(lexer);
        }
    }
    C23Lexer_free(lexer);
    if (!success) {
        free(source);
        free(starts);
        free(spans);
        return refuse(self, length + 1u);
    }
    free((*self).source);
    free((*self).starts);
    free((*self).spans);
    (*self).source = source;
    (*self).length = length;
    (*self).starts = starts;
    (*self).lineCount = count;
    (*self).spans = spans;
    (*self).spanCount = used;
    return true;
}

bool SemicolonSyntax_setPen(SemicolonSyntax *self, SyntaxRole role, TerminalPen pen) {
    if (self == nullptr || !SyntaxRole_isValid(role)) {
        THROW("SemicolonSyntax rejected palette role");
        return false;
    }
    (*self).pens[role] = pen;
    return true;
}

TerminalPen SemicolonSyntax_getPen(const SemicolonSyntax *self, SyntaxRole role) {
    return self == nullptr || !SyntaxRole_isValid(role) ? TerminalPen()
                                                     : (*self).pens[role];
}

TerminalPen SemicolonSyntax_penAt(const SemicolonSyntax *self, size_t line, size_t byte) {
    if (self == nullptr || line >= (*self).lineCount)
        return TerminalPen();
    size_t start = (*self).starts[line];
    size_t end = (*self).starts[line + 1u];
    if (byte >= end - start - 1u)
        return (*self).pens[SYNTAX_ROLE_IDENTIFIER];
    size_t offset = start + byte;
    size_t low = 0u;
    size_t high = (*self).spanCount;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        SyntaxSpan span = (*self).spans[middle];
        SourceRange range = span.range;
        if (offset < range.startByte)
            high = middle;
        else if (offset >= range.endByte)
            low = middle + 1u;
        else
            return (*self).pens[span.role];
    }
    return (*self).pens[SYNTAX_ROLE_IDENTIFIER];
}

size_t SemicolonSyntax_getLineCount(const SemicolonSyntax *self) {
    return self == nullptr ? 0u : (*self).lineCount;
}

size_t SemicolonSyntax_getSpanCount(const SemicolonSyntax *self) {
    return self == nullptr ? 0u : (*self).spanCount;
}

size_t SemicolonSyntax_exhaustionCount(const SemicolonSyntax *self) {
    return self == nullptr ? 0u : (*self).refusals;
}

TerminalProjection SemicolonSyntax_toString(const SemicolonSyntax *self) {
    if (self == nullptr)
        return TerminalProjection_format("nullptr");
    return TerminalProjection_format("SemicolonSyntax(lines=%zu, spans=%zu)",
                                     (*self).lineCount, (*self).spanCount);
}

TerminalProjection SemicolonSyntax_toStringStruct(const SemicolonSyntax *self) {
    if (self == nullptr)
        return TerminalProjection_format("nullptr");
    TerminalProjection projection = TerminalProjection_format(
        "SemicolonSyntax { source=%p, length=%zu, starts=%p, lineCount=%zu, spans=%p, spanCount=%zu, pens=[",
        (void*) (*self).source, (*self).length, (void*) (*self).starts,
        (*self).lineCount, (void*) (*self).spans, (*self).spanCount);
    for (size_t role = 0u; role <= SYNTAX_ROLE_IDENTIFIER; ++role) {
        TerminalPen pen = (*self).pens[role];
        TerminalProjection next = TerminalProjection_format(
            "%s%s%zu:{fg=%08x,bg=%08x,style=%u}", projection.text,
            role == 0u ? "" : ",", role, pen.fg, pen.bg, pen.style);
        next.truncated = next.truncated || projection.truncated;
        projection = next;
    }
    TerminalProjection result = TerminalProjection_format("%s], refusals=%zu }",
                                                         projection.text, (*self).refusals);
    result.truncated = result.truncated || projection.truncated;
    return result;
}
