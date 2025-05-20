#include <stdint.h>
#include <tree_sitter/parser.h>
#include <stdbool.h>
#include <stdio.h>

enum TokenType {
  LINE_END,        // Token type for line_end
  EMPHASIS_STAR_START,  // Token type for emphasis_start
  EMPHASIS_STAR_END,     // Token type for emphasis_end
  EMPHASIS_UNDER_START,
  EMPHASIS_UNDER_END,
  ERROR, //General Emphasis
};

typedef struct {
  bool in_emphasis; // State to track if we're inside an emphasis block
} ScannerState;

void *tree_sitter_quarto_external_scanner_create() {
  ScannerState *state = (ScannerState *)malloc(sizeof(ScannerState));
  state->in_emphasis = false; // Initialize the state
  return state;
}

void tree_sitter_quarto_external_scanner_destroy(void *payload) {
  free(payload); // Free the allocated state
}

unsigned tree_sitter_quarto_external_scanner_serialize(void *payload, char *buffer) {
  ScannerState *state = (ScannerState *)payload;
  buffer[0] = state->in_emphasis ? 1 : 0; // Serialize the state
  return 1; // Return the number of bytes written
}

void tree_sitter_quarto_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  if (length > 0) {
    ScannerState *state = (ScannerState *)payload;
    state->in_emphasis = buffer[0] == 1; // Deserialize the state
  }
}

static bool is_whitespace(int32_t char_) {
    return char_ == ' ' || char_ == '\t' || char_ == '\n';
}
static bool is_whitespace_next(TSLexer *lexer) {
    return is_whitespace(lexer->lookahead);
}

static bool is_static_at_symbol(TSLexer *lexer, int32_t current_char) {
    return lexer->lookahead == '@' && is_whitespace(current_char);
}

static bool valid_prior_emph_end(TSLexer *lexer, int32_t current_char, int32_t emph_char, bool *in_citation) {
    bool whitespace_next = is_whitespace_next(lexer);
    bool detect_at_symbol = emph_char == '_' && is_static_at_symbol(lexer, current_char);
    if (whitespace_next) {
        *in_citation = false;
    } else if (detect_at_symbol) {
        *in_citation = true;
    }
    if (whitespace_next ||  *in_citation ||
        lexer->lookahead == '_' ||
        lexer->lookahead == '*') {

        return false;
    }

    return true;
}

// advances the lexer until we find
// another star that would be valid
// closing star.
// a result of true indicates the lexer
// has just consumed that valid closing star
//
// a result of false indicates the lexer
// failed to find a valid closing star.
static bool find_end_emph(TSLexer *lexer, int32_t char_) {
    fprintf(stderr, "finding end star\n");
    uint8_t new_line_count = 0;
    bool valid_prior_char = true;
    int32_t current_char = char_;
    bool in_citation = false;
    while (lexer->lookahead != '\0' &&
        (lexer->lookahead != char_ || !valid_prior_char)) {
            // fprintf(stderr, "target_char: %c - current_char: %c - next char: %c - valid_prior: %i - in_citation: %i",
            //     char_, current_char, lexer->lookahead, valid_prior_char, in_citation);
        // marks the next character to be valid or invalid
        // if the prior char is marked invalid and the lookahead
        // is a '*', then we continue the loop.
        valid_prior_char = valid_prior_emph_end(lexer, current_char, char_, &in_citation);
        if (lexer->lookahead == '\n') {
            new_line_count++;
            if (new_line_count > 1) {
                return false;
            }
        } else if (lexer->lookahead == '\\') {
            // assuming that this is an emphasis block.
            // advance and the next lexer->advance will
            // treat it as a literal. Note that '\\' is
            // a valid prior char and thus anyting we
            // move past will be considered valid.
            lexer->advance(lexer, false);
        }
        current_char = lexer->lookahead;
        lexer->advance(lexer, false);
    }
    // we know that this star is a valid closing symbol for our intial
    // star.
    // We do not mark the ending as this is a helper function. simply
    // return true
    if (lexer->lookahead == char_) {
        // we have reached a star
        lexer->advance(lexer, false);
        return true;
    }
    return false;
}

bool tree_sitter_quarto_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  ScannerState *state = (ScannerState *)payload;

  fprintf(stderr, "scanner invoked before: %c\n", lexer->lookahead);
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
    lexer->advance(lexer, false); // Consume the newline
    lexer->result_symbol = LINE_END; // Emit the LINE_END token
    return true;
  }



  // Detect emphasis start
  if (!state->in_emphasis &&
      ((lexer->lookahead == '*' && valid_symbols[EMPHASIS_STAR_START]) ||
          (lexer->lookahead == '_' && valid_symbols[EMPHASIS_UNDER_START]))) {

    int32_t emph_char = lexer->lookahead;
    fprintf(stderr, "found a %c to begin at emphasis\n", emph_char);
    lexer->advance(lexer, false); // Consume the '*'
    lexer->mark_end(lexer); // this is potentially a emphasis start
    if (lexer->lookahead == emph_char || is_whitespace_next(lexer)) {
        // two stars back to back are not an emphasis
        // having space between a start and a word is not an valid emphasis
        return false;
    }
    //advance the lexer
    if (find_end_emph(lexer, emph_char)) {
        // make sure the next symbol isn't another of the
        // same type... this could imply a double emphasis
        while (lexer->lookahead == emph_char) {
            lexer->advance(lexer, true);
            if (!find_end_emph(lexer, emph_char)) {
                return false;
            }
        }
        if (emph_char == '*') {
            lexer->result_symbol = EMPHASIS_STAR_START; // Emit the EMPHASIS_STAR_START token
        } else {
            lexer->result_symbol = EMPHASIS_UNDER_START;
        }
        state->in_emphasis = true; // Update the state
        return true;
    }
  }

  // Detect emphasis end
  if (state->in_emphasis &&
      ((lexer->lookahead == '*' && valid_symbols[EMPHASIS_STAR_END]) ||
          (lexer->lookahead == '_' && valid_symbols[EMPHASIS_UNDER_END]))) {
    // fprintf(stderr, "looking for end star, detected whitespace before %i\n", skipped_whitespace);
    int32_t emph_char = lexer->lookahead;
    if (skipped_whitespace) {
        return false;
    }
    lexer->advance(lexer, false);
    if (lexer->lookahead == emph_char) {
        return false;
    } else {
        lexer->mark_end(lexer);
        if (emph_char == '*') {
            lexer->result_symbol = EMPHASIS_STAR_END; // Emit the EMPHASIS_STAR_END token
        } else {
            lexer->result_symbol = EMPHASIS_UNDER_END;
        }
        state->in_emphasis = false; // Update the state
        return true;
    }

  }

  return false; // No token recognized
}
