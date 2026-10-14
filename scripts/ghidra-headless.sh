#!/bin/bash
# Run analyzeHeadless against the SC4 project. Extra args are passed through.
# Example: scripts/ghidra-headless.sh -process "Sim City 4 Deluxe Editionsub" -noanalysis -postScript Foo.java
export JAVA_HOME="${JAVA_HOME:-/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless "$ROOT/ghidra/projects" SC4 "$@"
