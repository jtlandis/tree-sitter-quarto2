/**
 * @file Quarto markdown format
 * @author jtlandis <jtlandis314@gmail.com>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: "quarto",

  extras: ($) => [
    // The below symbol matches any whitespace character (spaces, tabs, line breaks, etc.)
    /\s/,
    $.comment,
  ],

  conflicts: ($) => [
    // Add conflicts here if needed
  ],

  inline: ($) => [
    // Add inline rules here if needed
  ],

  word: ($) => $.word,

  rules: {
    comment: ($) => token(seq("<!--", /.*/, "-->")),

    heading: ($) =>
      choice($.heading_1, $.heading_2, $.heading_3, $.heading_4, $.heading_5),

    heading_1: ($) => seq("# ", repeat($.word)),
    heading_2: ($) => seq("## ", repeat($.word)),
    heading_3: ($) => seq("### ", repeat($.word)),
    heading_4: ($) => seq("#### ", repeat($.word)),
    heading_5: ($) => seq("##### ", repeat($.word)),

    word: ($) => /[^\s\n#]+/,
  },
});
