module.exports = grammar({
  name: "quarto",

  extras: ($) => [
    // The below symbol matches any whitespace character (spaces, tabs, line breaks, etc.)
    /\s+/,
    $.comment,
  ],

  externals: ($) => [$.line_end, $.emph_start, $.emph_end],

  rules: {
    source_file: ($) => repeat($._section),

    comment: ($) => token(seq("<!--", /.*/, "-->")),

    _yaml: ($) => choice(),
    paragraph: ($) => prec.right(3, seq(repeat1($._line), $.paragraph_end)),
    line_break: ($) =>
      prec.right(
        2,
        choice(
          seq(token.immediate("\\"), $.line_end),
          seq(token.immediate("  "), $.line_end),
        ),
      ),
    paragraph_end: ($) =>
      prec.right(
        4,
        choice(
          seq($.line_break, repeat1($.line_end)),
          seq($.line_end, repeat1($.line_end)),
        ),
      ),
    _line_content: ($) =>
      repeat1(choice($.emph, $.word, $.puncuation, $.symbols)), //prec(1, repeat1(choice($.word, $.whitespace))),
    _line: ($) =>
      prec.right(
        2,
        seq(choice($._line_content, $.line_break), optional($.line_end)),
      ), //prec.right(seq($._line, optional($.line_end))),
    word: ($) => /[\p{L}\p{N}]+/,
    puncuation: ($) =>
      choice(
        $.period,
        $.comma,
        $.question,
        $.exclamation,
        $.colon,
        $.semi_colon,
        $.quotation,
      ),
    period: ($) => ".",
    comma: ($) => ",",
    exclamation: ($) => "!",
    question: ($) => "?",
    colon: ($) => ":",
    semi_colon: ($) => ";",
    quotation: ($) => choice($.single_quote, $.double_quote),
    single_quote: ($) => "'",
    double_quote: ($) => '"',
    symbols: ($) => prec(-1, /[@#\$%\^\&\*\(\)_\+\=\-/><~]/),

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

    emph: ($) =>
      seq(
        $.emph_start,
        choice($.word, seq($.word, optional($._line), $.word)),
        $.emph_end,
      ),
    // prec(
    //   1,
    //   ,
    // ),

    // whitespace: ($) => /\s+/,
  },

  conflicts: ($) => [
    // [$.paragraph],
    // [$.paragraph, $.line],
    // [$.paragraph, $.word],
  ],
});
