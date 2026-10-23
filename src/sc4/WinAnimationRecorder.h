// cSC4WinAnimationRecorder — the "Animation Record" developer window (28 symbols at 0x100371f8a..0x100375de2 plus the
// compiler-emitted constructor / destructor twins), object size 0x368. It is a small tool window with a Start / Close /
// Span button row, a name / rate / time edit, an output-type option group (bmp / png / jpg) and a resizable outline that
// marks the screen area being recorded: every `Rate` milliseconds (or every fourth animation tick when the animation tick
// manager drives it) it copies the outline's rectangle out of the GZ driver's colour buffer into a frame bitmap, and on
// Stop it writes the frames out through the Gimex factory and reports where they went.
//
// The window is a plain cGZWin (0x150 bytes), so the fields start at +0x150:
//   +0x150 recording outline (cSC4WinResizableOutline, added to the 3D view window)
//   +0x158 status text (cGZWinText's cIGZWinText view)        +0x160 name edit    (cGZWinTextEdit view)
//   +0x168 name text "Animation" (cRZString)                  +0x190 rate edit
//   +0x198 rate text "100" (ms per frame)                     +0x1c0 time edit
//   +0x1c8 time text "5" (seconds)                            +0x1f0 Start button
//   +0x1f8 Close button                                       +0x200 Span button (single frame; disabled while idle)
//   +0x208 "Delete bitmaps when finished" check box — never created (quirk, see below)
//   +0x210 the value ReadPreferences remembers for that option (never read again)
//   +0x218 output-type option group (1 bmp, 2 png, 3 jpg)
//   +0x220 outline colour for GZPaint                          +0x224 / +0x228 mouse-down anchor
//   +0x230 colour-buffer extension of the GZ driver (iid 0xe69bfe2a)
//   +0x238 preference file path (cRZString, from the app's GetAppIniFilePath)
//   +0x260 the files written by the last write-out (std::vector<cRZString>)
//   +0x278 frame cap (0x80)   +0x27c byte cap (0x8000000 = 128 MB)
//   +0x280 output directory (cRZString)                        +0x2a8 name used in file names (cRZString)
//   +0x2d0 output type last used (1 bmp, 2 png, 3 jpg)         +0x2d4 gx.exe found by GetGXDirectory (never read)
//   +0x2d8 GX directory (cRZString)                            +0x300 loop mode: drive the animation tick manager
//                                                              and write the buffer out instead of stopping (default 1)
//   +0x304 take number (in the file names)                     +0x308 frames held in memory
//   +0x30c frame interval in ms                                +0x310 recording time in seconds (float)
//   +0x314 frames to record                                    +0x318 capture rectangle
//   +0x328 frame bitmaps (std::vector<cIGZBuffer*>)            +0x340 millisecond timer
//   +0x358 frames left (tick-manager mode)                     +0x35c ticks per frame (4, tick-manager mode)
//   +0x360 frame number of the first bitmap (file names)
//
// Quirks kept (all asserted by the test):
//  ANIMREC-1  GZOnMouseMove offsets the window by (x - mouseDownX, y - mouseDownY) and then writes the *old* anchors
//             back (0x100374c76), so a drag keeps adding the whole distance from the press point instead of the step.
//  ANIMREC-2  The "Delete bitmaps when finished" check box (+0x208) is never created by Init: WritePreferences only
//             writes the option when it exists and ReadPreferences stores it in a second field (+0x210) nobody reads.
//  ANIMREC-3  SetAnimationOutputDirectory ignores the new directory when `createIfMissing` is set and the *current*
//             directory already exists (0x100375513 early return), and turns an empty string into "/" (the trailing
//             separator is appended unconditionally when the string does not end in one).
//  ANIMREC-4  GetGXDirectory walks up from the application directory appending "Bin/GX/" to the *parent* of the last
//             component, then tests `<dir>gx.exe` (no separator added) and returns that FileExists result, not whether
//             the directory was found. The value it caches at +0x2d4 is never read.
//  ANIMREC-5  WriteAnimatedGraphicFile writes nothing: it only reports "AnimationFrames written OK.\n\n Output is in
//             %s" as a message box (0x100375de2).
//  ANIMREC-6  The Start button's caption becomes "Stop" while recording and the Span button is only enabled then; the
//             timer path never decrements +0x358, so a timer-driven recording only stops when the frame bitmaps run out
//             (and only when loop mode is off) or the user presses Stop.
//  ANIMREC-7  Stop asks the message box (style 3 = Yes/No) for confirmation and writes the frames only on Yes; the
//             frame offset (+0x360) is advanced by WriteOutputBitmaps itself, so pressing Stop twice writes the second
//             batch at the following numbers, and Start resets it to 0.
//  ANIMREC-8  The "Output Type" preference is read into +0x2d0 but never applied to the option group, which Init always
//             sets to 3 (jpg); the write-out therefore uses the group's value (the default jpg) and ignores the ini.
//
// Host seam: the original reaches the engine through SL::spSC4, SL::spGDriver, SL::spWinLocationSaver, SL::spView3DWin,
// RZ::WCM(), RZ::FO(), RZ::GS() and the animation tick manager service (0x86ad10ec / 0x86ad10ed). All of those go
// through IWinAnimationRecorderHost below (one method per original call, with the slot in the comment); the window
// manager operations (child add, message posting, the message box) go through the cIGZWinMgr the cGZWin was built with.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/Buffer.h"
#include "gz/Win.h"
#include "rz/cRZString.h"
#include "rz/cRZTimer.h"

class cSLWinLocationSaver;
namespace gz {
class cIGZWinBtn;
class cIGZWinOptGrp;
class cGZWinText;
class cGZWinTextEdit;
class cIGZWinCtrlMgr;
class IWinFont;
}  // namespace gz

namespace sc4 {

class cSC4WinResizableOutline;

// The window's own timer message (DoMessage) and the three button ids (the original compares GZOnCommand's parameter).
constexpr std::uint32_t kAnimationRecordTick = 0x8802d2ff;
constexpr std::uint32_t kAnimationRecordButtonStart = 0x80293a3, kAnimationRecordButtonClose = 0x80293a4,
                        kAnimationRecordButtonSpan = 0x80293a5;
constexpr std::uint32_t kAnimationRecordOutlineId = 0x6acb1c40;
enum AnimationRecordImageType : int { kAnimationRecordBMP = 1, kAnimationRecordPNG = 2, kAnimationRecordJPG = 3 };

// The colour-buffer extension of the GZ driver (nSGLDX7::IGDDriverColorBuffer: QueryInterface 0xe69bfe2a, the object at
// driver +8, vslot 0x18 = 0x1006079b8). The copy fills `destination` and answers it, or null on failure.
class IWinAnimationColorBuffer : public cIGZUnknown {
public:
    // One reference is held in +0x230 and released by Shutdown (the original QueryInterfaces the driver for it).
    virtual cIGZBuffer* CopyColorBuffer(int x, int y, int width, int height, cIGZBuffer* destination) = 0;
};

// The animation tick manager (service 0x86ad10ec, iid 0x86ad10ed = cISC4AnimationTickManager); the class is not
// recovered, so the three slots this window uses are a seam.
class IWinAnimationTick {
public:
    virtual ~IWinAnimationTick() = default;
    virtual void PauseAnimationSystem(bool paused) = 0;             // cSC4AnimationTickManager vslot 0x60
    virtual void SetStepTickTimeDelta(std::uint32_t microseconds) = 0;   // vslot 0x78
    virtual void StepAnimationSystem() = 0;                         // vslot 0x80
};

// Everything the window takes from the engine, one method per original call. Pointers are borrowed (the original held
// the service pointer only for the length of the call); one that is null makes the window skip that part.
class IWinAnimationRecorderHost {
public:
    virtual ~IWinAnimationRecorderHost() = default;
    // SL::spSC4 (cISC4App) vslot 0xe8 = GetAppIniFilePath; the preference file's path (false: no application).
    virtual bool AppIniFilePath(cRZString& out) = 0;
    // SL::spSC4 vslot 0x188 = GetAppDirectory (the GX-directory walk starts there).
    virtual bool AppDirectory(cRZString& out) = 0;
    // RZ::WCM() vslot 0x50 CreateButton / vslot 0x70 CreateOptGrp (gz::cIGZWinCtrlMgr, borrowed).
    virtual gz::cIGZWinCtrlMgr* WinCtrlMgr() = 0;
    // RZ::FO() vslot 0x48 = cIGZFontSys::FontAcquire("Arial MT", 0, 10, 0, 1); one reference, null when unavailable.
    virtual gz::IWinFont* CreateFont(const char* face, int type, int size, int style, bool reuse) = 0;
    // RZ::GS() vslot 0x20: a fresh buffer; one reference, null when the graphics system cannot make one.
    virtual cIGZBuffer* CreateBuffer() = 0;
    // SL::spSC4 vslot 0x148 (GetGimexFactory) and that factory's vslot 0x30 SaveToFile(path, buffer, flags); false when
    // there is no factory or the save failed.
    virtual bool SaveImage(const cIGZString& path, cIGZBuffer* buffer, std::uint32_t flags) = 0;
    // SL::spGDriver QueryInterface(0xe69bfe2a): the colour-buffer extension, or null.
    virtual IWinAnimationColorBuffer* ColorBuffer() = 0;
    // RZGetFramework()->GetSystemService(0x86ad10ec, 0x86ad10ed), or null.
    virtual IWinAnimationTick* AnimationTick() = 0;
    // SL::spWinLocationSaver (cSLWinLocationSaver), or null.
    virtual cSLWinLocationSaver* WinLocationSaver() = 0;
    // SL::spView3DWin: the window the recording outline is added to (cIGZWin::ChildAdd), or null.
    virtual cIGZWin* View3DWindow() = 0;
    // A window's draw buffer as the cIGZBuffer gz::DrawRectangleOutline (0x1002b3deb) draws on; the cGZWin seam only
    // knows IWinBuffer. Null skips the outline in GZPaint (the fill still happens).
    virtual cIGZBuffer* WindowBuffer(IWinBuffer* buffer) = 0;
};
IWinAnimationRecorderHost* WinAnimationRecorderHost();
void SetWinAnimationRecorderHost(IWinAnimationRecorderHost* host);

class cSC4WinAnimationRecorder : public cGZWin {
public:
    explicit cSC4WinAnimationRecorder(cIGZWinMgr* mgr);   // @ 0x100371f8a (+ 0x1003728a4 compiler twin)
    ~cSC4WinAnimationRecorder() override;                  // @ 0x1003728ae (+ 0x100372a68 / 0x100372a72 twins)

    bool Init() override;                                  // @ 0x100372aa0
    bool Shutdown() override;                              // @ 0x1003740c6
    bool DoMessage(cGZMessage& message) override;          // @ 0x100374d46
    bool GZPaint() override;                               // @ 0x100374c96
    bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) override;   // @ 0x100374bd4
    bool GZOnMouseUpL(int x, int y, std::uint32_t modifiers) override;     // @ 0x100374c0c
    bool GZOnMouseMove(int x, int y, std::uint32_t modifiers) override;    // @ 0x100374c30
    bool GZOnCommand(std::uint32_t command, std::uint64_t parameter) override;   // @ 0x100374f90

    void SetAnimationOutputDirectory(const cIGZString& directory, bool createIfMissing);   // @ 0x1003754f2

    // internals (public for the test)
    bool ReadPreferences();                                // @ 0x10037232e
    int GetGXDirectory();                                  // @ 0x100373b0e
    void UpdateStatusText();                               // @ 0x100373fa2
    bool Stop();                                           // @ 0x1003742fa
    void DestroyInternalBitmaps();                         // @ 0x1003745bc
    bool WritePreferences();                               // @ 0x100374632
    bool RecordCurrentFrame();                             // @ 0x100374e78
    bool Start();                                          // @ 0x100375042
    void GetCurrentCaptureArea(cRZRect& area);             // @ 0x1003755b0
    void FixAnimationName(cRZString& name);                // @ 0x100375626
    bool CreateInternalBitmaps();                          // @ 0x100375796
    bool WriteOutFinalBitmaps();                           // @ 0x1003758da
    bool WriteOutputBitmaps();                             // @ 0x100375a0a
    bool WriteAnimatedGraphicFile();                       // @ 0x100375de2

    // The controls and values the test inspects (names from the original).
    gz::cGZWinText* StatusText() const { return statusText_; }              // +0x158
    gz::cGZWinTextEdit* NameEdit() const { return nameEdit_; }              // +0x160
    gz::cGZWinTextEdit* RateEdit() const { return rateEdit_; }              // +0x190
    gz::cGZWinTextEdit* TimeEdit() const { return timeEdit_; }              // +0x1c0
    gz::cIGZWinBtn* StartButton() const { return startButton_; }           // +0x1f0
    gz::cIGZWinBtn* CloseButton() const { return closeButton_; }           // +0x1f8
    gz::cIGZWinBtn* SpanButton() const { return spanButton_; }             // +0x200
    gz::cIGZWinOptGrp* OutputTypeGroup() const { return outputTypeGroup_; }   // +0x218
    cSC4WinResizableOutline* Outline() const { return outline_; }           // +0x150
    const cRZString& Name() const { return name_; }                         // +0x168
    const cRZString& Rate() const { return rate_; }                         // +0x198
    const cRZString& Time() const { return time_; }                         // +0x1c8
    const cRZString& AnimationName() const { return animationName_; }       // +0x2a8
    const cRZString& OutputDirectory() const { return outputDir_; }         // +0x280
    const cRZString& GXDirectory() const { return gxDir_; }                 // +0x2d8
    const cRZString& PreferencesPath() const { return preferencesPath_; }   // +0x238
    const std::vector<cRZString>& WrittenFiles() const { return writtenFiles_; }   // +0x260
    const std::vector<cIGZBuffer*>& Bitmaps() const { return bitmaps_; }    // +0x328
    std::uint32_t FillColor() const { return fillColor_; }                  // +0x220
    int Take() const { return take_; }                                      // +0x304
    int FrameCount() const { return frameCount_; }                          // +0x308
    int FrameIntervalMs() const { return frameIntervalMs_; }                // +0x30c
    float RecordingSeconds() const { return recordingSeconds_; }            // +0x310
    int FrameTarget() const { return frameTarget_; }                        // +0x314
    int FramesLeft() const { return framesLeft_; }                          // +0x358
    int TickDivider() const { return ticksPerFrame_; }                      // +0x35c
    int FrameOffset() const { return frameOffset_; }                        // +0x360
    int OutputType() const { return outputType_; }                          // +0x2d0
    bool GXExecutableExists() const { return gxExecutableExists_; }          // +0x2d4
    bool DeleteBitmapsWhenFinished() const { return deleteBitmapsWhenFinished_; }   // +0x210
    bool LoopRecording() const { return loopRecording_; }                  // +0x300
    void SetLoopRecording(bool on) { loopRecording_ = on; }                // +0x300 (test knob)
    const cRZRect& CaptureArea() const { return captureArea_; }             // +0x318
    int DragAnchorX() const { return dragAnchorX_; }                        // +0x224
    int DragAnchorY() const { return dragAnchorY_; }                        // +0x228
    cRZTimer& Timer() { return timer_; }                                    // +0x340

private:
    gz::cGZWinText* AddLabel(const char* caption, const cRZRect& area, gz::IWinFont* font);
    gz::cGZWinTextEdit* AddEdit(const cRZString& text, const cRZRect& area);

    cSC4WinResizableOutline* outline_ = nullptr; // +0x150
    gz::cGZWinText* statusText_ = nullptr;       // +0x158 (the cIGZWinText view in the original)
    gz::cGZWinTextEdit* nameEdit_ = nullptr;     // +0x160
    cRZString name_{"Animation"};                // +0x168
    gz::cGZWinTextEdit* rateEdit_ = nullptr;     // +0x190
    cRZString rate_{"100"};                      // +0x198
    gz::cGZWinTextEdit* timeEdit_ = nullptr;     // +0x1c0
    cRZString time_{"5"};                        // +0x1c8
    gz::cIGZWinBtn* startButton_ = nullptr;      // +0x1f0
    gz::cIGZWinBtn* closeButton_ = nullptr;      // +0x1f8
    gz::cIGZWinBtn* spanButton_ = nullptr;       // +0x200
    gz::cIGZWinBtn* deleteBitmapsCheckBox_ = nullptr;   // +0x208 (never created: ANIMREC-2)
    bool deleteBitmapsWhenFinished_ = true;      // +0x210 (read, never used: ANIMREC-2)
    gz::cIGZWinOptGrp* outputTypeGroup_ = nullptr;    // +0x218
    std::uint32_t fillColor_ = 0;                // +0x220
    int dragAnchorX_ = 0;                        // +0x224
    int dragAnchorY_ = 0;                        // +0x228
    IWinAnimationColorBuffer* captureDriver_ = nullptr;   // +0x230
    cRZString preferencesPath_;                  // +0x238
    std::vector<cRZString> writtenFiles_;        // +0x260
    std::uint32_t maxFrames_ = 0x80;             // +0x278
    std::uint32_t maxBytes_ = 0x8000000;         // +0x27c
    cRZString outputDir_;                        // +0x280
    cRZString animationName_;                    // +0x2a8
    int outputType_ = kAnimationRecordJPG;       // +0x2d0
    bool gxExecutableExists_ = false;            // +0x2d4
    cRZString gxDir_;                            // +0x2d8
    bool loopRecording_ = true;                   // +0x300
    int take_ = 0;                               // +0x304
    int frameCount_ = 0;                         // +0x308
    int frameIntervalMs_ = 0;                    // +0x30c
    float recordingSeconds_ = 0.0f;              // +0x310
    int frameTarget_ = 0;                        // +0x314
    cRZRect captureArea_;                        // +0x318
    std::vector<cIGZBuffer*> bitmaps_;           // +0x328
    cRZTimer timer_{cRZTimer::kMilliseconds};    // +0x340
    int framesLeft_ = 0;                         // +0x358
    int ticksPerFrame_ = 0;                      // +0x35c
    int frameOffset_ = 0;                        // +0x360
};

}  // namespace sc4
