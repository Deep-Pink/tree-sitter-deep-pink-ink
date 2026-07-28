#!zsh
find ./grammar.js ./src/scanner.c ./queries/*.scm ./test/**/* | entr -r -c -c -s "clear && tree-sitter generate && tree-sitter build --wasm && tree-sitter playground -q"