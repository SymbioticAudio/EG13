#!/bin/sh
# Usage: check-flagged-symbols.sh NAMESPACE:: OBJECT... [NAMESPACE:: OBJECT...]...
# An OBJECT argument may be a CMake list of paths separated by ';', as ctest passes
# $<TARGET_OBJECTS:...> of a library with several sources.
# Each OBJECT was compiled with Rack's -funsafe-math-optimizations. Fails if one defines an
# external symbol outside the namespace named before it (EG13's models: eg13::rack_flags::, the
# tests' reference models: eg13::test::rack_flags::). The linker merges weak symbols of the same
# name, so a symbol elsewhere could replace (or be replaced by) a copy built with other flags in
# EG13 or its tests (preflight ruling F6). Allowed besides: GCC's personality reference and the
# AddressSanitizer's bookkeeping symbols.
status=0
namespace=
check() {
  arg=$1
  [ -n "$namespace" ] || { echo "no namespace given before $arg"; exit 1; }
  symbols=$(nm -g -C --defined-only "$arg" | grep -E '^[0-9a-fA-F]+ [A-Za-z] ') \
    || { echo "nm found no symbols in $arg"; exit 1; }
  # The symbol's name must start with the namespace (after nm's address and type, and after a
  # demangler prefix such as "vtable for "); a std:: template merely naming it does not count.
  prefix=$(printf '%s' "$namespace" | sed 's/[.[\*^$]/\\&/g')
  stray=$(printf '%s\n' "$symbols" \
    | grep -vE "^[0-9a-fA-F]+ [A-Za-z] ((vtable|typeinfo|typeinfo name|VTT|guard variable) for )?$prefix" \
    | grep -v 'DW\.ref\.__gxx_personality_v0$' \
    | grep -vE ' _*asan_[A-Za-z_]*$')
  if [ -n "$stray" ]; then
    echo "$arg defines symbols outside $namespace"
    printf '%s\n' "$stray"
    status=1
  else
    echo "$arg: $(printf '%s\n' "$symbols" | wc -l | tr -d ' ') symbols, all in $namespace"
  fi
}

# Fails closed: an OBJECT argument that names no object (empty, or only ';'), or a namespace
# with no OBJECT after it, means the build passed nothing to check.
[ $# -gt 0 ] || { echo "no arguments"; exit 1; }
objects=0
for arg in "$@"; do
  case "$arg" in
    *::)
      [ -z "$namespace" ] || [ "$objects" -gt 0 ] || { echo "no objects given for $namespace"; exit 1; }
      namespace=$arg
      objects=0
      continue ;;
  esac
  rest=$arg
  found=0
  while [ -n "$rest" ]; do
    object=${rest%%;*}
    case "$rest" in
      *\;*) rest=${rest#*;} ;;
      *) rest= ;;
    esac
    if [ -n "$object" ]; then
      check "$object"
      found=$((found + 1))
    fi
  done
  [ "$found" -gt 0 ] || { echo "an object argument names no object: '$arg'"; exit 1; }
  objects=$((objects + found))
done
[ -n "$namespace" ] && [ "$objects" -gt 0 ] || { echo "no objects given for ${namespace:-any namespace}"; exit 1; }
exit $status
