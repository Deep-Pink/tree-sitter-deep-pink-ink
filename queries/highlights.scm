; ; expressions
[
  (hash)
  (triple_greater)
  (equals_sign) ; assign
] @operator

; ; functions
; (function_decl
;     name: (identifier)  @function)
(string) @string

(unit_command
  name: (string) @tag)

(named_command
  name: (string) @tag)

; (call_expression
;     (template_elaborated_ident (_)* (template_elaborated_ident_part name: ((identifier) @function)). ))
; ; punctuation
[
  (open_parenthesis)
  (close_parenthesis)
] @punctuation.bracket @punctuation.list_marker

[
  (comma)
  (colon)
] @punctuation.delimiter

(boolean) @boolean

(number) @number

(null) @keyword

(tag_command
  (named_command
    name: (string
      (identifier_string
        value: (identifier) @name
        (#match? @name "(choices|line)")) @keyword)))

(named_value
  name: (string) @property)
