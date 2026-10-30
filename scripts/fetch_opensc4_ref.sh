#!/bin/zsh
# Fetches OpenSC4's behaviour write-ups and save-record schemas (https://github.com/danhere/opensc4, LGPL-2.1) into
# work/opensc4_ref (git-ignored): a reference for the classes this port is missing. Its addresses and object layouts are the
# Windows build 1.1.641's; transcribe from our own disassembly. See notes/GROWTH_PLAN.md.
set -e
cd "$(dirname "$0")/.."
D=work/opensc4_ref
mkdir -p $D/docs $D/save_schemas
for f in $(gh api repos/danhere/opensc4/contents/docs --jq '.[] | select(.type=="file") | .name'); do
    gh api repos/danhere/opensc4/contents/docs/$f --jq .content | base64 -d > $D/docs/$f
done
for f in $(gh api repos/danhere/opensc4/contents/tools/re/save_schemas --jq '.[].name'); do
    gh api repos/danhere/opensc4/contents/tools/re/save_schemas/$f --jq .content | base64 -d > $D/save_schemas/$f
done
gh api repos/danhere/opensc4/contents/LICENSE --jq .content | base64 -d > $D/LICENSE
echo "fetched $(ls $D/docs | wc -l) docs and $(ls $D/save_schemas | wc -l) schemas into $D"
