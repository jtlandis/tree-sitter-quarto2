#include <stdint.h>
#include "tree_sitter/parser.h"
#include "tree_sitter/array.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>

size_t not_found = SIZE_MAX;

enum TokenType {
  LINE_END,        // Token type for line_end
  EMPHASIS_STAR_START,  // Token type for emphasis_start
  EMPHASIS_STAR_END,     // Token type for emphasis_end
  EMPHASIS_UNDER_START,
  EMPHASIS_UNDER_END,
  STRONG_STAR_START,
  STRONG_STAR_END,
  NO_PARSE,
  ERROR, //General Emphasis
};

enum ParseToken {
    NONE,
    DO_NOT_PARSE,
    EMPHASIS_STAR,
    EMPHASIS_UNDER,
    STRONG_STAR,
    STRONG_UNDER,
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

typedef struct Pos {
    uint32_t row;
    uint32_t col;
} Pos;

typedef struct Range {
    Pos start;
    Pos end;
} Range;

enum RangeType {
    DISJOINT_LESS,
    OVERLAP,
    CHILD,
    PARENT,
    DISJOINT_GREATER
};

typedef struct LexWrap {
    TSLexer *lexer;
    Pos init_pos;
    uint32_t pos;
    Array(int32_t) buffer;
    Array(uint32_t) new_line_loc;
} LexWrap;


static LexWrap new_lexer(TSLexer *lexer, Pos init_pos) {
    LexWrap obj;
    obj.lexer = lexer;
    obj.init_pos = init_pos;
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



static Pos new_position(uint32_t row, uint32_t col) {
    Pos obj;
    obj.row = row;
    obj.col = col;
    return obj;
}

static bool pos_eq(Pos *x, Pos *y) {
    return (x->row == y->row) && (x->col == y->col);
}

static bool pos_ne(Pos *x, Pos *y) {
    return (x->row != y->row) || (x->col != y->col);
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

static Pos lex_current_position(LexWrap *wrapper) {
    Pos range = new_position(wrapper->init_pos.row, wrapper->init_pos.col + wrapper->pos);
    if (wrapper->new_line_loc.size > 0) {
        uint32_t diff;
        uint32_t last_index = 0;
        uint32_t i = 0;
        uint32_t line_index = *array_get(&wrapper->new_line_loc, i);
        while(line_index < wrapper->pos
            && i < wrapper->new_line_loc.size) {
            range.row++;
            diff = line_index - last_index;
            last_index = line_index;
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
    Range range;
    enum ParseToken token;
} ParseResult;

static Range new_range(Pos start, Pos end) {
    Range obj;
    obj.start = start;
    obj.end = end;
    return obj;
}

static ParseResult new_parse_result() {
    ParseResult obj;
    obj.success = false;
    obj.length = 0;
    obj.range = new_range(new_position(0, 0), new_position(0, 0));
    obj.token = NONE;
    return obj;
}

typedef Array(ParseResult) ParseResultArray;
typedef Array(uint32_t) IndexArray;

static bool pos_within_range(Pos *x, Range *y) {
    return pos_le(x, &y->end) && pos_ge(x, &y->start);
}

/// x:   |----|
/// y:  |-------|
static bool range_within(Range *x, Range *y) {
    return pos_gt(&x->end, &y->start) && pos_lt(&x->start, &y->end);
}

/// x: |---|
/// y:       |----|
static bool range_disjoint(Range *x, Range *y) {
    return pos_ge(&y->start, &x->end) ||  pos_ge(&x->start, &y->end);
}

static enum RangeType classify_range(Range *x, Range *y) {
    // |----|
    //        |----|

    if (pos_le(&x->end, &y->start)) {
        return DISJOINT_LESS;
    }
    if (pos_lt(&x->end, &y->end)) {
        if (pos_gt(&x->start, &y->start)) {
            return CHILD;
        } else {
            return OVERLAP;
        }
    } else {
        if (pos_lt(&x->start, &y->start)) {
            return PARENT;
        }

        if (pos_lt(&x->start, &y->end)) {
            return OVERLAP;
        } else {
            return DISJOINT_GREATER;
        }
    }

}


static size_t stack_insert(ParseResultArray* array, ParseResult element) {
    if (array->size == 0) {
        array_push(array, element);
        return 0;
    } else {
        for (size_t i = 0; i < array->size; i++) {
            ParseResult *result = &array->contents[i];
            switch (classify_range(&element.range, &result->range)) {
                case OVERLAP: {
                    return not_found;
                }
                case PARENT: {
                    array_insert(array, i, element);
                    return i;
                }
                case DISJOINT_LESS: {
                    array_insert(array, i, element);
                    return i;
                }
                case DISJOINT_GREATER: {
                    array_insert(array, i + 1, element);
                    return i + 1;
                }
                case CHILD: {
                    continue;
                }
            }
        }
    }
    return not_found;

}



static size_t stack_find(ParseResultArray *array, Pos *pos, enum ParseToken token, bool end) {
    ParseResult *element;
    if (end) {
        for (size_t i = 0; i < array->size; i++) {
            element = &array->contents[i];
            if (element->token == token && pos_eq(&element->range.end, pos)) {
                return i;
            }
        }
    } else {
        for (size_t i = 0; i < array->size; i++) {
            element = &array->contents[i];
            if (element->token == token && pos_eq(&element->range.start, pos)) {
                return i;
            }
        }
    }
    return not_found;
}

static size_t stack_find_within(ParseResultArray *array, Pos *pos, enum ParseToken token) {
     ParseResult *element;
    for (size_t i = 0; i < array->size; i++) {
        element = &array->contents[i];
        if (element->token == token && pos_within_range(pos, &element->range)) {
            return i;
        }
    }
    return not_found;
}

static size_t stack_find_exact(ParseResultArray *array,  ParseResult *res) {
    ParseResult *element;
   for (size_t i = 0; i < array->size; i++) {
       element = &array->contents[i];
       if (element->token == res->token &&
           pos_eq(&element->range.start, &res->range.start) &&
           pos_eq(&element->range.end, &res->range.end)) {
           return i;
       }
   }
   return not_found;
}

static void print_pos(const Pos *pos) {
    fprintf(stderr, "Pos { row: %u, col: %u }", pos->row, pos->col);
}

static void print_parse_result(const ParseResult *res) {
    fprintf(stderr, "ParseResult { success: %d, length: %u, range: ", res->success, res->length);
    fprintf(stderr, "[%i, %i] - ", res->range.start.row, res->range.start.col);
    fprintf(stderr, "[%i, %i]", res->range.end.row, res->range.end.col);
    fprintf(stderr, ", token: %d }\n", res->token);
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
static ParseResult parse_under(LexWrap *wrapper, ParseResultArray* stack);

static ParseResult parse_inline(LexWrap *wrapper, ParseResultArray* stack) {
    uint32_t stack_start_size = stack->size;
    uint32_t buffer_start_pos = wrapper->pos;
    ParseResult res = new_parse_result();
    res.range.start = lex_current_position(wrapper);
    int32_t lookahead = lex_lookahead(wrapper);
    switch (lookahead) {
        case '*':
            res = parse_star(wrapper, stack);
            break;
    }
    return res;
}

static ParseResult parse_star(LexWrap *wrapper, ParseResultArray* stack) {
    fprintf(stderr, "calling - parse_star()\n");
    uint32_t stack_start_size = stack->size;
    uint32_t buffer_start_pos = wrapper->pos;
    ParseResult res = new_parse_result();
    res.range.start = lex_current_position(wrapper);

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
    }
    if (char_count > 3) {
        // as a special feature, we insert this into
        // the stack to signal that it should not match
        // any symbols

        res.success = true;
        res.range.end = lex_current_position(wrapper);
        res.length = char_count;
        res.token = DO_NOT_PARSE;
        wrapper->lexer->mark_end(wrapper->lexer);
        fprintf(stderr, "returning a NO_PARSE result:");
        print_parse_result(&res);
        fprintf(stderr, "\n");
        stack_insert(stack, res);
        return res;
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
                        uint32_t end_pos = wrapper->pos;
                        res.range.end = lex_current_position(wrapper);
                        res.success = true;
                        res.token = EMPHASIS_STAR;
                        res.length = wrapper->pos - buffer_start_pos;
                        // note that the lexer has pushed past  all the stars
                        // but we are now in a backtracking state.
                        if (end_char_count > 1) {
                            // if we parse further and find the end result
                            // is EMPHASIS_STAR, invalidate
                            ParseResult attempt = parse_star(wrapper, stack);
                            if (attempt.success && attempt.token == EMPHASIS_STAR) {
                                size_t index = stack_find(stack, &attempt.range.start, EMPHASIS_STAR, false);
                                ParseResult *stack_res = &stack->contents[index];
                                if (pos_eq(&attempt.range.end, &stack_res->range.end)) {
                                    // remove it from the stack
                                    array_erase(stack, index);
                                    // isert special - "no parse" tokens
                                    ParseResult emph_start = new_parse_result();
                                    emph_start.token = DO_NOT_PARSE;
                                    emph_start.range.start = attempt.range.start;
                                    emph_start.range.end = emph_start.range.start;
                                    emph_start.length = 1;
                                    emph_start.range.end.col++;
                                    fprintf(stderr, "attempting to add DO_NOT_PARSE: ");
                                    print_parse_result(&emph_start);
                                    stack_insert(stack, emph_start);
                                    ParseResult emph_end = new_parse_result();
                                    emph_end.token = DO_NOT_PARSE;
                                    emph_end.range.end = attempt.range.end;
                                    emph_end.range.start = emph_end.range.end;
                                    emph_end.range.start.col--;
                                    emph_end.length = 1;
                                    fprintf(stderr, "attempting to add DO_NOT_PARSE: ");
                                    print_parse_result(&emph_end);
                                    stack_insert(stack, emph_end);
                                }
                                ParseResult emph_end = new_parse_result();
                                emph_end.token = DO_NOT_PARSE;
                                emph_end.range.start = res.range.end;
                                emph_end.range.end = emph_end.range.start;
                                emph_end.range.start.col--;
                                emph_end.length = 1;
                                stack_insert(stack, emph_end);
                                res.token = DO_NOT_PARSE;
                                res.range.end = res.range.start;
                                res.range.end.col++;
                                res.success = true;
                            } else {
                                lex_backtrack_n(wrapper, wrapper->pos - end_pos);
                            }
                        }
                        if (stack_insert(stack, res) == not_found) {
                            res.success = false;
                        }
                        fprintf(stderr, "returning: ");
                        print_parse_result(&res);
                        return res;
                        break;
                    }
                    case 2: {
                        switch (end_char_count) {
                            case 1: {
                                // this may invalidate our current  scope
                                lex_backtrack_n(wrapper, 1);
                                ParseResult attempt = parse_star(wrapper, stack);
                                // if it wasn't successful, then this token will
                                // also not be successful
                                if (!attempt.success) {
                                    fprintf(stderr, "returning failure: ");
                                    print_parse_result(&res);
                                    return res;
                                }
                                fprintf(stderr, "successful internal parse... lexer at: ");
                                Pos _pos = lex_current_position(wrapper);
                                print_pos(&_pos);
                                lookahead = lex_lookahead(wrapper);
                                continue;
                            }
                            default: {
                                // no matter how many match here. we have
                                // reached our target.
                                lex_backtrack_n(wrapper, end_char_count - 2);
                                res.range.end = lex_current_position(wrapper);
                                res.success = true;
                                res.token = STRONG_STAR;
                                res.length = wrapper->pos - buffer_start_pos;
                                // note that the lexer has pushed past  all the stars
                                // but we are now in a backtracking state.
                                if (stack_insert(stack, res) == not_found) {
                                    res.success = false;
                                }
                                fprintf(stderr, "returning: ");
                                print_parse_result(&res);
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
                                inner.range.end = lex_current_position(wrapper);
                                inner.range.start = res.range.start;
                                inner.range.start.col += 2;
                                inner.success = true;
                                inner.token = EMPHASIS_STAR;
                                inner.length = wrapper->pos - buffer_start_pos - 2;
                                if (stack_insert(stack, inner) < not_found) {
                                    char_count--;
                                }
                                break;
                            }
                            case 2: {
                                // inner syntax is an strong and outer is
                                // likely a emph.
                                // create new result to insert
                                ParseResult inner = new_parse_result();
                                inner.range.end = lex_current_position(wrapper);
                                inner.range.start = res.range.start;
                                inner.range.start.col += 1;
                                inner.success = true;
                                inner.token = STRONG_STAR;
                                inner.length = wrapper->pos - buffer_start_pos - 1;
                                if (stack_insert(stack, inner) < not_found) {
                                    char_count -= 2;
                                }
                                break;
                            }
                            default: {
                                // no matter how many times we detected
                                // a '*'
                                // we have matched our stack!
                                lex_backtrack_n(wrapper, end_char_count - 3);
                                res.range.end = lex_current_position(wrapper);
                                res.success = true;
                                res.token = STRONG_STAR;
                                res.length = wrapper->pos - buffer_start_pos;
                                // inner will be an emphasis
                                ParseResult inner = new_parse_result();
                                inner.range.end = lex_current_position(wrapper);
                                inner.range.end.col -= 2;
                                inner.range.start = res.range.start;
                                inner.range.start.col += 2;
                                inner.success = true;
                                inner.token = EMPHASIS_STAR;
                                inner.length = wrapper->pos - buffer_start_pos - 2;
                                size_t index = stack_insert(stack, inner);
                                if (index < not_found) {
                                    array_insert(stack, index, res);
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
        lex_advance(wrapper, false);
        lookahead = lex_lookahead(wrapper);
    }

    return res;

}

static ParseResult parse_under(LexWrap *wrapper, ParseResultArray* stack) {
    fprintf(stderr, "calling - parse_under()\n");
    uint32_t stack_start_size = stack->size;
    uint32_t buffer_start_pos = wrapper->pos;
    ParseResult res = new_parse_result();
    res.range.start = lex_current_position(wrapper);

    /// for this parse to be valid one of
    /// if we detect 1 --> expecting emphasis
    /// if we detect 2 --> expecting strong
    /// if we detect 3 --> expecting combo of emphasis or strong
    ///                    with the possibility of either ending
    ///                    early.
    /// > 3 --> return false
    uint8_t char_count = 0;
    while (lex_lookahead(wrapper) == '_') {
        lex_advance(wrapper, false);
        char_count++;
    }
    if (char_count > 3) {
        // as a special feature, we insert this into
        // the stack to signal that it should not match
        // any symbols

        res.success = true;
        res.range.end = lex_current_position(wrapper);
        res.length = char_count;
        wrapper->lexer->mark_end(wrapper->lexer);
        fprintf(stderr, "returning a NONE result:");
        print_parse_result(&res);
        fprintf(stderr, "\n");
        stack_insert(stack, res);
        return res;
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
            case '_': {
                // see how many we can consume
                end_char_count = 0;
                while (lex_lookahead(wrapper) == '_') {
                    lex_advance(wrapper, false);
                    end_char_count++;
                }
                switch (char_count) {
                    case 1: {
                        // we only have one left to match...
                        // no matter the size of end_char_count
                        lex_backtrack_n(wrapper, end_char_count - 1);
                        uint32_t end_pos = wrapper->pos;
                        res.range.end = lex_current_position(wrapper);
                        res.success = true;
                        res.token = EMPHASIS_UNDER;
                        res.length = wrapper->pos - buffer_start_pos;
                        // note that the lexer has pushed past  all the underscores
                        // but we are now in a backtracking state.
                        if (end_char_count > 1) {
                            // if we parse further and find the end result
                            // is EMPHASIS_STAR, invalidate
                            ParseResult attempt = parse_star(wrapper, stack);
                            if (attempt.success && attempt.token == EMPHASIS_STAR) {
                                res.success = false;
                            } else {
                                lex_backtrack_n(wrapper, wrapper->pos - end_pos);
                            }
                        }
                        if (stack_insert(stack, res)==not_found) {
                            res.success = false;
                        }
                        fprintf(stderr, "returning: ");
                        print_parse_result(&res);
                        return res;
                        break;
                    }
                    case 2: {
                        switch (end_char_count) {
                            case 1: {
                                // this may invalidate our current  scope
                                lex_backtrack_n(wrapper, 1);
                                ParseResult attempt = parse_star(wrapper, stack);
                                // if it wasn't successful, then this token will
                                // also not be successful
                                if (!attempt.success) {
                                    fprintf(stderr, "returning failure: ");
                                    print_parse_result(&res);
                                    return res;
                                }
                                fprintf(stderr, "successful internal parse... lexer at: ");
                                Pos _pos = lex_current_position(wrapper);
                                print_pos(&_pos);
                                lookahead = lex_lookahead(wrapper);
                                continue;
                            }
                            default: {
                                // no matter how many match here. we have
                                // reached our target.
                                lex_backtrack_n(wrapper, end_char_count - 2);
                                res.range.end = lex_current_position(wrapper);
                                res.success = true;
                                res.token = STRONG_STAR;
                                res.length = wrapper->pos - buffer_start_pos;
                                // note that the lexer has pushed past  all the stars
                                // but we are now in a backtracking state.
                                if (stack_insert(stack, res)==not_found) {
                                    res.success = false;
                                }
                                fprintf(stderr, "returning: ");
                                print_parse_result(&res);
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
                                inner.range.end = lex_current_position(wrapper);
                                inner.range.start = res.range.start;
                                inner.range.start.col += 2;
                                inner.success = true;
                                inner.token = EMPHASIS_STAR;
                                inner.length = wrapper->pos - buffer_start_pos - 2;
                                if (stack_insert(stack, inner) < not_found) {
                                    char_count--;
                                }
                                break;
                            }
                            case 2: {
                                // inner syntax is an strong and outer is
                                // likely a emph.
                                // create new result to insert
                                ParseResult inner = new_parse_result();
                                inner.range.end = lex_current_position(wrapper);
                                inner.range.start = res.range.start;
                                inner.range.start.col += 1;
                                inner.success = true;
                                inner.token = STRONG_STAR;
                                inner.length = wrapper->pos - buffer_start_pos - 1;
                                if (stack_insert(stack, inner) < not_found) {
                                    char_count -= 2;
                                }
                                break;
                            }
                            default: {
                                // no matter how many times we detected
                                // a '*'
                                // we have matched our stack!
                                lex_backtrack_n(wrapper, end_char_count - 3);
                                res.range.end = lex_current_position(wrapper);
                                res.success = true;
                                res.token = STRONG_STAR;
                                res.length = wrapper->pos - buffer_start_pos;
                                // inner will be an emphasis
                                ParseResult inner = new_parse_result();
                                inner.range.end = lex_current_position(wrapper);
                                inner.range.end.col -= 2;
                                inner.range.start = res.range.start;
                                inner.range.start.col += 2;
                                inner.success = true;
                                inner.token = EMPHASIS_STAR;
                                inner.length = wrapper->pos - buffer_start_pos - 2;
                                size_t index = stack_insert(stack, inner);
                                if (index < not_found) {
                                    array_insert(stack, index, res);
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
        lex_advance(wrapper, false);
        lookahead = lex_lookahead(wrapper);
    }

    return res;

}



typedef struct {
  Pos pos;
  ParseResultArray results; // State to track if we're inside an emphasis block
} ScannerState;

static void print_scanner_state(const ScannerState *state) {
    fprintf(stderr, "ScannerState {\n  pos: ");
    print_pos(&state->pos);
    fprintf(stderr, "\n  results (size: %u):\n", state->results.size);
    for (uint32_t i = 0; i < state->results.size; i++) {
        fprintf(stderr, "\t");
        print_parse_result(&state->results.contents[i]);
    }
    fprintf(stderr, "}\n");
}

static void print_lexwrap(const LexWrap *wrap) {
    fprintf(stderr, "LexWrap {\n  init_pos: [%u, %u]\n  pos: %u\n", wrap->init_pos.row,
        wrap->init_pos.col, wrap->pos);
    fprintf(stderr, "  buffer (size: %u)\n", wrap->buffer.size);
    fprintf(stderr, " new_line_loc (size: %u): [", wrap->new_line_loc.size);
    for (uint32_t i = 0; i < wrap->new_line_loc.size; i++) {
        fprintf(stderr, "%u", wrap->new_line_loc.contents[i]);
        if (i + 1 < wrap->new_line_loc.size) fprintf(stderr, ", ");
    }
    fprintf(stderr, "]\n}\n");
}


void *tree_sitter_quarto_external_scanner_create() {
  // fprintf(stderr, "attempting to create scanner... ");
  ScannerState *state = (ScannerState *)malloc(sizeof(ScannerState));
  state->pos = new_position(0, 0);
  array_init(&state->results); // Initialize the state
  // fprintf(stderr, "returning scanner\n");
  return state;
}

void tree_sitter_quarto_external_scanner_destroy(void *payload) {
  // fprintf(stderr, "attempting to destroy scanner... ");
  ScannerState *state = (ScannerState *)payload;
  array_delete(&state->results); // Free the heap memory used by the array
  free(payload); // Free the allocated state
  // fprintf(stderr, "freeing memory and exiting\n");
}

unsigned tree_sitter_quarto_external_scanner_serialize(void *payload, char *buffer) {
  // fprintf(stderr, "attempting to serialize scanner... ");
  ScannerState *state = (ScannerState *)payload;
  size_t offset = 0;
  // get the position
  memcpy(buffer + offset, &state->pos.row, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(buffer + offset, &state->pos.col, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  // Serialize results array size
  memcpy(buffer + offset, &state->results.size, sizeof(uint32_t));
  offset += sizeof(uint32_t);

  // Serialize each ParseResult
  for (uint32_t i = 0; i < state->results.size; i++) {
      ParseResult *res = &state->results.contents[i];
      memcpy(buffer + offset, res, sizeof(ParseResult));
      offset += sizeof(ParseResult);
  }
  // fprintf(stderr, "%zu bytes written... \n", offset);
  return offset;
}

void tree_sitter_quarto_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    // fprintf(stderr, "attempting to deserialize scanner... \n");
    if (!payload || !buffer) {
        // fprintf(stderr, "Null pointer in deserialize!\n");
        return;
    }
    if (length < sizeof(uint32_t)) {
        // fprintf(stderr, "Buffer too small in deserialize!\n");
        return;
    }
    ScannerState *state = (ScannerState *)payload;
    size_t offset = 0;

    // fprintf(stderr, "writing row bits... ");
    memcpy(&state->pos.row, buffer + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    // fprintf(stderr, "writing col bits... ");
    memcpy(&state->pos.col, buffer + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    // fprintf(stderr, "writing array size bits... ");
    // Deserialize results array size
    uint32_t arr_size = 0;
    memcpy(&arr_size, buffer + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    // fprintf(stderr, "reserving array size... ");
    array_clear(&state->results);
    array_reserve(&state->results, arr_size);
    state->results.size = arr_size;

    // fprintf(stderr, "attempting to pull buffer info of %i elements... ", arr_size);
    // Deserialize each ParseResult
    for (uint32_t i = 0; i < arr_size; i++) {
      memcpy(&state->results.contents[i], buffer + offset, sizeof(ParseResult));
      offset += sizeof(ParseResult);
    }
    // fprintf(stderr, "exiting from deserializing function... \n");
}




static int32_t other_emphasis(int32_t char_) {
    if (char_=='*') {
        return '_';
    }
    return '*';
}

static void print_valid_symbols(const bool *valid_symbols) {
    fprintf(stderr, "valid_symbols: [");
    fprintf(stderr, "LINE_END=%d, ", valid_symbols[LINE_END]);
    fprintf(stderr, "EMPHASIS_STAR_START=%d, ", valid_symbols[EMPHASIS_STAR_START]);
    fprintf(stderr, "EMPHASIS_STAR_END=%d, ", valid_symbols[EMPHASIS_STAR_END]);
    fprintf(stderr, "EMPHASIS_UNDER_START=%d, ", valid_symbols[EMPHASIS_UNDER_START]);
    fprintf(stderr, "EMPHASIS_UNDER_END=%d, ", valid_symbols[EMPHASIS_UNDER_END]);
    fprintf(stderr, "STRONG_STAR_START=%d, ", valid_symbols[STRONG_STAR_START]);
    fprintf(stderr, "STRONG_STAR_END=%d, ", valid_symbols[STRONG_STAR_END]);
    fprintf(stderr, "NO_PARSE=%d, ", valid_symbols[NO_PARSE]);
    fprintf(stderr, "ERROR=%d", valid_symbols[ERROR]);
    fprintf(stderr, "]\n");
}

bool tree_sitter_quarto_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {


  ScannerState *state = (ScannerState *)payload;
  print_scanner_state(state);
  fprintf(stderr, "scanner invoked before: %c - is alpha: %i\n",
      lexer->lookahead, isalnum((int)lexer->lookahead));
  print_valid_symbols(valid_symbols);
  if (valid_symbols[ERROR]) {
      fprintf(stderr, "ERROR is a valid symbol. do not handle\n");
      // lexer->mark_end(lexer);
      // lexer->result_symbol = ERROR;
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
    state->pos.row++;
    state->pos.col = 0;
    lexer->advance(lexer, false); // Consume the newline
    lexer->result_symbol = LINE_END; // Emit the LINE_END token
    return true;
  }

  // handle NO_PARSE
  if (valid_symbols[NO_PARSE]) {
      state->pos.col = lexer->get_column(lexer);
      size_t index = stack_find(&state->results, &state->pos, DO_NOT_PARSE, false);
      if (index < not_found) {
          ParseResult *res = &state->results.contents[index];
          for (uint32_t i = 0; i < res->length; i++) {
              lexer->advance(lexer, false);
          }
          lexer->mark_end(lexer);
          lexer->result_symbol = NO_PARSE;
          array_erase(&state->results, index);
          return true;
      }

  }

  // detect  star
  if (lexer->lookahead == '*' && (
      valid_symbols[EMPHASIS_STAR_START] ||
      valid_symbols[STRONG_STAR_START] ||
      valid_symbols[EMPHASIS_STAR_END] ||
      valid_symbols[STRONG_STAR_END]
  )) {
      fprintf(stderr, "looking for strong or emph star\n");
      // get current start position
      state->pos.col = lexer->get_column(lexer);
      LexWrap wrapper = new_lexer(lexer, state->pos);
      lex_advance(&wrapper, false);
      // possible end if just an emphasis
      lexer->mark_end(lexer);
      // before we move the lexer forward check
      // if emphasis is valid... The grammar could
      // enable STRONG_STAR_END and EMPH_STAR_END
      // at the same time...
      Pos possible_pos = lex_current_position(&wrapper);
      fprintf(stderr, "lex is at: ");
      print_pos(&possible_pos);
      fprintf(stderr, "\n");
      if (valid_symbols[EMPHASIS_STAR_END]) {
          size_t index = stack_find(&state->results, &possible_pos, EMPHASIS_STAR, true);
          if (index < not_found) {
              lexer->result_symbol = EMPHASIS_STAR_END;
              array_erase(&state->results, index);
              return true;
          }
      }

      if (valid_symbols[EMPHASIS_STAR_START]) {
          // the start position should be one step prior
          possible_pos.col--;
          size_t index = stack_find(&state->results, &possible_pos, EMPHASIS_STAR, false);
          if (index < not_found) {
              lexer->result_symbol = EMPHASIS_STAR_START;
              return true;
          }
          possible_pos.col++;
      }

      // without actually advancing the lexer, check the stack
      if (valid_symbols[STRONG_STAR_START] || valid_symbols[STRONG_STAR_END]) {
          possible_pos.col++;
          if (valid_symbols[STRONG_STAR_END]) {
              size_t index = stack_find(&state->results, &possible_pos, STRONG_STAR, true);
              if (index < not_found) {
                  lex_advance(&wrapper, false);
                  lexer->mark_end(lexer);
                  lexer->result_symbol = STRONG_STAR_END;
                  array_erase(&state->results, index);
                  return true;
              }
          }
          if (valid_symbols[STRONG_STAR_START]) {
              //again, the start will be on the other side
              possible_pos.col -= 2;
              size_t index = stack_find(&state->results, &possible_pos, STRONG_STAR, false);
              if (index < not_found) {
                  lex_advance(&wrapper, false);
                  lexer->mark_end(lexer);
                  lexer->result_symbol = STRONG_STAR_START;
                  return true;
              }
              possible_pos.col += 2;
          }
      }

      // failed to match any pre-parsed info on the stack.
      // Its not the time to advance the lexer if STRONG match is possible.
      if (valid_symbols[EMPHASIS_STAR_START] || valid_symbols[STRONG_STAR_START]) {

          if (lexer->lookahead == '*' && valid_symbols[STRONG_STAR_START]) {
              lex_advance(&wrapper, false);
              lexer->mark_end(lexer);
          }
          // reset wrapper to begining of this scan.
          lex_backtrack_n(&wrapper, wrapper.buffer.size);
          // try and handle this parse...
          ParseResult res = parse_star(&wrapper, &state->results);
          if (res.success) {
              if (res.token == NONE) {
                  lexer->result_symbol = ERROR;
                  return true;
              }
              if (res.token == DO_NOT_PARSE) {
                  size_t index = stack_find_exact(&state->results, &res);
                  if (index < not_found) {
                      array_erase(&state->results, index);
                  }
                  lexer->result_symbol = NO_PARSE;
                  return true;
              }
              if (valid_symbols[EMPHASIS_STAR_START] && res.token == EMPHASIS_STAR) {
                  lexer->result_symbol = EMPHASIS_STAR_START;
                  return true;
              } else if (valid_symbols[STRONG_STAR_START] && res.token == STRONG_STAR){
                  lexer->result_symbol = STRONG_STAR_START;
                  return true;
              }
          }
      }

  }


  // detect  underscore
  if (lexer->lookahead == '_' && (
      valid_symbols[EMPHASIS_UNDER_START] ||
      valid_symbols[STRONG_UNDER_START] ||
      valid_symbols[EMPHASIS_UNDER_END] ||
      valid_symbols[STRONG_UNDER_END]
  )) {
      fprintf(stderr, "looking for strong or emph under\n");
      // get current start position
      state->pos.col = lexer->get_column(lexer);
      LexWrap wrapper = new_lexer(lexer, state->pos);
      lex_advance(&wrapper, false);
      // possible end if just an emphasis
      lexer->mark_end(lexer);
      // before we move the lexer forward check
      // if emphasis is valid... The grammar could
      // enable STRONG_STAR_END and EMPH_STAR_END
      // at the same time...
      Pos possible_pos = lex_current_position(&wrapper);
      fprintf(stderr, "lex is at: ");
      print_pos(&possible_pos);
      fprintf(stderr, "\n");
      if (valid_symbols[EMPHASIS_UNDER_END]) {
          size_t index = stack_find(&state->results, &possible_pos, EMPHASIS_UNDER, true);
          if (index < not_found) {
              lexer->result_symbol = EMPHASIS_UNDER_END;
              array_erase(&state->results, index);
              return true;
          }
      }

      if (valid_symbols[EMPHASIS_UNDER_START]) {
          // the start position should be one step prior
          possible_pos.col--;
          size_t index = stack_find(&state->results, &possible_pos, EMPHASIS_UNDER, false);
          if (index < not_found) {
              lexer->result_symbol = EMPHASIS_UNDER_START;
              return true;
          }
          possible_pos.col++;
      }

      // without actually advancing the lexer, check the stack
      if (valid_symbols[STRONG_UNDER_START] || valid_symbols[STRONG_UNDER_END]) {
          possible_pos.col++;
          if (valid_symbols[STRONG_UNDER_END]) {
              size_t index = stack_find(&state->results, &possible_pos, STRONG_UNDER, true);
              if (index < not_found) {
                  lex_advance(&wrapper, false);
                  lexer->mark_end(lexer);
                  lexer->result_symbol = STRONG_UNDER_END;
                  array_erase(&state->results, index);
                  return true;
              }
          }
          if (valid_symbols[STRONG_UNDER_START]) {
              //again, the start will be on the other side
              possible_pos.col -= 2;
              size_t index = stack_find(&state->results, &possible_pos, STRONG_UNDER, false);
              if (index < not_found) {
                  lex_advance(&wrapper, false);
                  lexer->mark_end(lexer);
                  lexer->result_symbol = STRONG_UNDER_START;
                  return true;
              }
              possible_pos.col += 2;
          }
      }

      // failed to match any pre-parsed info on the stack.
      // Its not the time to advance the lexer if STRONG match is possible.
      if (valid_symbols[EMPHASIS_UNDER_START] || valid_symbols[STRONG_UNDER_START]) {

          if (lexer->lookahead == '_' && valid_symbols[STRONG_UNDER_START]) {
              lex_advance(&wrapper, false);
              lexer->mark_end(lexer);
          }
          // reset wrapper to begining of this scan.
          lex_backtrack_n(&wrapper, wrapper.buffer.size);
          // try and handle this parse...
          ParseResult res = parse_under(&wrapper, &state->results);
          if (res.success) {
              if (res.token == NONE) {
                  lexer->result_symbol = ERROR;
                  return true;
              }
              if (res.token == DO_NOT_PARSE) {
                  size_t index = stack_find_exact(&state->results, &res);
                  if (index < not_found) {
                      array_erase(&state->results, index);
                  }
                  lexer->result_symbol = NO_PARSE;
                  return true;
              }
              if (valid_symbols[EMPHASIS_UNDER_START] && res.token == EMPHASIS_UNDER) {
                  lexer->result_symbol = EMPHASIS_UNDER_START;
                  return true;
              } else if (valid_symbols[STRONG_UNDER_START] && res.token == STRONG_UNDER){
                  lexer->result_symbol = STRONG_UNDER_START;
                  return true;
              }
          }
      }

  }

  return false; // No token recognized
}
