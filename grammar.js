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
  word: $ => $.identifier,

  extras: ($) => [$.whitespace],
  externals: $ => [
    $.eol,
    $.eof,
    $.start_string,
    $.end_string,
    $.whitespace,
    $._error_sentinel
  ],
  rules: {
    // Entry points
    // source_file: $ => $.line_command,
    line_command: $ => seq($.triple_greater, choice($.unit_command, $.named_command), $.stop_token),
    tag_command: $ => seq($.hash, choice($.named_command, $.implicit_command), $.stop_token),
    // TOKENS
    unit_command: $ => seq(field('name', choice($.identifier, $.quoted_string))),
    implicit_command: $ => field('arguments', $.arguments),
    triple_greater: $ => '>>>',
    comma: $ => ',',
    colon: $ => ':',
    true: $ => 'true',
    false: $ => 'false',
    null: $ => 'null',
    open_parenthesis: $ => '(',
    close_parenthesis: $ => ')',
    equals_sign: $ => '=',
    hash: $ => '#',
    stop_token: $ => choice($.hash, $.eol, $.eof),


    identifier: $ => IDENTIFIER_REGEX,
    named_command: $ => seq(field('name', choice($.identifier, $.quoted_string)), $.colon, field('arguments', $.arguments)),

    _concrete_value: $ => prec(1, choice(
      // String
      $.string,
      // Tokens
      $.boolean,
      $.null,
      $.number,
    )),
    named_value: $ => seq(choice($.quoted_string, $.identifier_string), $.equals_sign, $.value),
    empty_collection: $ => seq($.open_parenthesis, $.close_parenthesis),
    argument_array: $ => seq($.value, repeat(seq($.comma, $.value)), optional($.comma)),
    argument_map: $ => seq($.named_value, repeat(seq($.comma, $.named_value)), optional($.comma)),
    argument_bag: $ => seq($.value, repeat(seq($.comma, $.value)), repeat1(seq($.comma, $.named_value)), optional($.comma)),
    arguments: $ => choice(prec(2, $.argument_array), prec(1, $.argument_map), $.argument_bag),
    array: $ => seq($.open_parenthesis, $.value, repeat(seq($.comma, $.value)), optional($.comma), $.close_parenthesis),
    bag: $ => seq($.open_parenthesis, seq($.value, repeat(seq($.comma, $.value))), repeat1(seq($.comma, $.named_value)), optional($.comma), $.close_parenthesis),
    map: $ => seq($.open_parenthesis, $.named_value, repeat(seq($.comma, $.named_value)), optional($.comma), $.close_parenthesis),
    collection: $ => choice($.empty_collection, $.array, $.map, $.bag),
    value: $ => choice(
      prec.left(1, $._concrete_value),
      $.collection
    ),
    // Values
    number: $ => NUMBER_REGEX,
    identifier_string: $ => field('value', $.identifier),
    string_value: $ => repeat1(token(/((\\[\"\\\f\r\t])|[^\"\\\f\r\t])/)),
    quoted_string: $ => seq($.start_string, optional($.string_value), $.end_string),
    string: $ => choice($.quoted_string, $.identifier_string),
    boolean: $ => choice($.true, $.false),
  },
});
