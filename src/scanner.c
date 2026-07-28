#include "tree_sitter/parser.h"
#include "tree_sitter/alloc.h"
#include "tree_sitter/array.h"
#include <stdint.h>

typedef enum
{
    END_OF_LINE,
    END_OF_FILE,
    START_STRING,
    END_STRING,
    WHITESPACE,
    ERROR,
} Token;

#define DEBUG 0
// From this badass: https://stackoverflow.com/a/1644898
// and this https://www.reddit.com/r/C_Programming/comments/jq8zsq/gblfq5y/ for the ## __VA_ARGS bit
#define DBG(fmt, ...)                                     \
    do                                                    \
    {                                                     \
        if (DEBUG)                                        \
            fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, \
                    __LINE__, __func__, ##__VA_ARGS__);   \
    } while (0)

// Less noisy than DBG, but provides no context.
#define MSG(fmt, ...)                            \
    do                                           \
    {                                            \
        if (DEBUG)                               \
            fprintf(stderr, fmt, ##__VA_ARGS__); \
    } while (0)

static char pretty(char c)
{
    switch (c)
    {
    case '\n':
    case '\t':
    case '\f':
    case '\r':
        return '\0';
    default:
        return c;
    }
}

typedef enum
{
    BLOCK_TYPE_NONE,
    BLOCK_TYPE_CONTENT,
    BLOCK_TYPE_CHOICE,
    BLOCK_TYPE_GATHER,
    BLOCK_TYPE_STITCH,
    BLOCK_TYPE_KNOT
} BlockType;

// Typedef for numeric block level; juuust in case we want to change the amount of nesting we allow.
// To future self … CAUTION: This needs to be serialized to a string of bytes,
// so account for that when changing it to something larger.
typedef uint8_t BlockLevel;
const BlockLevel BLOCK_LEVEL_NONE = 0; // treat this an _unset_
const BlockLevel BLOCK_LEVEL_KNOT = 1;
const BlockLevel BLOCK_LEVEL_STITCH = 2;
const BlockLevel BLOCK_LEVEL_FLOW = 3;
const BlockLevel BLOCK_LEVEL_MAX = UINT8_MAX;

typedef struct
{
    bool is_in_string;
} Scanner;

////////////////////
// Lexing Helpers //
////////////////////

static void mark_end(TSLexer *lexer)
{
    lexer->mark_end(lexer);
}

static void skip(TSLexer *lexer)
{
    lexer->advance(lexer, true);
}

static void consume(TSLexer *lexer)
{
    lexer->advance(lexer, false);
}

static bool is_eof(TSLexer *lexer)
{
    return lexer->eof(lexer);
}

static int32_t lookahead(TSLexer *lexer)
{
    return lexer->lookahead;
}

///////////////////////
// Scanner Callbacks //
///////////////////////
unsigned tree_sitter_deep_pink_ink_external_scanner_serialize(void *payload, char *buffer)
{
    Scanner *scanner = (Scanner *)payload;
    uint32_t size = 0;

    if (size >= TREE_SITTER_SERIALIZATION_BUFFER_SIZE)
    {
        // printf needs to be commented out to be compiled for wasm (i.e. to use the playground).
        // printf("WARN: Bumped up against tree sitter serialization limit (%d)! We may have lost data!\n",
        //        TREE_SITTER_SERIALIZATION_BUFFER_SIZE);
    }
    buffer[size++] = scanner->is_in_string ? 1 : 0;
    // MSG("Serializing %d bytes of state\n", size);
    return size;
}

void tree_sitter_deep_pink_ink_external_scanner_deserialize(void *payload, const char *buffer, unsigned length)
{
    Scanner *scanner = (Scanner *)payload;

    if (buffer != NULL && length > 0)
    {
        uint32_t size = 0;
        scanner->is_in_string = buffer[size++] > 0 ? true : false;
    }
}

void *tree_sitter_deep_pink_ink_external_scanner_create(void)
{
    Scanner *scanner = ts_calloc(1, sizeof(Scanner));
    tree_sitter_deep_pink_ink_external_scanner_deserialize(scanner, NULL, 0);
    return scanner;
}

void tree_sitter_deep_pink_ink_external_scanner_destroy(void *payload)
{
    Scanner *scanner = (Scanner *)payload;
    ts_free(scanner);
}

/// Skip all whitspace (including carriage returns).
static void skip_ws(TSLexer *lexer)
{
    while (lookahead(lexer) <= ' ' && !is_eof(lexer))
        skip(lexer);
}

/// Skip whitespace until _before_ a carriage return (don't consume it).
/// Return `true` if ended up at up carriage return, false otherwise.
static bool skip_whitespace_to_newline(TSLexer *lexer)
{
    while (lookahead(lexer) <= ' ' && lookahead(lexer) != '\n' && !is_eof(lexer))
        skip(lexer);
    return lookahead(lexer) == '\n';
}

static void skip_whitspace(TSLexer *lexer)
{
    while (lookahead(lexer) <= ' ' && lookahead(lexer) != '\n' && !is_eof(lexer))
        skip(lexer);
}

static bool is_escaped_newline(TSLexer *lexer)
{
    if (lookahead(lexer) == '\\')
    {
        skip(lexer);
        return lookahead(lexer) == '\n';
    }
    return false;
}

bool tree_sitter_deep_pink_ink_external_scanner_scan(
    void *payload,
    TSLexer *lexer,
    const bool *valid_symbols)
{
    Scanner *scanner = (Scanner *)payload;

    MSG("at '%c' (%d).\n", pretty(lookahead(lexer)), lookahead(lexer));

    if (valid_symbols[ERROR])
    {
        return false;
    }

    if (is_eof(lexer))
    {
        MSG("  at EOF\n");
        lexer->result_symbol = END_OF_FILE;
        consume(lexer);
        return valid_symbols[END_OF_FILE];
    }

    if (scanner->is_in_string)
    {
        if (valid_symbols[END_STRING] && lookahead(lexer) == '"')
        {
            scanner->is_in_string = false;
            MSG("We aren't in the string anymore\n");
            consume(lexer);
            lexer->result_symbol = END_STRING;
            return true;
        }
    }

    if (valid_symbols[END_OF_LINE] && lookahead(lexer) == '\n')
    {
        MSG("  at EOL\n");
        lexer->result_symbol = END_OF_LINE;
        consume(lexer);
        return true;
    }

    if (!scanner->is_in_string)
    {
        if (valid_symbols[WHITESPACE] && lookahead(lexer) <= ' ')
        {
            skip_whitspace(lexer);
            lexer->result_symbol = WHITESPACE;
            return true;
        }

        if (valid_symbols[START_STRING] && lookahead(lexer) == '"')
        {
            MSG("at start string %c\n", pretty(lookahead(lexer)));
            {
                scanner->is_in_string = true;
                lexer->result_symbol = START_STRING;
                consume(lexer);
                MSG("we did it!\n");
                return true;
            }
        }
    }

    MSG("*** FALLTHROUGH! ***\n");
    return false;
}
