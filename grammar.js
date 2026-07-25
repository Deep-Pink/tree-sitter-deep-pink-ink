/**
 * @file DeepPinkInk grammar for tree-sitter
 * @author Natalie Cuthbert <natalie@cuthbert.co.za>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check


const NUMBER_REGEX = /-?(0|([1-9]\d*))(\.\d+)?([eE][+-]?\d+)?/;
const IDENTIFIER_REGEX = /[A-Za-z_]([A-Za-z0-9]*)/;

export default grammar({
  name: "deep_pink_ink",

  rules: {
    // TODO: add the actual grammar rules
    source_file: $ => "hello"
  }
});
