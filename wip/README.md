# Unfinished work from interrupted parallel agents

Each `patches/*.patch` is `git diff` of one agent's partial port (headers/sources/tests written before the agent was
stopped). They are **not** applied and not built by `run_tests.sh`. To resume a class: `git apply wip/patches/<file>.patch`,
finish it (test that prints ok via `scripts/build_test_linux.sh`, registry line, `notes/ledger_pending/<name>.txt`),
then `python3 scripts/apply_agent_results.py`.

Known state when saved: LandfillZone, AmbienceLayer and cGZDBSegmentDirectoryFiles have source + test that do not compile
yet (test fixes noted in the commit log); CitySituationAdvice and ViewInputControlSignTool have sources without tests;
the rest are headers or partial edits only.
