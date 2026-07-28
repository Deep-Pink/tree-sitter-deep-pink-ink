#!zsh
find ./test/**/* ./src/parser.c ./queries/*.scm ./test/**/* | entr -r -c -c -s "clear && clear && tree-sitter test"