#!/bin/sh
# Usage: check-flagged-symbols.sh NAMESPACE:: OBJECT... [NAMESPACE:: OBJECT...]...
# Each OBJECT was compiled with Rack's -funsafe-math-optimizations. Fails if one defines an
# external symbol outside the namespace named before it (EG13's models: eg13::rack_flags::, the
# tests' reference models: eg13::test::rack_flags::). The linker merges weak symbols of the same
# name, so a symbol elsewhere could replace (or be replaced by) a copy built with other flags in
# EG13 or its tests (preflight ruling F6). Allowed: Rack's per-thread random state, which is data
# and shared on purpose (rack.hpp), and GCC's personality reference.
status=0
namespace=
for arg in "$@"; do
  case "$arg" in
    *::) namespace=$arg; continue ;;
  esac
  [ -n "$namespace" ] || { echo "no namespace given before $arg"; exit 1; }
  symbols=$(nm -g -C --defined-only "$arg" | grep -E '^[0-9a-fA-F]+ [A-Za-z] ') \
    || { echo "nm found no symbols in $arg"; exit 1; }
  stray=$(printf '%s\n' "$symbols" \
    | grep -vF "$namespace" \
    | grep -v 'rack::random::tState$' \
    | grep -v 'DW\.ref\.__gxx_personality_v0$')
  if [ -n "$stray" ]; then
    echo "$arg defines symbols outside $namespace"
    printf '%s\n' "$stray"
    status=1
  else
    echo "$arg: $(printf '%s\n' "$symbols" | wc -l | tr -d ' ') symbols, all in $namespace"
  fi
done
exit $status
