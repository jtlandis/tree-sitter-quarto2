module.exports = grammar({
  name: "quarto",

  extras: ($) => [
    // The below symbol matches any whitespace character (spaces, tabs, line breaks, etc.)
    /\s+/,
    $.comment,
  ],

  externals: ($) => [$.line_end],

  rules: {
    source_file: ($) => repeat($._section),

    comment: ($) => token(seq("<!--", /.*/, "-->")),

    _yaml: ($) => choice(),
    paragraph: ($) =>
      prec.right(seq(repeat1($._line), optional($.paragraph_end))),
    line_break: ($) =>
      prec.right(
        choice(
          seq(token.immediate("\\"), $.line_end),
          seq(token.immediate("  "), $.line_end),
        ),
      ),
    paragraph_end: ($) =>
      prec.right(
        choice(
          seq(token.immediate("\\"), $.line_end),
          seq(token.immediate("  "), $.line_end),
          seq($.line_end, repeat1($.line_end)),
        ),
      ),
    _line_content: ($) => repeat1(choice($.emph, $.word)), //prec(1, repeat1(choice($.word, $.whitespace))),
    _line: ($) => prec.right(choice($._line_content, $.line_end)), //prec.right(seq($._line, optional($.line_end))),
    word: ($) => /[\p{L}\p{N}]+/,
    content: ($) => prec.right(repeat1($.paragraph)),
    _section: ($) =>
      prec.right(choice(seq($.heading, $.content), $.heading, $.content)),
    heading: ($) =>
      choice(
        $.heading_1,
        $.heading_2,
        $.heading_3,
        $.heading_4,
        $.heading_5,
        $.heading_6,
      ),
    heading_1: ($) => seq("#", repeat($.word), $.line_end),
    heading_2: ($) => seq("##", repeat($.word), $.line_end),
    heading_3: ($) => seq("###", repeat($.word), $.line_end),
    heading_4: ($) => seq("####", repeat($.word), $.line_end),
    heading_5: ($) => seq("#####", repeat($.word), $.line_end),
    heading_6: ($) => seq("######", repeat($.word), $.line_end),

    emph: ($) => seq("*", $.word, "*"),

    // whitespace: ($) => /\s+/,
  },

  conflicts: ($) => [
    // [$.paragraph],
    // [$.paragraph, $.line],
    // [$.paragraph, $.word],
  ],
});
