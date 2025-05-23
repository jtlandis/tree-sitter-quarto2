#include <stdint.h>
#include "tree_sitter/parser.h"
#include "tree_sitter/array.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

enum TokenType {
  LINE_END,        // Token type for line_end
  EMPHASIS_STAR_START,  // Token type for emphasis_start
  EMPHASIS_STAR_END,     // Token type for emphasis_end
  EMPHASIS_UNDER_START,
  EMPHASIS_UNDER_END,
  ERROR, //General Emphasis
};

enum ParseToken {
    NONE,
    EMPHASIS_STAR,
    EMPHASIS_UNDER,
    STRONG_STAR,
};

// this struct is for emphasis
// strong, and strong_emphasis
// sections. I dont predict that
// they will need the full size
// of a uint32, but i suppose it
// isn't imposible?
typedef struct {
  bool within;
  uint32_t row;
  uint32_t col;
} WithinRange;


typedef struct LexWrap {
    TSLexer *lexer;
    uint32_t pos;
    Array(int32_t) buffer;
    Array(uint32_t) new_line_loc;
} LexWrap;


static LexWrap new_lexer(TSLexer *lexer) {
    LexWrap obj;
    obj.lexer = lexer;
    obj.pos = 0;
    array_init(&obj.buffer);
    array_init(&obj.new_line_loc);
    return obj;
}

static int32_t lex_lookahead(LexWrap* wrapper) {

    if (wrapper->pos == wrapper->buffer.size) {
        return wrapper->lexer->lookahead;
    } else {
        return *array_get(&wrapper->buffer, wrapper->pos);
    }
}

static void lex_advance(LexWrap* wrapper, bool skip) {
    if (wrapper->pos == wrapper->buffer.size) {
        int32_t lookahead = wrapper->lexer->lookahead;
        if (lookahead == '\n') {
            array_push(&wrapper->new_line_loc, wrapper->pos + 1);
        }
        array_push(&wrapper->buffer, lookahead);
        wrapper->lexer->advance(wrapper->lexer, skip);
    }
    wrapper->pos++;
}

static void lex_backtrack_n(LexWrap* wrapper, uint32_t n) {
    assert(n <= wrapper->pos);
    wrapper->pos -= n;
}

typedef struct Pos {
    uint32_t row;
    uint32_t col;
} Pos;

static Pos new_position(uint32_t row, uint32_t col) {
    Pos obj;
    obj.row = row;
    obj.col = col;
    return obj;
}

/// x < y
static bool pos_lt(Pos *x, Pos *y) {
    return (x->row < y->row ) || (x->row == y->row && x->col < y->col);
}

/// x <= y
static bool pos_le(Pos *x, Pos *y) {
    return x->row <= y->row && x->col <= y->col;
}

static bool pos_gt(Pos *x, Pos *y) {
    return (x->row > y->row) || (x->row == y->row && x->col > y ->col);
}

static bool pos_ge(Pos *x, Pos *y) {
    return x->row >= y->row && x->col >= y->col;
}

static Pos lex_current_range(LexWrap *wrapper) {
    Pos range = new_position(0, wrapper->pos);
    if (wrapper->new_line_loc.size > 0) {
        uint32_t diff = 0;
        uint32_t i = 0;
        uint32_t line_index = *array_get(&wrapper->new_line_loc, i);
        while(line_index < wrapper->pos
            && i < wrapper->new_line_loc.size) {
            range.row = i;
            diff = line_index - diff;
            i++;
            line_index = *array_get(&wrapper->new_line_loc, i);
            range.col -= diff;
        }
    }
    return range;
}

typedef struct ParseResult {
    bool success;
    uint32_t length;
    Pos start;
    Pos end;
    enum ParseToken token;
} ParseResult;

static ParseResult new_parse_result() {
    ParseResult obj;
    obj.success = false;
    obj.length = 0;
    obj.start = new_position(0, 0);
    obj.end = new_position(0, 0);
    obj.token = NONE;
    return obj;
}

typedef Array(ParseResult) ParseResultArray;

// the stack is ordered in the direction that the lexer will encounter elements.
//
static bool stack_insert(ParseResultArray* array, uint32_t index, ParseResult element) {
    assert(index <= array->size);
    if (index == array->size) {
        array_push(array, element);
    } else {
        uint32_t i = index;
        ParseResult *old = &(array)->contents[i];
        // check that what we want to insert is not
        // within any elements that have start positions
        // before the end position of our element.
        while (pos_gt(&element.end, &old->start)) {
            if (pos_lt(&element.end, &old->end)) {
                return false;
            }
            i++;
            if (i== array->size) {
                break;
            }
            old =  &(array)->contents[i];
        }
        array_insert(array, index, element);

    }
    return true;
}

static bool is_whitespace(int32_t char_) {
    return char_ == ' ' || char_ == '\t' || char_ == '\n';
}
static bool is_whitespace_next(TSLexer *lexer) {
    return is_whitespace(lexer->lookahead);
}

static bool is_inline_synatx(int32_t char_) {
    return char_ == '*' || char_ == '_' ||
     char_ == '^' || char_ == '~' ||
     char_ == '`' || char_ == '@' ||
     char_ == '[' || char_ == ']';
}

// prototypes:

static ParseResult parse_inline(LexWrap *wrapper, ParseResultArray* stack);
static ParseResult parse_star(LexWrap *wrapper, ParseResultArray* stack);

static ParseResult parse_inline(LexWrap *wrapper, ParseResultArray* stack) {
    uint32_t stack_start_size = stack->size;
    uint32_t buffer_start_pos = wrapper->pos;
    uint32_t advance_count = 0;
    ParseResult res = new_parse_result();
    res.start = lex_current_range(wrapper);
    int32_t lookahead = lex_lookahead(wrapper);
    switch (lookahead) {
        case '*':
            res = parse_star(wrapper, stack);
            break;
    }
    return res;
}

static ParseResult parse_star(LexWrap *wrapper, ParseResultArray* stack) {

    uint32_t stack_start_size = stack->size;
    uint32_t buffer_start_pos = wrapper->pos;
    uint32_t advance_count = 0;
    ParseResult res = new_parse_result();
    res.start = lex_current_range(wrapper);

    /// for this parse to be valid one of
    /// if we detect 1 --> expecting emphasis
    /// if we detect 2 --> expecting strong
    /// if we detect 3 --> expecting combo of emphasis or strong
    ///                    with the possibility of either ending
    ///                    early.
    /// > 3 --> return false
    uint8_t char_count = 0;
    while (lex_lookahead(wrapper) == '*') {
        lex_advance(wrapper, false);
        char_count++;
        advance_count++;
    }
    if (char_count > 3) {
        lex_backtrack_n(wrapper, char_count);
        return res;
    }
    enum ParseToken ret_type;
    switch (char_count) {
        case 1: {
            ret_type = EMPHASIS_STAR;
            break;
        }
        case 2: {
            ret_type = STRONG_STAR;
            break;
        }

        case 3:
            ret_type = NONE;
            break;
    }
    int32_t lookahead = lex_lookahead(wrapper);
    if (is_whitespace(lookahead)) {
        // cannot parse star as any type of valid
        // emphasis or strong.
        return res;
    }
    uint8_t end_char_count = 0;
    uint8_t new_line_count = 0;
    while(lookahead != '\0' && char_count > 0) {
        switch (lookahead) {
            case '*': {
                // see how many we can consume
                end_char_count = 0;
                while (lex_lookahead(wrapper) == '*') {
                    lex_advance(wrapper, false);
                    end_char_count++;
                }
                switch (char_count) {
                    case 1: {
                        // we only have one left to match...
                        // no matter the size of end_char_count
                        lex_backtrack_n(wrapper, end_char_count - 1);
                        res.end = lex_current_range(wrapper);
                        res.success = true;
                        res.token = EMPHASIS_STAR;
                        res.length = wrapper->pos - buffer_start_pos;
                        // note that the lexer has pushed past  all the stars
                        // but we are now in a backtracking state.
                        if (end_char_count > 1) {
                            // if we parse further and find the end result
                            // is EMPHASIS_STAR, invalidate
                            ParseResult attempt = parse_star(wrapper, stack);
                            if (attempt.token == EMPHASIS_STAR) {
                                res.success = false;
                            }
                        }
                        return res;
                        break;
                    }
                    case 2: {
                        switch (end_char_count) {
                            case 1: {
                                // this may invalidate our current
                                // scope.
                                ParseResult attempt = parse_star(wrapper, stack);
                                // if it wasn't successful, then this token will
                                // also not be successful
                                if (!attempt.success) {
                                    return res;
                                }
                                lookahead = lex_lookahead(wrapper);
                                continue;
                            }
                            default: {
                                // no matter how many match here. we have
                                // reached our target.
                                lex_backtrack_n(wrapper, end_char_count - 2);
                                res.end = lex_current_range(wrapper);
                                res.success = true;
                                res.token = STRONG_STAR;
                                res.length = wrapper->pos - buffer_start_pos;
                                // note that the lexer has pushed past  all the stars
                                // but we are now in a backtracking state.
                                if (!stack_insert(stack, stack_start_size, res)) {
                                    res.success = false;
                                }
                                return res;
                                break;
                            }
                        }
                        break;
                    }
                    case 3: {
                        switch (end_char_count){
                            case 1: {
                                // inner syntax is an emph and outer is
                                // likely a strong.
                                // create new result to insert
                                ParseResult inner = new_parse_result();
                                inner.end = lex_current_range(wrapper);
                                inner.start = res.start;
                                inner.start.col += 2;
                                inner.success = true;
                                inner.token = EMPHASIS_STAR;
                                inner.length = wrapper->pos - buffer_start_pos - 2;
                                if (stack_insert(stack, stack_start_size, inner)) {
                                    char_count--;
                                }
                                break;
                            }
                            case 2: {
                                // inner syntax is an strong and outer is
                                // likely a emph.
                                // create new result to insert
                                ParseResult inner = new_parse_result();
                                inner.end = lex_current_range(wrapper);
                                inner.start = res.start;
                                inner.start.col += 1;
                                inner.success = true;
                                inner.token = STRONG_STAR;
                                inner.length = wrapper->pos - buffer_start_pos - 1;
                                if (stack_insert(stack, stack_start_size, inner)) {
                                    char_count -= 2;
                                }
                                break;
                            }
                            default: {
                                // no matter how many times we detected
                                // a '*'
                                // we have matched our stack!
                                lex_backtrack_n(wrapper, end_char_count - 3);
                                res.end = lex_current_range(wrapper);
                                res.success = true;
                                res.token = STRONG_STAR;
                                res.length = advance_count;
                                // inner will be an emphasis
                                ParseResult inner = new_parse_result();
                                inner.end = lex_current_range(wrapper);
                                inner.end.col -= 2;
                                inner.start = res.start;
                                inner.start.col += 2;
                                inner.success = true;
                                inner.token = EMPHASIS_STAR;
                                inner.length = wrapper->pos - buffer_start_pos - 2;
                                if (stack_insert(stack, stack_start_size, inner)) {
                                    array_insert(stack, stack_start_size, res);
                                } else {
                                    res.success = false;
                                }
                                return res;
                            }
                            break;
                        }
                        break;
                    }
                }
                break;
            }
            case '\n': {
                new_line_count++;
                if (new_line_count > 1) {
                    return res;
                }
                break;
            }
            case '\\': {
                // treat next character as literal - do not
                // parse it
                advance_count++;
                lex_advance(wrapper, false);
                break;
            }
            default: {
                // check if inline symbol
                new_line_count = 0;
                if (is_inline_synatx(lookahead)) {
                    ParseResult attempt = parse_inline(wrapper, stack);
                    if (!attempt.success) {
                        return res;
                    }
                }
            }
                break;
        }
        advance_count++;
        lex_advance(wrapper, false);
        lookahead = lex_lookahead(wrapper);
    }
    
    return res;

}

static WithinRange new_range2() {
    WithinRange obj;
    obj.within = false;
    obj.row = 0;
    obj.col = 0;
    return obj;
}



typedef struct {
  WithinRange emphasis; // State to track if we're inside an emphasis block
} ScannerState;

// Pretty-print a WithinRange struct
static void print_within_range(const WithinRange *range) {
    fprintf(stderr, "WithinRange { within: %s, row: %u, col: %u }\n",
            range->within ? "true" : "false",
            range->row,
            range->col);
}

// Pretty-print a ScannerState struct
static void print_scanner_state(const ScannerState *state) {
    fprintf(stderr, "ScannerState {\n  emphasis: ");
    print_within_range(&state->emphasis);
    fprintf(stderr, "}\n");
}


void *tree_sitter_quarto_external_scanner_create() {
  ScannerState *state = (ScannerState *)malloc(sizeof(ScannerState));
  state->emphasis = new_range2(); // Initialize the state
  return state;
}

void tree_sitter_quarto_external_scanner_destroy(void *payload) {
  free(payload); // Free the allocated state
}

unsigned tree_sitter_quarto_external_scanner_serialize(void *payload, char *buffer) {
  ScannerState *state = (ScannerState *)payload;
  size_t offset = 0;

  buffer[offset++] = state->emphasis.within ? 1 : 0;
  memcpy(buffer + offset, &state->emphasis.row, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(buffer + offset, &state->emphasis.col, sizeof(uint32_t));
  offset += sizeof(uint32_t);

  return offset; // Return the number of bytes written
}

void tree_sitter_quarto_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    if (length >= 1 + 2 * sizeof(uint32_t)) {
       ScannerState *state = (ScannerState *)payload;
       size_t offset = 0;

       state->emphasis.within = buffer[offset++] == 1;
       memcpy(&state->emphasis.row, buffer + offset, sizeof(uint32_t));
       offset += sizeof(uint32_t);
       memcpy(&state->emphasis.col, buffer + offset, sizeof(uint32_t));
       // offset += sizeof(uint32_t); // Not needed unless you add more fields
     }
}



// static bool is_static_at_symbol(TSLexer *lexer, int32_t current_char) {
//     return lexer->lookahead == '@' && !u_isalpha((UChar32)current_char);
// }

// static bool valid_prior_emph_end(TSLexer *lexer, int32_t current_char, int32_t emph_char, bool *in_citation) {
//     bool whitespace_next = is_whitespace_next(lexer);
//     bool detect_at_symbol = emph_char == '_' && is_static_at_symbol(lexer, current_char);
//     if (whitespace_next) {
//         *in_citation = false;
//     } else if (detect_at_symbol) {
//         *in_citation = true;
//     }
//     if (whitespace_next ||  *in_citation ||
//         lexer->lookahead == '_' ||
//         lexer->lookahead == '*') {

//         return false;
//     }

//     return true;
// }

static int32_t other_emphasis(int32_t char_) {
    if (char_=='*') {
        return '_';
    }
    return '*';
}

typedef struct Queue {
    uint8_t emph_star;
    uint8_t emph_under;
    uint8_t strong_star;
    uint8_t strong_under;
    uint8_t strong_emph_star;
    uint8_t strong_emph_under;
    uint8_t superscript;
    uint8_t subscript;
    uint8_t strikethrough;
    uint8_t Reference;
    uint8_t span;
    void (*set_emph)(struct Queue* self, int32_t char_, uint8_t index);
    uint8_t (*get_emph)(struct Queue* self, int32_t char_);
    void (*set_strong)(struct Queue* self, int32_t char_, uint8_t index);
    uint8_t (*get_strong)(struct Queue* self, int32_t char_);
    void (*set_strong_emph)(struct Queue* self, int32_t char_, uint8_t index);
    uint8_t (*get_strong_emph)(struct Queue* self, int32_t char_);
} Queue;

void set_emph_impl(Queue* self, int32_t char_, uint8_t index) {
    if (char_ == '_') {
        self->emph_under = index;
    } else {
        self->emph_star = index;
    }
}

uint8_t get_emph_impl(Queue* self, int32_t char_) {
    if (char_ == '_') {
        return self->emph_under;
    } else {
        return self->emph_star;
    }
}

void set_strong_impl(Queue* self, int32_t char_, uint8_t index) {
    if (char_ == '_') {
        self->strong_under = index;
    } else {
        self->strong_star = index;
    }
}

uint8_t get_strong_impl(Queue* self, int32_t char_) {
    if (char_ == '_') {
        return self->strong_under;
    } else {
        return self->strong_star;
    }
}

void set_strong_emph_impl(Queue* self, int32_t char_, uint8_t index) {
    if (char_ == '_') {
        self->strong_emph_under = index;
    } else {
        self->strong_emph_star = index;
    }
}

uint8_t get_strong_emph_impl(Queue* self, int32_t char_) {
    if (char_ == '_') {
        return self->strong_emph_under;
    } else {
        return self->strong_emph_star;
    }
}

static Queue new_queue() {
    Queue obj;
    obj.emph_star = 0;
    obj.emph_under = 0;
    obj.strong_star = 0;
    obj.strong_under = 0;
    obj.strong_emph_star = 0;
    obj.strong_emph_under = 0;
    obj.superscript = 0;
    obj.subscript = 0;
    obj.strikethrough = 0;
    obj.Reference = 0;
    obj.span = 0;
    obj.set_emph = set_emph_impl;
    obj.get_emph = get_emph_impl;
    obj.set_strong = set_strong_impl;
    obj.get_strong = get_strong_impl;
    obj.set_strong_emph = set_strong_emph_impl;
    obj.get_strong_emph = get_strong_emph_impl;

    return obj;
}

// advances the lexer until we find
// another star that would be valid
// closing star.
// a result of true indicates the lexer
// has just consumed that valid closing star
//
// a result of false indicates the lexer
// failed to find a valid closing star.
static bool find_end_emph(TSLexer *lexer, int32_t char_, WithinRange *range) {
    fprintf(stderr, "finding end %c\n", char_);
    uint8_t char_count = 1;
    while (lexer->lookahead == char_) {
        lexer->advance(lexer, false);
        char_count++;
    }
    // this function is for finding a SINGLE emphasis token
    // however if we detect excatly 2, there is no way to
    // split them down the line
    if (char_count==2) {
        return false;
    }
    if (char_count>3) {
        // this will invalidate the next three
        // attempts to find any kind of '*' or '_' pattern.
        // should do something to the scanner state
        return false;
    }
    uint8_t new_line_count = 0;
    uint32_t row = 0;
    uint32_t col = 0;
    int32_t other_char = other_emphasis(char_);
    int32_t other_char_count = 0;
    Queue queue = new_queue();
    uint8_t qcount = 1;
    queue.set_emph(&queue, char_, qcount);
    if (char_count > 1) {
        qcount++;
        queue.set_strong(&queue, char_, qcount);
    }
    while(lexer->lookahead == other_char) {
        other_char_count++;
        col++;
        lexer->advance(lexer, false);
    }
    // this is immediately after char_
    // and if whitespace follows the other
    // emphasis symbol, this doesnt necessarily
    // invalidate char_ empahsis
    if (is_whitespace_next(lexer) || other_char_count > 3) {
        other_char_count = 0;
    } else {
        qcount++;
        switch (other_char_count) {
            case 1:
              queue.set_emph(&queue, other_char, qcount);
              break;
            case 2:
                queue.set_strong(&queue, other_char, qcount);
                break;
            case 3:
                queue.set_strong_emph(&queue, other_char, qcount);
                break;
        }
    }

    // assume the prior character is whitespace.
    int32_t current_char = ' ';
    bool in_citation = false;
    // StrongRange strong = new_strong_range();
    while (lexer->lookahead != '\0' &&
           // lexer->lookahead != char_ &&
           char_count > 0 ) {
            // fprintf(stderr, "target_char: %c - current_char: %c - next char: %c - valid_prior: %i - in_citation: %i",
            //     char_, current_char, lexer->lookahead, valid_prior_char, in_citation);
        switch (lexer->lookahead) {
            case '@':
               if (is_whitespace(current_char)) {
                   //likely a citation
                   qcount++;
                   queue.Reference = qcount;
               }
               break;
            case '*':
            case '_':
                if (lexer->lookahead == char_) {
                    if (queue.Reference) {

                    }
                    // we only reach this branch if char_count == 3
                    // or == 1
                    uint8_t char_consumed = 0;
                    while(lexer->lookahead == char_) {
                        char_consumed++;
                        col++;
                        lexer->advance(lexer, false);
                    }
                    // short cut - for this scan
                    if (char_count == 3) {
                       if (char_consumed == 2) {
                           char_count = 1;
                           continue;
                       }
                       // decrementing by 2 would be the only valid
                       // way for emphasis.
                       return false;
                    }
                    // char_count == 1 here
                    switch (char_consumed) {
                        case 1:
                          char_count = 0;
                          break;
                        case 2:
                          char_count += 2;
                        default:
                          char_count = 0;
                          char_consumed--;
                          col -= char_consumed;
                          continue;
                          break;
                    }
                } else {
                    if (other_char_count==0) {
                        while(lexer->lookahead == other_char) {
                            other_char_count++;
                            col++;
                            lexer->advance(lexer, false);
                        }
                        // next symbol MUST be a character
                        if (is_whitespace_next(lexer) || other_char_count > 3) {
                            other_char_count = 0;
                        }
                    } else {
                        int32_t prior_count = other_char_count;
                        while(lexer->lookahead == other_char && other_char_count > 0) {
                            other_char_count--;
                            col++;
                            lexer->advance(lexer, false);
                        }
                        if (prior_count == 2 && other_char_count != 0) {
                            return false;
                        }
                        if (other_char_count == 0 && lexer->lookahead == other_char) {
                            // we have a trailing symbol with no whitespace
                            return false;
                        }
                        if (other_char=='_' && !is_whitespace_next(lexer)) {
                            // underscores could be used interword
                            return false;
                        }
                        // // next symbol MUST be a character
                        // if (!u_isalnum((UChar32)lexer->lookahead)) {
                        //     // we have detected the other emphasis
                        //     // and it has an invalid start...
                        //     return false;
                        // }
                    }
                }

                break;
            case '\n':
                new_line_count++;
                row++;
                col = 0;
                if (new_line_count > 1) {
                    return false;
                }
                break;
            case '\\':
                // assuming that this is an emphasis block.
                // advance and the next lexer->advance will
                // treat it as a literal. Note that '\\' is
                // a valid prior char and thus anyting we
                // move past will be considered valid.
                col++;
                lexer->advance(lexer, false);
                break;
            default:
                if (is_whitespace_next(lexer)) {
                    if (queue.Reference) {
                        if (queue.Reference != qcount) {
                            return false;
                        }
                        queue.Reference = 0;
                        qcount--;
                    }
                }

        }
        current_char = lexer->lookahead;
        col++;
        lexer->advance(lexer, false);
    }
    // we know that this next symbol is valid for an emphasis.
    // if any other scans is still open, this may be invalid
    if (other_char_count) {
        return false;
    }

    // We do not mark the ending as this is a helper function for the
    // Start symbol which was already marked.
    // simply return true and update the range
    if (lexer->lookahead == char_) {
        // we have reached a valid emphasis
        if (row == 0) {
            col = lexer->get_column(lexer);
        } else {
            col--;
        }
        range->row += row;
        range->col = col;
        // lexer->advance(lexer, false);
        return true;
    }
    return false;
}

bool tree_sitter_quarto_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  ScannerState *state = (ScannerState *)payload;
  print_scanner_state(state);
  fprintf(stderr, "scanner invoked before: %c - is alpha: %i\n", lexer->lookahead, isalnum((int)lexer->lookahead));
  if (valid_symbols[ERROR]) {
      fprintf(stderr, "ERROR is a valid symbol\n");
      return false;
  }
  // Skip whitespace
  bool skipped_whitespace = false;
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
    skipped_whitespace = true;
    lexer->advance(lexer, true);
  }

  // fprintf(stderr, "scanner invoked... next char %c\n", lexer->lookahead);
  // Detect a newline
  if (lexer->lookahead == '\n' && valid_symbols[LINE_END]) {
    if (state->emphasis.within) {
        state->emphasis.row--;
    }
    lexer->advance(lexer, false); // Consume the newline
    lexer->result_symbol = LINE_END; // Emit the LINE_END token
    return true;
  }



  // Detect emphasis start
  if (!state->emphasis.within &&
      ((lexer->lookahead == '*' && valid_symbols[EMPHASIS_STAR_START]) ||
          (lexer->lookahead == '_' && valid_symbols[EMPHASIS_UNDER_START]))) {

    if (valid_symbols[EMPHASIS_UNDER_START] && !skipped_whitespace) {
        return false;
    }
    int32_t emph_char = lexer->lookahead;
    fprintf(stderr, "found a %c to begin at emphasis\n", emph_char);
    lexer->advance(lexer, false); // Consume the '*'
    lexer->mark_end(lexer); // this is potentially a emphasis start
    if (is_whitespace_next(lexer)) {
        // two stars back to back are not an emphasis
        // having space between a start and a word is not an valid emphasis
        return false;
    }
    //advance the lexer
    if (find_end_emph(lexer, emph_char, &state->emphasis)) {
        // note if returning true, state->emphasis has definitly
        // changed its range values.

        // make sure the next symbol isn't another of the
        // same type... this could imply a double emphasis
        switch (emph_char) {
            case '*':
                while (lexer->lookahead == emph_char) {
                    lexer->advance(lexer, true);
                    if (!find_end_emph(lexer, emph_char, &state->emphasis)) {
                        state->emphasis.row = 0;
                        return false;
                    }
                }
                break;
            case '_':
                while (!is_whitespace_next(lexer)) {
                    lexer->advance(lexer, true);
                    if (!find_end_emph(lexer, emph_char, &state->emphasis)) {
                        state->emphasis.row = 0;
                        return false;
                    }
                }
                break;
        }

        if (emph_char == '*') {
            lexer->result_symbol = EMPHASIS_STAR_START; // Emit the EMPHASIS_STAR_START token
        } else {
            lexer->result_symbol = EMPHASIS_UNDER_START;
        }
        state->emphasis.within = true; // Update the state
        return true;
    }
  }

  // Detect emphasis end
  if (state->emphasis.within &&
      state->emphasis.row == 0 &&
      ((lexer->lookahead == '*' && valid_symbols[EMPHASIS_STAR_END]) ||
          (lexer->lookahead == '_' && valid_symbols[EMPHASIS_UNDER_END]))) {
    // fprintf(stderr, "looking for end star, detected whitespace before %i\n", skipped_whitespace);
    fprintf(stderr, "looking for ending - calculating column from lexer\n");
    int32_t emph_char = lexer->lookahead;
    if (lexer->get_column(lexer) == state->emphasis.col) {
        lexer->advance(lexer, false);
        lexer->mark_end(lexer);
        if (emph_char == '*') {
            lexer->result_symbol = EMPHASIS_STAR_END; // Emit the EMPHASIS_STAR_END token
        } else {
            lexer->result_symbol = EMPHASIS_UNDER_END;
        }
        state->emphasis.col = 0;
        state->emphasis.within = false; // Update the state
        return true;
    }

    // if (skipped_whitespace) {
    //     return false;
    // }
    // lexer->advance(lexer, false);
    // if (lexer->lookahead == emph_char || (
    //     valid_symbols[EMPHASIS_UNDER_END] && !is_whitespace_next(lexer)
    // )) {
    //     return false;
    // } else {
    //     lexer->mark_end(lexer);
    //     if (emph_char == '*') {
    //         lexer->result_symbol = EMPHASIS_STAR_END; // Emit the EMPHASIS_STAR_END token
    //     } else {
    //         lexer->result_symbol = EMPHASIS_UNDER_END;
    //     }
    //     state->emphasis.within = false; // Update the state
    //     return true;
    // }

  }

  return false; // No token recognized
}
