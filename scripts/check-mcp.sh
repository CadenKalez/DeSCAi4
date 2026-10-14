#!/bin/bash
# Verify the Ghidra plugin's HTTP server is up (needs Ghidra running with GhidraMCPPlugin enabled).
URL="${GHIDRA_MCP_URL:-http://127.0.0.1:8089}"
echo "GET $URL/check_connection"; curl -sS --max-time 5 "$URL/check_connection"; echo
echo "GET $URL/mcp/health";       curl -sS --max-time 5 "$URL/mcp/health"; echo
