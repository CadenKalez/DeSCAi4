# Brief for parallel porting agents (cloud/Linux container)

You are one of several agents continuing a hand-written clean C++20 decompilation of SimCity 4 (x86_64, Aspyr macOS build)
in your own git worktree. Read `AGENT_RULES.md`, `NEXT_AGENT.md`, `.claudecode` first. Those were written for a macOS
workflow; here, in the Linux cloud container, the differences below apply.

## Setup (in this order)
1. `git fetch origin claude/amazing-euler-v1kvwg && git reset --hard FETCH_HEAD` (a worktree may start from an old base).
2. **Never push.** Build/run a test: `scripts/build_test_linux.sh scripts/tests/<test>.cpp <class .cpp> [extra .cpp ...]`
   (copy the extra sources from a neighbouring entry of `scripts/run_tests.sh`). After you create a registry file, run it
   with `scripts/run_registered_linux.sh <name>`. ASan runtimes are missing: use gdb/valgrind for crashes.
3. **Ghidra**: the prompt that started you gives `GHIDRA_URL` and `GHIDRA_TOKEN`. In every shell command that talks to
   Ghidra, first `export` them. Never write the token into any file, commit message, test or log.
   - `python3 scripts/class_dump.py <ClassName> [--list] [--lo 0x.. --hi 0x..]` writes the decompilation of a class's
     address span to `work/sc4/<Class>.txt` (gitignored). The span may include neighbouring classes; narrow with
     `--lo/--hi`. `notes/work_queue.md` lists source files by address span.
   - Endpoints: `curl -s -H "X-Ghidra-Token: $GHIDRA_TOKEN" "$GHIDRA_URL/<path>"`:
     `get_functions?functions=0xA,0xB&fields=decompiled_code&include_call_context=false`,
     `disassemble_function?address=A`, `read_memory?address=A&length=N`, `find_functions?name=X&limit=N`.
     `python3 scripts/read_mem.py ADDR LEN hex` also works.
   - `notes/symbols-functions-demangled.txt` does NOT exist here: do not run `ledger_class.py`, `dec_addrs.py`,
     `apply_agent_results.py`, `class_range.py`.
   - The decompiler drops call arguments (and sometimes message construction): read the disassembly whenever an
     argument or a vtable slot matters.

## Rules (non-negotiable)
- Compare **every** function with its Ghidra decompilation. Do not invent behaviour. A passing test only proves the port
  matches your reading; the decompilation is the authority. If they disagree, the source is wrong unless you can show
  otherwise from the binary. (In past sessions the ported source was usually right and the *test* expectations wrong:
  confirm from the binary before changing either.)
- Clean C++20 in the style of neighbouring `src/` files; unique descriptive names; replace `SL::sp*` globals with small
  host interfaces (seams); where a seam hides original logic, say so in a comment and mark the function
  `recovered-partial`. Keep original quirks and comment them.
- Before starting a class, grep `notes/ledger.csv` for its addresses; don't redo recovered functions.
- **Do not edit `notes/ledger.csv` or `scripts/run_tests.sh`.** Instead write
  - `notes/ledger_fragments/<Class>.csv`: no header, 6 columns `address,symbol,unique_name,source,status,evidence`
    (address like `0x1000acbac`; `source` like `src/sc4/Foo.cpp|h`; status `recovered` or `recovered-partial`; quote
    fields with commas; `unique_name` must not exist in the ledger: grep `,<name>,` notes/ledger.csv and your own rows).
  - `notes/ledger_fragments/<Class>_status_updates.csv`: same columns, to *change* an existing row (same address and
    unique_name; status/evidence replaced).
  - `scripts/tests/registry/<test_name>.txt`: one line in the format of the entries of the `TESTS` array in `run_tests.sh`.
  The coordinator merges these with `scripts/merge_agent_fragments.py`.
- Don't modify existing shared source files except for minimal additive changes; list them in your report.
- **Commit locally in your worktree often** (after every coherent chunk, even WIP): interrupted agents lose uncommitted work.
  Each commit message ends with
  `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_015sWy4p5A4GaoBjkp9ENoHh`.
- Never ask the user questions; make sensible choices and note them.
- Final report (short): commit hash(es); functions recovered / partial / skipped with reasons; test output; files changed
  outside your own; what is left.
