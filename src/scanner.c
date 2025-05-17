#include <tree_sitter/parser.h>
#include <stdbool.h>
#include <stdio.h>

enum TokenType {
  LINE_END, // Define the token type for line_end
  EMPHASIS_START, // $.emph_start
  EMPHASIS_END, // $.emph_end
};

void *tree_sitter_quarto_external_scanner_create() {
  return NULL; // No state is needed for this scanner
}

void tree_sitter_quarto_external_scanner_destroy(void *payload) {
  // No cleanup is needed
}

unsigned tree_sitter_quarto_external_scanner_serialize(void *payload, char *buffer) {
  return 0; // No state to serialize
}

void tree_sitter_quarto_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  // No state to deserialize
}

bool tree_sitter_quarto_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
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
   if (lexer->lookahead == '*' && valid_symbols[EMPHASIS_START]) {
     lexer->advance(lexer, false); // Consume the '*'
     lexer->result_symbol = EMPHASIS_START; // Emit the EMPHASIS_START token
     return true;
   }

   // Detect emphasis end
   if (lexer->lookahead == '*' && valid_symbols[EMPHASIS_END]) {
     lexer->advance(lexer, false); // Consume the '*'
     lexer->result_symbol = EMPHASIS_END; // Emit the EMPHASIS_END token
     return true;
   }

  return false; // No token recognized
}
