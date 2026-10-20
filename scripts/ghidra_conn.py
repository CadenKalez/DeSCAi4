"""Optional remote Ghidra access: import this module and urllib calls to GHIDRA_URL-less scripts keep working.
Set GHIDRA_URL / GHIDRA_TOKEN (see scripts/ghidra_tunnel.sh) to talk to a tunnelled backend."""
import os, urllib.request
BASE = os.environ.get("GHIDRA_URL", "http://127.0.0.1:8089").rstrip("/")
_tok = os.environ.get("GHIDRA_TOKEN")
if _tok:
    op = urllib.request.build_opener()
    op.addheaders = [("X-Ghidra-Token", _tok), ("User-Agent", "sc4-decomp")]
    urllib.request.install_opener(op)
