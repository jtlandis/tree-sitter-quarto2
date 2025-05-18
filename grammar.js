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
        repeat1($.line_end),
        // choice(
        //   seq($.line_break, repeat1($.line_end)),
        //   seq($.line_end, repeat1($.line_end)),
        // ),
      ),
    _line_content: ($) =>
      repeat1(choice($.emph, $.word, $.puncuation, $.literal, $.symbols)), //, $.whitespace)), //prec(1, repeat1(choice($.word, $.whitespace))),
    _line: ($) =>
      prec.right(2, seq($._line_content, choice($.line_break, $.line_end))), //prec.right(seq($._line, optional($.line_end))),
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
    symbols: ($) => prec(-1, /[@#\$%\^\&\*\(\)_\+\=\-/><~\\]/),
    literal: ($) => prec(10, /\\[@#\$%\^\&\*\(\)_\+\=\-/><~\\]/),
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
    heading_1: ($) => seq("#", $._line),
    heading_2: ($) => seq("##", $._line),
    heading_3: ($) => seq("###", $._line),
    heading_4: ($) => seq("####", $._line),
    heading_5: ($) => seq("#####", $._line),
    heading_6: ($) => seq("######", $._line),

    emph: ($) =>
      prec(
        2,
        seq(
          $.emph_start,
          repeat1(
            seq(
              repeat1(choice($.word, $.puncuation, $.literal, $.symbols)),
              optional(choice($.line_break, $.line_end)),
            ),
          ),
          // seq(
          //   repeat1(choice($.word, $.puncuation, $.symbols)),
          //   optional(
          //     seq(
          //       optional(
          //         seq(
          //           repeat1(
          //             choice($.whitespace, $.word, $.puncuation, $.symbols),
          //           ),
          //           optional(choice($.line_break, $.line_end)),
          //         ),
          //       ),
          //       repeat1(choice($.word, $.puncuation, $.symbols)),
          //     ),
          //   ),
          // ),

          $.emph_end,
        ),
      ),
    // prec(
    //   1,
    //   ,
    // ),
    // whitespace: ($) => /\s+/,
  },

  conflicts: ($) => [
    [$.emph],
    // [$.paragraph],
    // [$.paragraph, $.line],
    // [$.paragraph, $.word],
  ],
});
