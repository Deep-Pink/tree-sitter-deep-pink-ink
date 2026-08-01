/**
 * @file DeepPinkInk grammar for tree-sitter
 * @author Natalie Cuthbert <natalie@cuthbert.co.za>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const NUMBER_REGEX = /-?(0|([1-9]\d*))(\.\d+)?([eE][+-]?\d+)?/;
const IDENTIFIER_REGEX = /[A-Za-z_]([A-Za-z0-9_]*)/;

export default grammar({
  name: "deep_pink_ink",
  word: ($) => $.identifier,
  extras: ($) => [$.whitespace],
  externals: ($) => [
    $.eol,
    $.eof,
    $.start_string,
    $.end_string,
    $.whitespace,
    $.start_line_command,
    $.start_tag_command,
    $.start_of_line,
    $.start_hole,
    $.end_hole,
    $.open_format_tag, // "<" b>
    $.close_format_tag, // <b ">"
    $.close_self_closing_format_tag, // <b "/>"
    $.open_close_format_tag, // "</" b>
    $.speaker_identifier,
    $._error_sentinel,
  ],
  rules: {
    entry_point: ($) =>
      repeat(choice($.line_command, $.tag_command, $.content_line_with_speaker, $.content_line, $.empty_line)),
    content_line: ($) => seq(
      $.start_of_line,
      repeat1($._outer_content),
      $.stop_token,
    ),
    content_line_with_speaker: ($) => seq(
      $.start_of_line,
      $.speaker,
      repeat($._outer_content),
      $.stop_token,
    ),
    speaker: ($) => choice(
      seq($.hole, $.colon),
      seq($.speaker_identifier, $.colon),
      seq($.quoted_string, $.colon)
    ),
    _outer_content: ($) => choice($.hole, $.format_tag_command_with_body, $.self_closing_format_tag_command, $.text_content),
    _content_element: $ => choice($.text_content, $.hole, $.self_closing_format_tag_command, $.format_tag_command_with_body),
    format_tag_command_with_body: ($) => seq($.start_format_tag, repeat($._content_element), $.end_format_tag),
    self_closing_format_tag_command: ($) => choice($._unit_self_closing_format_tag_command, $._self_closing_format_tag_command_with_arguments),
    _unit_self_closing_format_tag_command: ($) => prec.right(2, seq($.open_format_tag, field("name", $.key), $.close_self_closing_format_tag)),
    _self_closing_format_tag_command_with_arguments: ($) =>
      seq($.open_format_tag, field("name", $.key), $.colon, field("arguments", $.arguments), $.close_self_closing_format_tag),
    text_content: ($) => /[^\n{><]+/,
    start_format_tag: ($) => choice($._unit_start_format_tag, $._start_format_tag_with_arguments),
    _unit_start_format_tag: ($) => seq($.open_format_tag, field("name", $.key), $.close_format_tag),
    _start_format_tag_with_arguments: ($) =>seq($.open_format_tag, field("name", $.key), $.colon, field("arguments", $.arguments), $.close_format_tag),
    end_format_tag: ($) => seq($.open_close_format_tag, field("name", $.key), $.close_format_tag),
    empty_line: ($) => seq($.start_of_line, $.stop_token),
    line_command: ($) => seq(
          $.start_of_line,
          $.start_line_command,
          choice($.unit_command, $.named_command),
          $.stop_token,
      ),
    tag_command: ($) =>
      seq(
        $.start_of_line,
        $.start_tag_command,
        choice($.named_command, $.implicit_command),
        $.stop_token,
      ),
    // TOKENS
    unit_command: ($) => field("name", $.key),
    implicit_command: ($) => field("arguments", $.arguments),
    comma: ($) => ",",
    colon: ($) => ":",
    true: ($) => "true",
    false: ($) => "false",
    null: ($) => "null",
    open_parenthesis: ($) => "(",
    close_parenthesis: ($) => ")",
    equals_sign: ($) => "=",
    stop_token: ($) => choice($.start_tag_command, $.eol, $.eof),
    identifier: ($) => IDENTIFIER_REGEX,
    named_command: ($) =>
      seq(
        field("name", $.key),
        $.colon,
        field("arguments", $.arguments),
      ),

    _concrete_value: ($) =>
        choice(
          // String
          $.string,
          // Tokens
          $.boolean,
          $.null,
          $.number,
        ),
    named_value: ($) =>
      seq(field("name", $.key), $.equals_sign, $.value),
    empty_collection: ($) => seq($.open_parenthesis, $.close_parenthesis),
    argument_array: ($) =>
      seq($.value, repeat(seq($.comma, $.value)), optional($.comma)),
    argument_map: ($) =>
      seq(
        $.named_value,
        repeat(seq($.comma, $.named_value)),
        optional($.comma),
      ),
    argument_bag: ($) =>
      seq(
        $.value,
        repeat(seq($.comma, $.value)),
        repeat1(seq($.comma, $.named_value)),
        optional($.comma),
      ),
    arguments: ($) =>
      choice(
        prec(2, $.argument_array),
        prec(1, $.argument_map),
        $.argument_bag,
      ),
    array: ($) =>
      seq(
        $.open_parenthesis,
        $.value,
        repeat(seq($.comma, $.value)),
        optional($.comma),
        $.close_parenthesis,
      ),
    bag: ($) =>
      seq(
        $.open_parenthesis,
        seq($.value, repeat(seq($.comma, $.value))),
        repeat1(seq($.comma, $.named_value)),
        optional($.comma),
        $.close_parenthesis,
      ),
    map: ($) =>
      seq(
        $.open_parenthesis,
        $.named_value,
        repeat(seq($.comma, $.named_value)),
        optional($.comma),
        $.close_parenthesis,
      ),
    collection: ($) => choice($.empty_collection, $.array, $.map, $.bag),
    value: ($) => choice($._concrete_value, $.collection, $.hole),
    // Values
    number: ($) => NUMBER_REGEX,
    identifier_string: ($) => field("value", $.identifier),
    string_value: ($) => repeat1(token(/((\\[\"\\\f\r\t])|[^\"\\\f\r\t])/)),
    quoted_string: ($) =>
      seq($.start_string, optional($.string_value), $.end_string),
    string: ($) => choice($.quoted_string, $.identifier_string),
    boolean: ($) => choice($.true, $.false),
    hole: ($) =>
      prec.left(
        seq(
          $.start_hole,
          repeat(choice($.eol, $.hole, /(\\\{|[^{}])+/)),
          $.end_hole,
        ),
      ),
    key: ($) => choice($.hole, $.string)
  },
});
