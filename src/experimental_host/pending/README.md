> **SUPERSEDED (2026-10-29).** The native application `build/sc4_game` (src/app/game_main.mm) already is this game loop: its `Frame()`
> pumps both message servers, ticks `cSC4App` and the window manager, and draws through Metal. It reaches the splash, the region screen
> and a loaded city. Run it headless with `build/sc4_game --script scenarios/region.txt --threads=1`. These two files are kept only as
> history of the early experimental host; do not link them.

# pending - written, not linked yet

`HostGame.cpp` is the game loop: after `RunBoot(keepAlive)` it lets the recovered `cWinSC4App` create the splash screen
and swap to the region screen, then pumps the message servers and the app tick for N frames. `HostWinSC4App.h` is the
`IWinSC4AppHost` adapter it needs (every absent service answers null/false, which is what the original does when a
service is missing).

They live here because linking them pulls in `cWinSC4App`, which pulls in `cSC4View3DWin`, and that file is being
recovered right now. To turn them on:

1. move both files back into `src/experimental_host/`
2. add `src/sc4/WinSC4App.cpp` (and the view sources once `AddGUI` is complete) to `scripts/host_link_list.txt`
3. add the `--play [frames]` flag in `host_main.mm` calling `exphost::RunGame(frames)`
4. `scripts/build_host.sh && ./build/sc4_host --play 120`

The first three services (`GraphicSystem`, `MessageServer2`, `ReadyToHearMusic`) are the ones that matter: `ReadyToHearMusic`
is set at the end of `CreateRegionScreen`, and it is the host-side signal that the region screen got far enough to ask
for its music.
