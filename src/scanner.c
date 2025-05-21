#include <stdint.h>
#include <tree_sitter/parser.h>
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


static WithinRange new_range() {
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
  state->emphasis = new_range(); // Initialize the state
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

static bool is_whitespace(int32_t char_) {
    return char_ == ' ' || char_ == '\t' || char_ == '\n';
}
static bool is_whitespace_next(TSLexer *lexer) {
    return is_whitespace(lexer->lookahead);
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

typedef struct {
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
} Queue;

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
    while(lexer->lookahead == other_char) {
        other_char_count++;
        col++;
        lexer->advance(lexer, false);
    }
    // this is immediately after char_
    // and if whitespace follows the other
    // emphasis symbol, this doesnt necessarily
    // invalidate char_ empahsis
    if (is_whitespace_next(lexer)) {
        other_char_count = 0;
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

               }
            case '*':
            case '_':
                if (lexer->lookahead == char_) {
                    // we only reach this branch if char_count == 3
                    // or == 1
                    uint8_t char_consumed = 0;
                    while(lexer->lookahead == char_) {
                        char_consumed++;
                        col++;
                        lexer->advance(lexer, false);
                    }
                    switch (char_count) {
                        case 1:
                          if (char_consumed == 1) {
                              char_count = 0;
                              continue;
                          }
                          if (char_consumed == 2) {
                              char_count += 2;
                              continue;
                          }
                          if (char_consumed > 2) {
                              char_count = 0;
                              char_consumed--;
                              col -= char_consumed;
                              continue;
                          }
                          break;
                        case 3:
                          if (char_consumed > 2) {
                              char_count = 1;
                              continue;
                          }
                          // decrementing by 2 would be the only valid
                          // way for emphasis.
                          return false;
                          break;
                    } 
                    if (3 == char_count + char_consumed) {
                        char_count = 3;
                        continue;
                    } else if ()
                    if (char_consumed == char_count) {
                        // we have found our end point
                        char_count = 0;
                        continue;
                    }
                    if (char_consumed > 3) {
                        continue;
                    }
                    char_count
                    // and lookahead is our specified char.
                    char_count--;
                    col++;
                    lexer->advance(lexer, false);
                    // we must be able to decrement twice
                    if (lexer->lookahead != char_) {
                        return false;
                    }
                    char_count--;
                    // let the loop run as normal
                    // it will escape
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
        }
        // if (lexer->lookahead == char_) {
        //     char_count--;
        // }
        // // valid_prior_char = valid_prior_emph_end(lexer, current_char, char_, &in_citation);
        // if (lexer->lookahead == '\n') {

        // } else if (lexer->lookahead == '\\') {

        // }
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
        lexer->advance(lexer, false);
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
