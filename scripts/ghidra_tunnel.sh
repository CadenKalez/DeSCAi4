#!/bin/zsh
# One-command tunnel: exposes the local GhidraMCP backend (127.0.0.1:8089) through a Cloudflare quick tunnel,
# protected by a random token. Run it, copy the two printed lines to Claude, press Ctrl-C when finished.
# usage: scripts/ghidra_tunnel.sh
cd "$(dirname "$0")/.."
if ! curl -s -m 3 -o /dev/null http://127.0.0.1:8089/; then
  echo "GhidraMCP is not answering on 127.0.0.1:8089 - start it first (scripts/ghidra-mcp-headless.sh)."; exit 1
fi
if ! command -v cloudflared >/dev/null; then
  command -v brew >/dev/null || { echo "Install Homebrew or cloudflared first."; exit 1; }
  brew install cloudflared || exit 1
fi
TOKEN=$(python3 -c 'import secrets;print(secrets.token_urlsafe(32))')
LOG=$(mktemp)
python3 scripts/ghidra_proxy.py "$TOKEN" 8090 & PROXY=$!
cloudflared tunnel --no-autoupdate --url http://127.0.0.1:8090 >"$LOG" 2>&1 & TUN=$!
trap 'kill $PROXY $TUN 2>/dev/null; rm -f "$LOG"; echo "tunnel closed"' EXIT INT TERM
URL=""
for i in {1..60}; do
  URL=$(grep -o 'https://[a-z0-9-]*\.trycloudflare\.com' "$LOG" | head -1)
  [ -n "$URL" ] && break; sleep 1
done
[ -n "$URL" ] || { echo "no tunnel URL appeared; cloudflared log:"; cat "$LOG"; exit 1; }
echo
echo "Tunnel is up. Send Claude these two lines:"
echo "GHIDRA_URL=$URL"
echo "GHIDRA_TOKEN=$TOKEN"
echo
echo "Leave this window open. Ctrl-C closes the tunnel."
wait $TUN
