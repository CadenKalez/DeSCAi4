# Ledger code-existence audit (2026-10-30)

`scripts/ledger_code_audit.py` checked every row claiming a recovery (recovered / recovered-partial / partial, 28,648 rows)
for code: its address cited under src/, a definition of the function (out of line anywhere in src/, inline in its class's
header), a compiler-generated special member, a macro-generated method, or a thunk of one of those.

- With code: 26,708. No code found: **1,940**, now marked `pending` (evidence: "code audit: no definition or cited address found ..."), listed in
  `notes/ledger_code_audit_none.csv`. Two hand-checked random samples (12 and 15 rows) were all real gaps.
- Rows with no code by class (top 40):

| class | rows |
|---|---|
| cSCLua | 203 |
| cGZGraphicSystem | 166 |
| cSC4NetworkTool | 110 |
| cRZVariant | 106 |
| (free) | 86 |
| cSC4LotDeveloper | 77 |
| nsTexelOp | 58 |
| cSC4City | 56 |
| cSC4TrafficSimulator | 49 |
| cSC4ResidentialSimulator | 48 |
| cSC4CitySituationManager | 48 |
| cSC4PowerPoleOccupant | 40 |
| cSLTokenField | 36 |
| cSC4ViewInputControlQuery | 30 |
| cRZAutoRefCount | 28 |
| cSC4FloraSimulator | 27 |
| nSGLDX7 | 26 |
| cGZDrawContext | 26 |
| cSC4PowerSimulator | 24 |
| cSC4NetworkOccupant | 22 |
| cSC4SimGrid | 20 |
| cSC424HourClock | 18 |
| cS3DModelInstanceAnimated | 17 |
| cSC4ResourceLibCOMDirector | 16 |
| cSC4PoliceSimulator | 14 |
| cSC4LandfillOccupant | 14 |
| cSC4PollutionSimulator | 13 |
| cSC4ProtectionSimulator | 11 |
| cPropertySetUint32XmlReader | 10 |
| cSC4FoundationOccupant | 10 |
| cSC4PowerLineOccupant | 10 |
| cSC4ToxicOccupant | 10 |
| cGZWinBtn | 10 |
| cSC4Demolition | 10 |
| cGZPlayList | 10 |
| cHitControlGroup | 9 |
| cHitPropertySetResource | 9 |
| cGZPropertySetSerializerXmlT | 9 |
| nsGZLua5ThreadLib::cThreadRegistry | 9 |
| cSC4FireOccupant | 9 |

Rerun the script after a recovery: it should report `none 0`; a non-zero count is a row claiming code that is not there.
