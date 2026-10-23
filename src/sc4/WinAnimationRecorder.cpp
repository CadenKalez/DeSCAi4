// cSC4WinAnimationRecorder — see sc4/WinAnimationRecorder.h for the layout, the quirks and the host seam. Every function
// below is the original's, with the address in its comment; the four hand-written helpers at the top of the anonymous
// namespace are the port's (they stand in for one repeated inline sequence each, listed where they are used).
#include "sc4/WinAnimationRecorder.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "gz/Graph.h"          // gz::DrawRectangleOutline @ 0x1002b3deb
#include "gz/WinBtn.h"
#include "gz/WinCtrlMgr.h"
#include "gz/WinFont.h"
#include "gz/WinMsgBox.h"
#include "gz/WinOptGrp.h"
#include "gz/WinText.h"
#include "gz/WinTextEdit.h"
#include "rz/RZStringUtils.h"
#include "rz/cRZDirectory.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"
#include "sc4/WinLocationSaver.h"
#include "sc4/WinResizableOutline.h"

namespace sc4 {

namespace {

IWinAnimationRecorderHost* g_host = nullptr;

constexpr const char* kSection = "Animation Recorder";
constexpr const char* kTitle = "Animation Recorder";
constexpr std::uint32_t kLockFill = 0x8001, kLockOutline = 0x8008;

// The originals compare these button / message ids; the values live in the original's command table.
constexpr std::uint32_t kMsgBoxStyleYesNo = 3;

cIGZWinMgr* Manager(cGZWin* window) { return window->GetWindowManager(); }

// The original's `cRZString::Sprintf("%s%s %03d_%03d.<ext>", dir, name, take, frame)`.
void FormatFrameName(cRZString& out, const cRZString& directory, const cRZString& name, int take, int frame,
                     int imageType) {
    if (imageType == kAnimationRecordBMP) {
        out.Sprintf("%s%s %03d_%03d.bmp", directory.Data(), name.Data(), take, frame);
    } else if (imageType == kAnimationRecordPNG) {
        out.Sprintf("%s%s %03d_%03d.png", directory.Data(), name.Data(), take, frame);
    } else {
        out.Sprintf("%s%s %03d_%03d.jpg", directory.Data(), name.Data(), take, frame);
    }
}

// One message box with the window's fixed title (cGZWinMgrBase::GZMsgBox, window-manager vslot 0x160); the answer.
int Ask(cGZWin* window, const cRZString& text, std::uint32_t style) {
    cIGZWinMgr* mgr = Manager(window);
    if (!mgr) return 0;
    return mgr->GZMsgBox(text, cRZString(kTitle), style, true, 0);
}

}  // namespace

IWinAnimationRecorderHost* WinAnimationRecorderHost() { return g_host; }
void SetWinAnimationRecorderHost(IWinAnimationRecorderHost* host) { g_host = host; }

// @ 0x100371f8a. The original's cGZWin constructor takes the window manager from RZ::WM(); here it is the argument.
cSC4WinAnimationRecorder::cSC4WinAnimationRecorder(cIGZWinMgr* mgr) : cGZWin(mgr) {
    if (g_host) g_host->AppIniFilePath(preferencesPath_);   // SL::spSC4 vslot 0xe8
    ReadPreferences();
}

// @ 0x1003728ae. The original frees the seven cRZString members, the written-file vector, the frame-bitmap vector's
// storage and the timer here; the members do that themselves, and cGZWin's destructor deletes the children.
cSC4WinAnimationRecorder::~cSC4WinAnimationRecorder() = default;

// @ 0x10037232e. Section "Animation Recorder" of the application ini file (the original builds the section, key and value
// cRZStrings on the stack; cRZString is std::string here).
bool cSC4WinAnimationRecorder::ReadPreferences() {
    cRZIniFile ini(preferencesPath_);
    const cRZString section(kSection);
    cRZString key, value;
    int number = 0;

    if (ini.ReadEntry(section, cRZString("Name"), value)) name_ = value;
    if (ini.ReadEntry(section, cRZString("Rate"), value)) rate_ = value;
    if (ini.ReadEntry(section, cRZString("Time"), value)) time_ = value;
    if (ini.ReadNumber(section, cRZString("Run"), number)) take_ = number;
    if (ini.ReadNumber(section, cRZString("Delete bitmaps when finished"), number)) deleteBitmapsWhenFinished_ = number != 0;
    if (ini.ReadEntry(section, cRZString("Output Directory"), value)) outputDir_ = value;
    if (ini.ReadEntry(section, cRZString("GX Directory"), value)) gxDir_ = value;
    if (ini.ReadNumber(section, cRZString("Output Type"), number)) {
        const int clamped = number < 0 ? 0 : number;      // max(0, n)
        outputType_ = clamped < 4 ? clamped : kAnimationRecordJPG;   // min(3, n)
    }
    return true;
}

// @ 0x100373b0e. Walks up from the application directory: appDir is split into (last component, parent); the candidate
// is `<parent>Bin/GX/`, and when it is missing the search moves to the parent. The cached +0x2d4 is the FileExists of
// `<gxDir>gx.exe` (no separator is added) and is the return value; nothing reads it. See ANIMREC-4.
int cSC4WinAnimationRecorder::GetGXDirectory() {
    bool found = gxDir_.Strlen() != 0 && cRZDirectory::DoesDirectoryExist(gxDir_);
    if (!found) {
        gxDir_ = "<unknown>";
        if (g_host) {
            cRZString appDir;
            g_host->AppDirectory(appDir);                                  // SL::spSC4 vslot 0x188
            cRZString parts[2];
            int count = 2;
            for (;;) {
                if (!SplitDirectory(appDir, parts, count) || count != 2) break;
                cRZString candidate = parts[1];                            // the parent directory (ends in '/')
                candidate.AppendChars("Bin", 3);
                candidate.AppendChars("/", 1);
                candidate.AppendChars("GX", 2);
                candidate.AppendChars("/", 1);
                if (cRZDirectory::DoesDirectoryExist(candidate)) {
                    gxDir_ = candidate;
                    break;
                }
                appDir = parts[1];
            }
        }
    }
    std::string exePath(gxDir_.Data());
    exePath += "gx.exe";
    gxExecutableExists_ = cRZFile::FileExists(cRZString(exePath.c_str()));
    return gxExecutableExists_ ? 1 : 0;
}

// @ 0x100373fa2. Two fixed texts: the idle one is a literal, the recording one is Sprintf'd with the frame count.
void cSC4WinAnimationRecorder::UpdateStatusText() {
    if (!statusText_) return;
    if (!timer_.IsTimerRunning()) {
        statusText_->SetCaption(cRZString("Status: Stopped.\nFrame count: 0"));
    } else {
        cRZString text;
        text.Sprintf("Status: Recording.\nFrame count: %d", frameCount_);
        statusText_->SetCaption(text);
    }
}

// The repeated label block of Init (four times there, plus the status text): create, AddRef, area, caption, the
// ignore-cursor flag, no background, the shared font and a black text colour, then ChildAdd. The four labels are
// Released after the ChildAdd and the status text is not (it keeps the recorder's reference until Shutdown).
gz::cGZWinText* cSC4WinAnimationRecorder::AddLabel(const char* caption, const cRZRect& area, gz::IWinFont* font) {
    auto* label = new gz::cGZWinText(GetWindowManager());
    label->AddRef();
    label->AsIGZWin()->SetArea(area);
    label->SetCaption(cRZString(caption));
    label->AsIGZWin()->SetFlag(kWinIgnoreCursor, true);
    label->SetBackgroundOpaque(false);
    label->SetFont(font);
    label->SetTextColor(gz::cIGZWinText::kColorText, MakeFillColor(0, 0, 0));
    ChildAdd(label->AsIGZWin());
    return label;
}

// The repeated edit block of Init (three times there) with flags 0x11ff, the single-line flag 0x200 and the initial text.
gz::cGZWinTextEdit* cSC4WinAnimationRecorder::AddEdit(const cRZString& text, const cRZRect& area) {
    auto* edit = new gz::cGZWinTextEdit(GetWindowManager(), 0x11ff);
    edit->AddRef();
    edit->AsIGZWin()->SetArea(area);
    edit->SetFlag(0x200);                     // cIGZWinTextEdit::SetFlag(kTESingleLine) (vslot 0x260)
    edit->SetText(text, false);               // cIGZWinTextEdit::SetText(cIGZString const&, bool) (vslot 0x188)
    ChildAdd(edit->AsIGZWin());
    return edit;
}

// @ 0x100372aa0.
bool cSC4WinAnimationRecorder::Init() {
    if (get_init_flag()) return true;
    IWinAnimationRecorderHost* host = g_host;
    gz::cIGZWinCtrlMgr* wcm = host ? host->WinCtrlMgr() : nullptr;
    gz::IWinFont* font = host ? host->CreateFont("Arial MT", 0, 10, 0, true) : nullptr;   // RZ::FO() vslot 0x48

    SetID(0x6acb1af1);
    SetFlag(kWinPrivateBuffer, true);                         // 0x10000
    SetArea(4, 4, 0x120, 0x67);
    SetFillColor(MakeFillColor(0x8c, 0x9a, 0xae));
    fillColor_ = MakeFillColor(0, 0, 0);
    cGZWin::Init();

    GetGXDirectory();

    // the five text windows: Name / Rate / Time / Output labels and the status line
    gz::cGZWinText* label = AddLabel("Name:", cRZRect{3, 7, 0x36, 0x1b}, font);
    label->Release();
    label = AddLabel("Rate (ms):", cRZRect{3, 0x1b, 0x35, 0x2d}, font);
    label->Release();
    label = AddLabel("Time (s):", cRZRect{0x69, 0x1b, 0x98, 0x2d}, font);
    label->Release();
    label = AddLabel("Output:", cRZRect{3, 0x30, 0x34, 0x40}, font);
    label->Release();
    statusText_ = AddLabel("", cRZRect{3, 0x43, 100, 0x62}, font);
    UpdateStatusText();

    // the three single-line edits
    nameEdit_ = AddEdit(name_, cRZRect{0x39, 6, 0xb7, 0x16});
    rateEdit_ = AddEdit(rate_, cRZRect{0x39, 0x1b, 0x67, 0x2c});
    timeEdit_ = AddEdit(time_, cRZRect{0x92, 0x1b, 0xb7, 0x2c});

    // the buttons (window control manager vslot 0x50 = CreateButton(id, caption, image, area, key))
    if (wcm) {
        const cRZString empty;
        startButton_ = wcm->CreateButton(kAnimationRecordButtonStart, cRZString("Start"), nullptr, nullptr, 0);
        if (startButton_) {
            startButton_->SetBtnFlag(gz::kBtnFitToContent, false);
            startButton_->AsIGZWin()->SetArea(0x60, 0x46, 0x98, 0x5c);
            ChildAdd(startButton_->AsIGZWin());
        }
        closeButton_ = wcm->CreateButton(kAnimationRecordButtonClose, cRZString("Close"), nullptr, nullptr, 0);
        if (closeButton_) {
            closeButton_->SetBtnFlag(gz::kBtnFitToContent, false);
            closeButton_->AsIGZWin()->SetArea(0x9f, 0x46, 0xd7, 0x5c);
            ChildAdd(closeButton_->AsIGZWin());
        }
        spanButton_ = wcm->CreateButton(kAnimationRecordButtonSpan, cRZString("Span"), nullptr, nullptr, 0);
        if (spanButton_) {
            spanButton_->AsIGZWin()->SetFlag(kWinEnabled, false);
            spanButton_->SetFontColor(4, MakeFillColor(0x80, 0x80, 0x80));
            spanButton_->SetBtnFlag(gz::kBtnFitToContent, false);
            spanButton_->AsIGZWin()->SetArea(0xde, 0x46, 0x116, 0x5c);
            ChildAdd(spanButton_->AsIGZWin());
        }

        // the output-type radio group (vslot 0x70 = CreateOptGrp(id, caption, area))
        outputTypeGroup_ = wcm->CreateOptGrp(0, empty, nullptr);
        if (outputTypeGroup_) {
            outputTypeGroup_->SetOptGrpFlag(1, false);
            outputTypeGroup_->SetOptGrpFlag(2, true);
            outputTypeGroup_->SetOptGrpFlag(4, false);
            outputTypeGroup_->SetOptGrpFlag(8, false);
            outputTypeGroup_->AsIGZWin()->SetArea(0x35, 0x2c, 0x116, 0x44);
            outputTypeGroup_->OptBtnAdd(1, cRZString("bmp"), 0, nullptr, nullptr);
            outputTypeGroup_->OptBtnAdd(2, cRZString("png"), 0, nullptr, nullptr);
            outputTypeGroup_->OptBtnAdd(3, cRZString("jpg"), 0, nullptr, nullptr);
            outputTypeGroup_->SetValue(3);
            ChildAdd(outputTypeGroup_->AsIGZWin());
        }
    }

    // the recording outline. The original's class constructor (not in the recovered set) sets resizing / moving = 1 and
    // colours -0xff at +0x150..+0x160; the setter called at the end of this block sets the same two flags, so the port
    // relies on that one call instead of poking the fields.
    if (!outline_) {
        outline_ = new cSC4WinResizableOutline(GetWindowManager());
        outline_->AddRef();
        outline_->SetID(kAnimationRecordOutlineId);
        outline_->SetFlag(kWinPrivateBuffer, true);
        outline_->SetArea(100, 100, 0x106, 0xde);
        outline_->SetColors(static_cast<std::int32_t>(MakeFillColor(0x8c, 0x9a, 0xae)),
                            static_cast<std::int32_t>(MakeFillColor(0xbe, 0xcc, 0xe0)),
                            static_cast<std::int32_t>(MakeFillColor(0xbe, 0x5a, 0x5a)));
        outline_->SetResizingAndMoving(true, true);
        if (host) {
            if (cIGZWin* view = host->View3DWindow()) view->ChildAdd(outline_);   // SL::spView3DWin vslot 0x70
        }
    }

    if (font) font->Release();

    // the colour-buffer extension of the GZ driver (SL::spGDriver QueryInterface(0xe69bfe2a)); one reference held
    if (!captureDriver_ && host) captureDriver_ = host->ColorBuffer();

    if (host) {
        if (cSLWinLocationSaver* saver = host->WinLocationSaver()) {
            int x = 0, y = 0;
            if (outline_ && saver->GetWindowRestoreLocation(outline_->GetID() + 1, x, y, true)) {   // vslot 0x40
                outline_->SetSize(x, y);                                     // the size is stored under id + 1
            }
            if (outline_) saver->RestoreWindowLocation(outline_, true);      // vslot 0x48
            saver->RestoreWindowLocation(this, true);
        }
    }

    if (outline_) {
        outline_->PullToFront();
        outline_->SetFlag(kWinFlag800, false);
    }
    PullToFront();
    SetFlag(kWinFlag800, false);
    return true;
}

// @ 0x1003740c6.
bool cSC4WinAnimationRecorder::Shutdown() {
    if (get_init_flag()) {
        if (timer_.IsTimerRunning()) Stop();
        if (g_host) {
            if (cSLWinLocationSaver* saver = g_host->WinLocationSaver()) {
                const int w = outline_ ? outline_->GetW() : 0;
                const int h = outline_ ? outline_->GetH() : 0;
                if (outline_) {
                    saver->SaveWindowLocation(outline_);                            // vslot 0x28
                    saver->SaveWindowLocation(outline_->GetID() + 1, w, h);        // vslot 0x30
                }
                saver->SaveWindowLocation(this);
            }
        }
        DestroyInternalBitmaps();
        WritePreferences();
        if (captureDriver_) {
            captureDriver_->Release();
            captureDriver_ = nullptr;
        }
        if (outline_) {
            if (g_host) {
                if (cIGZWin* view = g_host->View3DWindow()) view->ChildRemove(outline_);   // vslot 0x78
            }
            outline_->Release();
            outline_ = nullptr;
        }
        auto release = [](auto*& window) {
            if (window) {
                window->Release();
                window = nullptr;
            }
        };
        release(statusText_);
        release(nameEdit_);
        release(rateEdit_);
        release(timeEdit_);
        release(startButton_);
        release(closeButton_);
        release(spanButton_);
        release(deleteBitmapsCheckBox_);
        release(outputTypeGroup_);
    }
    return cGZWin::Shutdown();
}

// @ 0x1003742fa. Answers whether the timer was running. Quirk ANIMREC-7: the Yes/No box gates the write-out.
bool cSC4WinAnimationRecorder::Stop() {
    if (!timer_.IsTimerRunning()) return false;
    timer_.Reset();
    framesLeft_ = 0;
    if (frameCount_ != 0) {
        if (Ask(this, cRZString("Would you like to write out the animation frames?"), kMsgBoxStyleYesNo) ==
            static_cast<int>(gz::kMsgBoxYes)) {
            WriteOutFinalBitmaps();
        }
    }
    DestroyInternalBitmaps();
    if (startButton_) startButton_->AsIGZWin()->SetCaption(cRZString("Start"));
    if (spanButton_) spanButton_->AsIGZWin()->SetFlag(kWinEnabled, false);
    UpdateStatusText();
    if (outline_) {
        outline_->SetResizingAndMoving(true, true);
        outline_->SetFlag(kWinIgnoreCursor, false);                    // 0x200000
    }
    if (g_host && loopRecording_) {
        if (IWinAnimationTick* tick = g_host->AnimationTick()) tick->PauseAnimationSystem(false);
    }
    return true;
}

// @ 0x1003745bc. Releases the frame bitmaps and empties the vector (the original does not free its storage).
void cSC4WinAnimationRecorder::DestroyInternalBitmaps() {
    for (cIGZBuffer* buffer : bitmaps_) {
        if (buffer) buffer->Release();
    }
    bitmaps_.clear();
}

// @ 0x100374632. Writes the same section ReadPreferences reads. The two edit texts come from the windows' captions
// (cGZWin::GetCaption, vslot 0x248) and the "Delete bitmaps when finished" number from the check box that Init never
// creates (ANIMREC-2), so that entry is only written when a caller put a button there.
bool cSC4WinAnimationRecorder::WritePreferences() {
    cRZIniFile ini(preferencesPath_);
    const cRZString section(kSection);
    cRZString key, value;

    if (nameEdit_) {
        value.AssignFromIGZString(nameEdit_->GetCaption());
        ini.WriteEntry(section, cRZString("Name"), value);
    }
    if (rateEdit_) {
        value.AssignFromIGZString(rateEdit_->GetCaption());
        ini.WriteEntry(section, cRZString("Rate"), value);
    }
    if (timeEdit_) {
        value.AssignFromIGZString(timeEdit_->GetCaption());
        ini.WriteEntry(section, cRZString("Time"), value);
    }
    if (deleteBitmapsCheckBox_) ini.WriteNumber(section, cRZString("Delete bitmaps when finished"), deleteBitmapsCheckBox_->IsChecked());
    ini.WriteNumber(section, cRZString("Run"), take_);
    ini.WriteEntry(section, cRZString("Output Directory"), outputDir_);
    ini.WriteEntry(section, cRZString("GX Directory"), gxDir_);
    if (outputTypeGroup_) ini.WriteNumber(section, cRZString("Output Type"), static_cast<int>(outputTypeGroup_->GetValue()));
    return true;
}

// @ 0x100374bd4. The anchors are the mouse-down point; see ANIMREC-1 for what GZOnMouseMove does with them.
bool cSC4WinAnimationRecorder::GZOnMouseDownL(int x, int y, std::uint32_t) {
    GZSetCapture();
    if (GZAmCapturing()) {
        dragAnchorX_ = x;
        dragAnchorY_ = y;
    }
    return true;
}

// @ 0x100374c0c.
bool cSC4WinAnimationRecorder::GZOnMouseUpL(int, int, std::uint32_t) {
    if (GZAmCapturing()) GZReleaseCapture();
    return true;
}

// @ 0x100374c30. ANIMREC-1: the old anchors are written back, so every move applies the whole distance from the press.
bool cSC4WinAnimationRecorder::GZOnMouseMove(int x, int y, std::uint32_t) {
    if (GZAmCapturing()) {
        const int anchorX = dragAnchorX_;
        const int anchorY = dragAnchorY_;
        GZWinOffset(x - anchorX, y - anchorY);
        dragAnchorX_ = anchorX;
        dragAnchorY_ = anchorY;
    }
    return true;
}

// @ 0x100374c96. Fill with the outline colour, then draw its 1-pixel frame when the draw buffer can be locked.
bool cSC4WinAnimationRecorder::GZPaint() {
    IWinBuffer* buffer = drawBuffer_;
    if (!buffer) return true;
    if (buffer->Lock(kLockFill)) {
        buffer->Fill(fillColor_, drawArea_);
        buffer->Unlock(kLockFill);
    }
    if (buffer->Lock(kLockOutline)) {
        cIGZBuffer* target = g_host ? g_host->WindowBuffer(buffer) : nullptr;
        if (target) gz::DrawRectangleOutline(target, drawArea_, fillColor_, 1);
        buffer->Unlock(kLockOutline);
    }
    return true;
}

// @ 0x100374d46. The window's own timer message: either the animation tick manager (four ticks per frame) or the
// millisecond timer drives RecordCurrentFrame; the message is posted again at the end (vslot 0x88 on the manager).
bool cSC4WinAnimationRecorder::DoMessage(cGZMessage& message) {
    if (message.type != kAnimationRecordTick) return cGZWin::DoMessage(message);
    if (!timer_.IsTimerRunning()) return true;

    IWinAnimationTick* tick = (g_host && loopRecording_) ? g_host->AnimationTick() : nullptr;
    if (tick) {
        if (--ticksPerFrame_ == 0) {
            RecordCurrentFrame();
            tick->StepAnimationSystem();
            if (--framesLeft_ == 0) {
                Stop();
            } else {
                ticksPerFrame_ = 4;
            }
        }
    } else {
        const std::uint32_t elapsed = timer_.GetElapsedTimeU32();
        if (static_cast<std::uint32_t>(frameIntervalMs_) <= elapsed) {
            RecordCurrentFrame();
            timer_.Restart();
            if (--framesLeft_ == 0) Stop();
        }
    }
    PostMsg(this, kAnimationRecordTick, 0, 0, 0);
    return true;
}

// @ 0x100374e78. Copies the outline's rectangle (inset by one pixel, at least 2 x 2) out of the screen buffer into the
// current frame bitmap. When the frame list is full: loop mode writes the batch out and starts over, otherwise Stop.
bool cSC4WinAnimationRecorder::RecordCurrentFrame() {
    if (!captureDriver_ || !outline_) return false;
    int index = frameCount_;
    if (bitmaps_.size() <= static_cast<std::size_t>(index)) {
        if (!loopRecording_) {
            Stop();
            return false;
        }
        WriteOutputBitmaps();
        frameCount_ = 0;
        index = 0;
    }
    if (static_cast<std::size_t>(index) < bitmaps_.size() && bitmaps_[index]) {
        const int left = outline_->GetArea().left, top = outline_->GetArea().top;
        const int right = outline_->GetArea().right, bottom = outline_->GetArea().bottom;
        captureArea_.left = left + 1;
        captureArea_.top = top + 1;
        captureArea_.right = (right - 1 > left + 1) ? right - 1 : left + 2;
        captureArea_.bottom = (bottom - 1 > top + 1) ? bottom - 1 : top + 2;
        if (captureDriver_->CopyColorBuffer(captureArea_.left, captureArea_.top, captureArea_.right - captureArea_.left,
                                            captureArea_.bottom - captureArea_.top, bitmaps_[index])) {
            ++frameCount_;
            UpdateStatusText();
            return true;
        }
    }
    return false;
}

// @ 0x100374f90. The buttons post command 0x287259f6 (kBtnCmdActivated) with their id as the parameter.
bool cSC4WinAnimationRecorder::GZOnCommand(std::uint32_t command, std::uint64_t parameter) {
    if (command == gz::kBtnCmdActivated) {
        if (parameter == kAnimationRecordButtonSpan) {
            if (timer_.IsTimerRunning()) RecordCurrentFrame();
        } else if (parameter == kAnimationRecordButtonClose) {
            if (timer_.IsTimerRunning()) Stop();
            PostMsg(GetParentWin(), 1, reinterpret_cast<std::uint64_t>(this), 0, 0);   // kGZMsgDeleteChild
        } else if (parameter == kAnimationRecordButtonStart) {
            if (!timer_.IsTimerRunning()) {
                Start();
            } else {
                Stop();
            }
        }
    }
    return true;
}

// @ 0x100375042. Reads the edits (only the name is kept: the rate and the time go through a local cRZString), builds the
// frame list, starts the timer and hands the pacing to the animation tick manager when loop mode is on. ANIMREC-3/6.
bool cSC4WinAnimationRecorder::Start() {
    if (timer_.IsTimerRunning()) return false;
    if (!captureDriver_) {
        Ask(this, cRZString("Screencapture cIGZGDriver extension not present."), gz::kMsgBoxOK_);
        return false;
    }
    if (!nameEdit_ || !rateEdit_ || !timeEdit_ || !startButton_ || !spanButton_ || !outline_) return false;   // port guard
    if (outputDir_.Strlen() != 0 && !cRZDirectory::DoesDirectoryExist(outputDir_)) {
        // ANIMREC-3 keeps the successful-exists path running as well, but the original also falls back to the app
        // directory when the creation failed.
        cRZDirectory::CreateDirectoryA(outputDir_);
        if (!cRZDirectory::DoesDirectoryExist(outputDir_)) {
            if (g_host) g_host->AppDirectory(outputDir_);
        }
    }

    cRZString edited;
    nameEdit_->GetText(animationName_);            // the only edit Start keeps
    FixAnimationName(animationName_);
    ++take_;
    frameCount_ = 0;
    rateEdit_->GetText(edited);
    frameIntervalMs_ = static_cast<int>(RZAtofEnglishA(edited.Data()));
    timeEdit_->GetText(edited);
    recordingSeconds_ = static_cast<float>(RZAtofEnglishA(edited.Data()));
    frameTarget_ = static_cast<int>((1000.0f / static_cast<float>(frameIntervalMs_)) * recordingSeconds_);
    writtenFiles_.clear();

    int frames = frameTarget_;
    if (!loopRecording_ && static_cast<int>(maxFrames_) < frames) {
        frameTarget_ = static_cast<int>(maxFrames_);
        frames = frameTarget_;
    }
    framesLeft_ = frames;
    GetCurrentCaptureArea(captureArea_);
    if (!CreateInternalBitmaps()) return false;
    timer_.Restart();

    startButton_->AsIGZWin()->SetCaption(cRZString("Stop"));
    spanButton_->AsIGZWin()->SetFlag(kWinEnabled, true);
    PostMsg(this, kAnimationRecordTick, 0, 0, 0);
    UpdateStatusText();
    outline_->SetResizingAndMoving(false, true);
    outline_->SetFlag(kWinIgnoreCursor, true);
    if (g_host && loopRecording_) {
        if (IWinAnimationTick* tick = g_host->AnimationTick()) {
            tick->PauseAnimationSystem(true);
            tick->SetStepTickTimeDelta(static_cast<std::uint32_t>(frameIntervalMs_ * 1000));
            ticksPerFrame_ = 4;
        }
    }
    frameOffset_ = 0;
    return true;
}

// @ 0x1003754f2. ANIMREC-3: with `createIfMissing` the new directory is ignored while the current one exists, and the
// trailing separator is appended unconditionally when the string does not already end in '/'.
void cSC4WinAnimationRecorder::SetAnimationOutputDirectory(const cIGZString& directory, bool createIfMissing) {
    if (createIfMissing && outputDir_.Strlen() != 0) {
        if (cRZDirectory::DoesDirectoryExist(outputDir_)) return;
        if (cRZDirectory::CreateDirectoryA(outputDir_)) return;
    }
    outputDir_.AssignFromIGZString(directory);
    if (outputDir_.Strlen() != 0 && outputDir_.Data()[outputDir_.Strlen() - 1] == '/') return;
    outputDir_.AppendChars("/", 1);
}

// @ 0x1003755b0. The outline's area inset by one pixel, grown back to 2 x 2 when the inset would collapse it.
void cSC4WinAnimationRecorder::GetCurrentCaptureArea(cRZRect& area) {
    if (!outline_) return;                                         // port guard
    const cRZRect& source = outline_->GetArea();
    const int left = source.left, top = source.top, right = source.right, bottom = source.bottom;
    area = source;
    area.left = left + 1;
    area.top = top + 1;
    area.right = right - 1;
    area.bottom = bottom - 1;
    if (right - 1 <= left + 1) area.right = left + 2;
    if (bottom - 1 <= top + 1) area.bottom = top + 2;
}

// @ 0x100375626. Truncates to 128 bytes, then keeps only alphanumeric characters and spaces (isalnum / isspace).
void cSC4WinAnimationRecorder::FixAnimationName(cRZString& name) {
    if (name.Strlen() > 0x80) name.Resize(0x80);
    cRZString filtered;
    const char* chars = name.Data();
    const std::uint32_t length = name.Strlen();
    for (std::uint32_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(chars[i]);
        if (std::isalnum(c) != 0 || std::isspace(c) != 0) filtered.AppendChars(reinterpret_cast<const char*>(&chars[i]), 1);
    }
    name = filtered;
}

// @ 0x100375796. Makes min(frameTarget, 128) buffers of the capture size unless the 128 MB cap cuts the count down; a null
// buffer or a failed Init throws the whole set away and answers false. (The original divides by the pixel count; a
// degenerate rectangle is guarded here instead.)
bool cSC4WinAnimationRecorder::CreateInternalBitmaps() {
    IWinAnimationRecorderHost* host = g_host;
    if (!host) {
        DestroyInternalBitmaps();
        return false;
    }
    std::uint32_t count = static_cast<std::uint32_t>(frameTarget_);
    if (maxFrames_ < count) count = maxFrames_;
    const std::uint32_t width = static_cast<std::uint32_t>(captureArea_.right - captureArea_.left);
    const std::uint32_t height = static_cast<std::uint32_t>(captureArea_.bottom - captureArea_.top);
    const std::uint32_t bytes = width * height;
    if (bytes == 0) return true;                                   // port guard (the original would divide by zero)
    if (maxBytes_ < bytes * count) count = maxBytes_ / bytes;
    if (count == 0) return true;

    bool ok = true;
    for (std::uint32_t i = 0; i < count; ++i) {
        cIGZBuffer* buffer = host->CreateBuffer();                  // RZ::GS() vslot 0x20
        if (!buffer) {
            ok = false;
            break;
        }
        // the original passes {9, 2} (ARGB8888, 32 bits) packed into one register; this cGZBuffer takes the bit count
        ok = buffer->Init(width, height, cGZBufferColorType{kBufferColorARGB8888, 32});
        if (!ok) {
            buffer->Release();
            break;
        }
        bitmaps_.push_back(buffer);
    }
    if (ok) return true;
    DestroyInternalBitmaps();
    return false;
}

// @ 0x1003758da. Write the frames, then the "animation" report; any failure shows "Animation write failed.".
bool cSC4WinAnimationRecorder::WriteOutFinalBitmaps() {
    if (WriteOutputBitmaps() && WriteAnimatedGraphicFile()) return true;
    Ask(this, cRZString("Animation write failed."), gz::kMsgBoxOK_);
    return false;
}

// @ 0x100375a0a. Saves each held frame through the Gimex factory (vslot 0x30 = SaveToFile(path, buffer, 100)), replacing
// files that already exist, and remembers the names. The frame offset advances by the whole frame count even when a write
// fails (the original reloads +0x308 after every iteration); the one case the seam cannot tell apart is "no factory",
// which the original answers before touching the offset — here it looks like a failed save.
bool cSC4WinAnimationRecorder::WriteOutputBitmaps() {
    if (!g_host) return false;
    const int count = frameCount_;
    bool ok = true;
    for (int i = 0; i < count; ++i) {
        cIGZBuffer* buffer = bitmaps_[static_cast<std::size_t>(i)];
        if (!buffer) {
            ok = false;
            break;
        }
        outputType_ = outputTypeGroup_ ? static_cast<int>(outputTypeGroup_->GetValue()) : outputType_;
        cRZString filename;
        FormatFrameName(filename, outputDir_, animationName_, take_, frameOffset_ + i, outputType_);
        if (cRZFile::FileExists(filename)) cRZFile::Remove(filename);
        ok = g_host->SaveImage(filename, buffer, 100);   // Gimex factory vslot 0x30
        if (ok) writtenFiles_.push_back(filename);
        if (!ok) break;
    }
    frameOffset_ += count;
    return ok;
}

// @ 0x100375de2. ANIMREC-5: nothing is written here — the "animation" is reported as written when the output type is one
// of the three image types.
bool cSC4WinAnimationRecorder::WriteAnimatedGraphicFile() {
    if (outputTypeGroup_) outputType_ = static_cast<int>(outputTypeGroup_->GetValue());
    if (static_cast<std::uint32_t>(outputType_ - 1) < 3) {
        cRZString text;
        text.Sprintf("AnimationFrames written OK.\n\n Output is in %s", outputDir_.Data());
        Ask(this, text, gz::kMsgBoxOK_);
        return true;
    }
    return false;
}

}  // namespace sc4
