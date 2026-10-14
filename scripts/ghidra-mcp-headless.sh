#!/bin/bash
# Start the GhidraMCP *headless* server (no GUI) on 127.0.0.1:8089 with the SC4 project open.
# Classpath recipe mirrors mcp/ghidra-mcp/docker/entrypoint.sh. Stop with Ctrl-C / kill.
export JAVA_HOME="${JAVA_HOME:-/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GH=/opt/homebrew/opt/ghidra/libexec
CP="$HOME/Library/ghidra/ghidra_12.1.4_PUBLIC/Extensions/GhidraMCP/lib/GhidraMCP-7.0.0.jar"
for jar in "$GH"/Ghidra/Framework/*/lib/*.jar "$GH"/Ghidra/Features/*/lib/*.jar "$GH"/Ghidra/Processors/*/lib/*.jar; do
  CP="$CP:$jar"
done
exec "$JAVA_HOME/bin/java" -Xmx${GHIDRA_MCP_XMX:-3g} -XX:+UseG1GC \
  -Dghidra.home="$GH" -Dapplication.name=GhidraMCP \
  -classpath "$CP" com.xebyte.headless.GhidraMCPHeadlessServer \
  --bind 127.0.0.1 --port 8089 \
  --project "$ROOT/ghidra/projects/SC4.gpr" \
  --program "/Sim City 4 Deluxe Editionsub" "$@"
