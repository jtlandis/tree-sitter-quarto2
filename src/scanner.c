#include <tree_sitter/parser.h>
#include <stdbool.h>
#include <stdio.h>

enum TokenType {
  LINE_END,        // Token type for line_end
  EMPHASIS_START,  // Token type for emphasis_start
  EMPHASIS_END     // Token type for emphasis_end
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

static bool is_whitespace_next(TSLexer *lexer) {
    if (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\n') {
        return true;
    }
    return false;
}

// advances the lexer until we find
// another star that would be valid
// closing star.
// a result of true indicates the lexer
// has just consumed that valid closing star
//
// a result of false indicates the lexer
// failed to find a valid closing star.
static bool find_end_star(TSLexer *lexer) {
    fprintf(stderr, "finding end star\n");
    uint8_t new_line_count = 0;
    bool white_space_prior = false;
    while (lexer->lookahead != '\0' && (lexer->lookahead != '*' || white_space_prior)) {
        fprintf(stderr, "char: %c\n", lexer->lookahead);
        white_space_prior = is_whitespace_next(lexer);
        if (lexer->lookahead == '\n') {
            new_line_count++;
            if (new_line_count > 1) {
                return false;
            }
        } else if (lexer->lookahead == '\\') {
            // assuming that this is an emphasis block.
            // advance and the next lexer->advance will
            // treat it as a literal
            lexer->advance(lexer, false);
        }

        lexer->advance(lexer, false);
    }
    // we know that this star is a valid closing symbol for our intial
    // star.
    // We do not mark the ending as this is a helper function. simply
    // return true
    if (lexer->lookahead == '*') {
        // we have reached a star
        lexer->advance(lexer, false);
        return true;
    }
    return false;
}

bool tree_sitter_quarto_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  ScannerState *state = (ScannerState *)payload;

  // Skip whitespace
  bool skipped_whitespace = false;
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
    skipped_whitespace = true;
    lexer->advance(lexer, true);
  }

  fprintf(stderr, "scanner invoked... next char %c\n", lexer->lookahead);
  // Detect a newline
  if (lexer->lookahead == '\n' && valid_symbols[LINE_END]) {
    lexer->advance(lexer, false); // Consume the newline
    lexer->result_symbol = LINE_END; // Emit the LINE_END token
    return true;
  }

  // Detect emphasis start
  if (lexer->lookahead == '*' && valid_symbols[EMPHASIS_START] && !state->in_emphasis) {
    // fprintf(stderr, "found a star to begin at... ");
    lexer->advance(lexer, false); // Consume the '*'
    lexer->mark_end(lexer); // this is potentially a emphasis start
    if (lexer->lookahead == '*' || is_whitespace_next(lexer)) {
        // two stars back to back are not an emphasis
        // having space between a start and a word is not an valid emphasis
        return false;
    }
    //advance the lexer
    if (find_end_star(lexer)) {
        // make sure the next symbol isn't another star
        while (lexer->lookahead == '*') {
            lexer->advance(lexer, true);
            if (!find_end_star(lexer)) {
                return false;
            }
        }
        lexer->result_symbol = EMPHASIS_START; // Emit the EMPHASIS_START token
        state->in_emphasis = true; // Update the state
        return true;
    }
  }

  // Detect emphasis end
  if (lexer->lookahead == '*' && valid_symbols[EMPHASIS_END] && state->in_emphasis) {
    fprintf(stderr, "looking for end star, detected whitespace before %i\n", skipped_whitespace);
    if (skipped_whitespace) {
        return false;
    }
    lexer->advance(lexer, false);
    if (lexer->lookahead == '*') {
        return false;
    } else {
        lexer->mark_end(lexer);
        lexer->result_symbol = EMPHASIS_END; // Emit the EMPHASIS_START token
        state->in_emphasis = false; // Update the state
        return true;
    }

  }

  return false; // No token recognized
}
