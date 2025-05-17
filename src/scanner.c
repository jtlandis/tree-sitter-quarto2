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

bool tree_sitter_quarto_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  ScannerState *state = (ScannerState *)payload;

  // Skip whitespace
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
    lexer->advance(lexer, true);
  }

  // Detect a newline
  if (lexer->lookahead == '\n' && valid_symbols[LINE_END]) {
    lexer->advance(lexer, false); // Consume the newline
    lexer->result_symbol = LINE_END; // Emit the LINE_END token
    return true;
  }

  // Detect emphasis start
  if (lexer->lookahead == '*' && valid_symbols[EMPHASIS_START] && !state->in_emphasis) {
    lexer->advance(lexer, false); // Consume the '*'
    lexer->mark_end(lexer); // this is potentially a emphasis start
    if (lexer->lookahead == '*' | lexer->lookahead == ' ') {
        // two stars back to back are not an emphasis
        // having space between a start and a word is not an valid emphasis
        return false;
    }
    uint8_t new_line_count = 0;
    while (lexer->lookahead != '*' && lexer->lookahead != '\0') {
        if (lexer->lookahead == '\n') {
            new_line_count++;
        }
        if (new_line_count > 1) {
            return false;
        }
        lexer->advance(lexer, false);
    }
    if (lexer->lookahead == '*') {
        // advance the lexer until we see another star OR until
        lexer->advance(lexer, false);
        lexer->result_symbol = EMPHASIS_START; // Emit the EMPHASIS_START token
        state->in_emphasis = true; // Update the state
        return true;
    }
    lexer->result_symbol = EMPHASIS_START; // Emit the EMPHASIS_START token
    state->in_emphasis = true; // Update the state
    return true;
  }

  // Detect emphasis end
  if (lexer->lookahead == '*' && valid_symbols[EMPHASIS_END] && state->in_emphasis) {
    lexer->advance(lexer, false); // Consume the '*'
    lexer->result_symbol = EMPHASIS_END; // Emit the EMPHASIS_END token
    state->in_emphasis = false; // Update the state
    return true;
  }

  return false; // No token recognized
}
