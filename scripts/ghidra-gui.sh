#!/bin/bash
# Launch the Ghidra GUI (Homebrew install, JDK 21 via openjdk@21).
export JAVA_HOME="${JAVA_HOME:-/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home}"
exec /opt/homebrew/bin/ghidraRun "$@"
