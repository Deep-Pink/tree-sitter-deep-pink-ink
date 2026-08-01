#include "tree_sitter/alloc.h"
#include "tree_sitter/array.h"
#include "tree_sitter/parser.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  END_OF_LINE,
  END_OF_FILE,
  START_STRING,
  END_STRING,
  WHITESPACE,
  START_LINE_COMMAND,
  START_TAG_COMMAND,
  START_OF_LINE,
  START_HOLE,
  END_HOLE,
  OPEN_FORMAT_TAG,               // "<" b>
  CLOSE_FORMAT_TAG,              // <b ">"
  CLOSE_SELF_CLOSING_FORMAT_TAG, // <b "/>"
  OPEN_CLOSE_FORMAT_TAG,         // "</" b>
  SPEAKER_IDENTIFIER,
  ERROR,
} Token;

typedef enum {
  NONE = 0,
  IN_START_TAG,
  IN_END_TAG,
} FormatTagState;

#define DEBUG 0
// From this badass: https://stackoverflow.com/a/1644898
// and this https://www.reddit.com/r/C_Programming/comments/jq8zsq/gblfq5y/ for
// the ## __VA_ARGS bit
#define DBG(fmt, ...)                                                          \
  do {                                                                         \
    if (DEBUG)                                                                 \
      fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__,        \
              ##__VA_ARGS__);                                                  \
  } while (0)

// Less noisy than DBG, but provides no context.
#define MSG(fmt, ...)                                                          \
  do {                                                                         \
    if (DEBUG)                                                                 \
      fprintf(stderr, fmt, ##__VA_ARGS__);                                     \
  } while (0)

static char pretty(char c) {
  switch (c) {
  case '\n':
  case '\t':
  case '\f':
  case '\r':
    return '\0';
  default:
    return c;
  }
}

/// A fixed capacity, dynamic sized queue of bits (expressed as bools)
#define BITQUEUE_CAPACITY 64

typedef struct {
  uint64_t bits;
  size_t count;
  size_t read_offset;
} BitQueue;

/// @param index the index of the bit starting from the front
/// @return the bit value
static bool bitqueue_get(const BitQueue *queue, size_t index) {
  assert(index < queue->count);
  return (queue->bits >> ((index + queue->read_offset) % BITQUEUE_CAPACITY)) &
         1;
}

static void bitqueue_set(BitQueue *queue, size_t index, bool value) {
  assert(index < queue->count);
  size_t bit_index = (index + queue->read_offset) % BITQUEUE_CAPACITY;
  if (value) {
    queue->bits |= (1ULL << bit_index);
  } else {
    queue->bits &= ~(1ULL << bit_index);
  }
}

/// Removes the bit at the front of the queue
/// @returns the value of the bit that was removed
static bool bitqueue_pop_front(BitQueue *queue) {
  assert(queue->count > 0);
  bool value = bitqueue_get(queue, 0);
  queue->count--;
  queue->read_offset++;
  return value;
}

/// Appends a bit to the back of the queue
static void bitqueue_push_back(BitQueue *queue, bool value) {
  assert(queue->count < BITQUEUE_CAPACITY);
  queue->count++;
  bitqueue_set(queue, queue->count - 1, value);
}

/// @returns true if the queue holds no bits.
static bool bitqueue_empty(const BitQueue *queue) { return queue->count == 0; }

/// @returns the number of bits held by the queue.
static size_t bitqueue_count(const BitQueue *queue) { return queue->count; }

#if DEBUG
static void bitqueue_to_chars(const BitQueue *queue, char *str) {
  sprintf(str, "%zu:", queue->count);
  for (size_t i = 0; i < queue->count; ++i) {
    strcat(str, bitqueue_get(queue, i) ? "#" : ".");
  }
}
#endif

typedef struct {
  uint32_t *data;
  size_t length;
} StringEntry;

/* Stack entry for text tag parsing */
typedef struct {
  size_t index;      // Index of the opening '>' in lt_is_tmpl
  size_t expr_depth; // The value of 'expr_depth' for the opening '<'
} StackEntry;

/* Dynamic array for StackEntry */
typedef struct {
  StackEntry *data;
  size_t size;
  size_t capacity;
} StackEntryArray;

static void stack_entry_array_init(StackEntryArray *array) {
  array->data = NULL;
  array->size = 0;
  array->capacity = 0;
}

static void stack_entry_array_push(StackEntryArray *array, StackEntry entry) {
  if (array->size == array->capacity) {
    size_t new_capacity = array->capacity == 0 ? 1 : array->capacity * 2;
    StackEntry *new_data =
        realloc(array->data, new_capacity * sizeof(StackEntry));
    if (new_data == NULL) {
      /* Handle allocation failure */
      return;
    }
    array->data = new_data;
    array->capacity = new_capacity;
  }
  array->data[array->size++] = entry;
}

static void stack_entry_array_pop(StackEntryArray *array) {
  if (array->size > 0) {
    array->size--;
  }
}

static StackEntry *stack_entry_array_back(StackEntryArray *array) {
  if (array->size > 0) {
    return &array->data[array->size - 1];
  }
  return NULL;
}

static bool stack_entry_array_empty(StackEntryArray *array) {
  return array->size == 0;
}

static void stack_entry_array_clear(StackEntryArray *array) { array->size = 0; }

static void stack_entry_array_free(StackEntryArray *array) {
  free(array->data);
  array->data = NULL;
  array->size = 0;
  array->capacity = 0;
}

typedef struct {
  bool is_in_string;
  bool is_in_command;
  FormatTagState format_tag_state;
  uint32_t hole_count;
  uint32_t format_tag_body_count;
} Scanner;

////////////////////
// Lexing Helpers //
////////////////////

static void mark_end(TSLexer *lexer) { lexer->mark_end(lexer); }

static void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static bool consume(TSLexer *lexer) {
  lexer->advance(lexer, false);
  return true;
}

static bool is_eof(TSLexer *lexer) { return lexer->eof(lexer); }

static int32_t lookahead(TSLexer *lexer) { return lexer->lookahead; }

static bool is_whitespace(TSLexer *lexer) {
  return lookahead(lexer) <= ' ' && lookahead(lexer) != '\n';
}

static bool is_whitespace_or_newline(TSLexer *lexer) {
  return lookahead(lexer) <= ' ';
}
///////////////////////
// Scanner Callbacks //
///////////////////////
unsigned tree_sitter_deep_pink_ink_external_scanner_serialize(void *payload,
                                                              char *buffer) {
  Scanner *scanner = (Scanner *)payload;
  size_t bytes_written = 0;
  buffer[bytes_written++] = scanner->is_in_string ? 1 : 0;
  buffer[bytes_written++] = scanner->is_in_command;
  memcpy(buffer + bytes_written, &scanner->format_tag_state,
         sizeof(scanner->format_tag_state));
  bytes_written += sizeof(scanner->format_tag_state);
  memcpy(buffer + bytes_written, &scanner->hole_count,
         sizeof(scanner->hole_count));
  bytes_written += sizeof(scanner->hole_count);
  memcpy(buffer + bytes_written, &scanner->format_tag_body_count,
         sizeof(scanner->format_tag_body_count));
  bytes_written += sizeof(scanner->format_tag_body_count);
  return (unsigned)bytes_written;
}

void tree_sitter_deep_pink_ink_external_scanner_deserialize(void *payload,
                                                            const char *buffer,
                                                            unsigned length) {
  Scanner *scanner = (Scanner *)payload;

  if (buffer != NULL && length > 0) {
    uint32_t bytes_read = 0;
    scanner->is_in_string = buffer[bytes_read++] > 0 ? true : false;
    scanner->is_in_command = buffer[bytes_read++] > 0 ? true : false;
    memcpy(&scanner->format_tag_state, buffer + bytes_read,
           sizeof(scanner->format_tag_state));
    bytes_read += sizeof(scanner->format_tag_state);
    memcpy(&scanner->hole_count, buffer + bytes_read,
           sizeof(scanner->hole_count));
    bytes_read += sizeof(scanner->hole_count);
    memcpy(&scanner->format_tag_body_count, buffer + bytes_read,
           sizeof(scanner->format_tag_body_count));
    bytes_read += sizeof(scanner->format_tag_body_count);
  }
}

void *tree_sitter_deep_pink_ink_external_scanner_create(void) {
  Scanner *scanner = ts_calloc(1, sizeof(Scanner));
  scanner->format_tag_state = NONE;
  tree_sitter_deep_pink_ink_external_scanner_deserialize(scanner, NULL, 0);
  return scanner;
}

void tree_sitter_deep_pink_ink_external_scanner_destroy(void *payload) {
  Scanner *scanner = (Scanner *)payload;
  ts_free(scanner);
}

/// Skip all whitspace (including carriage returns).
static void skip_whitespace_including_newline(TSLexer *lexer) {
  while (lookahead(lexer) <= ' ' && !is_eof(lexer))
    skip(lexer);
}

/// Skip whitespace until _before_ a carriage return (don't consume it).
/// Return `true` if ended up at up carriage return, false otherwise.
static bool skip_whitespace_to_newline(TSLexer *lexer) {
  while (lookahead(lexer) <= ' ' && lookahead(lexer) != '\n' && !is_eof(lexer))
    skip(lexer);
  return lookahead(lexer) == '\n';
}

static void skip_whitespace(TSLexer *lexer) {
  while (lookahead(lexer) <= ' ' && lookahead(lexer) != '\n' &&
         !is_eof(lexer)) {
    skip(lexer);
  }
}

static void consume_whitespace(TSLexer *lexer) {
  while (lookahead(lexer) <= ' ' && lookahead(lexer) != '\n' &&
         !is_eof(lexer)) {
    consume(lexer);
  }
}

static void consume_whitespace_including_newline(TSLexer *lexer) {
  while (lookahead(lexer) <= ' ' && !is_eof(lexer)) {
    consume(lexer);
  }
}

static bool is_escaped_newline(TSLexer *lexer) {
  if (lookahead(lexer) == '\\') {
    skip(lexer);
    return lookahead(lexer) == '\n';
  }
  return false;
}

bool tree_sitter_deep_pink_ink_external_scanner_scan(
    void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;

  MSG("at '%c' (%d).\n", pretty(lookahead(lexer)), lookahead(lexer));

  if (valid_symbols[ERROR]) {
    return false;
  }

  if (is_eof(lexer)) {
    MSG("  at EOF\n");
    lexer->result_symbol = END_OF_FILE;
    consume(lexer);
    return valid_symbols[END_OF_FILE];
  }

  if (scanner->hole_count > 0) {
    if (valid_symbols[END_HOLE] && lookahead(lexer) == '}') {
      consume(lexer);
      scanner->hole_count--;
      lexer->result_symbol = END_HOLE;
      return true;
    }
  }

  if (valid_symbols[START_OF_LINE] && lexer->get_column(lexer) == 0) {
    lexer->result_symbol = START_OF_LINE;
    return true;
  }

  if (scanner->is_in_string) {
    if (valid_symbols[END_STRING] && lookahead(lexer) == '"') {
      scanner->is_in_string = false;
      MSG("We aren't in the string anymore\n");
      consume(lexer);
      lexer->result_symbol = END_STRING;
      return true;
    }
    return false;
  }

  bool in_tag_content =
      scanner->format_tag_state != NONE || scanner->format_tag_body_count > 0;

  if (!in_tag_content && is_whitespace(lexer)) {
    consume_whitespace(lexer);
    lexer->result_symbol = WHITESPACE;
    return true;
  } else if (in_tag_content && is_whitespace_or_newline(lexer)) {
    consume_whitespace_including_newline(lexer);
    lexer->result_symbol = WHITESPACE;
    return true;
  }

  if (lookahead(lexer) == '\n') {
    MSG("  at EOL\n");
    scanner->is_in_command = false;
    consume(lexer);
    lexer->result_symbol = END_OF_LINE;
    return true;
  }

  if (!in_tag_content) {
    if (valid_symbols[SPEAKER_IDENTIFIER]) {
      int32_t current = lookahead(lexer);
      if (current == '_' || (current >= 'A' && current <= 'Z') ||
          (current >= 'a' && current <= 'a')) {
        lexer->result_symbol = SPEAKER_IDENTIFIER;
        do {
          consume(lexer);
          current = lookahead(lexer);
        } while (current == '_' || (current >= 'A' && current <= 'Z') ||
                 (current >= 'a' && current <= 'a') ||
                 (current >= '0' && current <= '9'));
        skip_whitespace(lexer);
        if (lookahead(lexer) == ':') {
          return true;
        }
        return false;
      }
    }
    if (lookahead(lexer) == '#') {
      scanner->is_in_command = true;
      consume(lexer);
      lexer->result_symbol = START_TAG_COMMAND;
      return true;
    }
    if (lookahead(lexer) == '>') {
      lexer->result_symbol = START_LINE_COMMAND;
      consume(lexer);
      if (lookahead(lexer) == '>' && consume(lexer) &&
          lookahead(lexer) == '>' && consume(lexer)) {
        MSG("START LINE COMMAND\n");
        scanner->is_in_command = true;
        lexer->result_symbol = START_LINE_COMMAND;
        return true;
      }
      return false;
    }
  }

  if (!scanner->is_in_string) {
    FormatTagState currentState = scanner->format_tag_state;
    if (currentState == IN_START_TAG) {
      if (lookahead(lexer) == '>') {
        consume(lexer);
        lexer->result_symbol = CLOSE_FORMAT_TAG;
        scanner->format_tag_body_count++;
        scanner->format_tag_state = NONE;
        return true;
      }
      if (lookahead(lexer) == '/') {
        consume(lexer);
        skip_whitespace(lexer);
        if (lookahead(lexer) == '>') {
          consume(lexer);
          scanner->format_tag_state = NONE;
          lexer->result_symbol = CLOSE_SELF_CLOSING_FORMAT_TAG;
          return true;
        }
      }
    } else if (currentState == IN_END_TAG) {
      if (lookahead(lexer) == '>') {
        consume(lexer);
        lexer->result_symbol = CLOSE_FORMAT_TAG;
        scanner->format_tag_state = NONE;
        return true;
      }
    } else if (lookahead(lexer) == '<') {
      consume(lexer);
      skip_whitespace_including_newline(lexer);
      if (lookahead(lexer) == '/') {
        scanner->format_tag_state = IN_END_TAG;
        lexer->result_symbol = OPEN_CLOSE_FORMAT_TAG;
        consume(lexer);
        scanner->format_tag_body_count--;
        return scanner->format_tag_body_count >= 0;
      } else {
        scanner->format_tag_state = IN_START_TAG;
        lexer->result_symbol = OPEN_FORMAT_TAG;
        return true;
      }
    }
    if (lookahead(lexer) == '"') {
      MSG("at start string %c\n", pretty(lookahead(lexer)));
      scanner->is_in_string = true;
      lexer->result_symbol = START_STRING;
      consume(lexer);
      return true;
    }
    if (lookahead(lexer) == '{') {
      consume(lexer);
      lexer->result_symbol = START_HOLE;
      scanner->hole_count += 1;
      return true;
    }
  }

  MSG("*** FALLTHROUGH! ***\n");
  return false;
}
