#!/usr/bin/env bash
# Downloads clap-validator and pluginval into build/tools (idempotent).
set -euo pipefail
TOOLS="$(cd "$(dirname "$0")/.." && pwd)/build/tools"
mkdir -p "$TOOLS"

if [ ! -d "$TOOLS/clap-validator" ]; then
  curl -fsSL -o "$TOOLS/clap-validator.zip" \
    https://github.com/free-audio/clap-validator/releases/download/0.4.1/clap-validator-0.4.1-127-g152b982-macos-universal.zip
  unzip -q -o "$TOOLS/clap-validator.zip" -d "$TOOLS/clap-validator"
  # The release zip wraps a .tar.gz.
  for t in "$TOOLS"/clap-validator/*.tar.gz; do [ -e "$t" ] || continue; tar -xzf "$t" -C "$TOOLS/clap-validator"; done
  find "$TOOLS/clap-validator" -type f -name clap-validator -exec chmod +x {} \;
fi

if [ ! -d "$TOOLS/pluginval" ]; then
  curl -fsSL -o "$TOOLS/pluginval.zip" \
    https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_macOS.zip
  unzip -q -o "$TOOLS/pluginval.zip" -d "$TOOLS/pluginval"
fi

find "$TOOLS/clap-validator" -type f -name clap-validator
find "$TOOLS/pluginval" -type f -path '*MacOS/pluginval'
