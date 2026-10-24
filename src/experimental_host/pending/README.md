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
