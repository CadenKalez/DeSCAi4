#!/bin/zsh
# usage: scripts/credit_names.sh 'src/..|h' 'evidence' 'exact demangled name' ...   (registers free functions by exact symbol)
cd "$(dirname "$0")/.."
SRC=$1; EV=$2; shift 2
RX="^($(python3 -c 'import re,sys;print("|".join(re.escape(a) for a in sys.argv[1:]))' "$@"))\$"
python3 scripts/ledger_class.py --class free --source "$SRC" --evidence "$EV" --match "$RX" | tail -2
