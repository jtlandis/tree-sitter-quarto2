package tree_sitter_quarto_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_quarto "github.com/jtlandis/tree-sitter-quarto2/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_quarto.Language())
	if language == nil {
		t.Errorf("Error loading Quarto grammar")
	}
}
