// cGZWinTextEdit — the multi-line text edit control (0x1001fb418..0x1002674f8, 145 functions, iid 0x231a1c57 for the
// cIGZWinTextEdit view at +0x150). It keeps the text in one string plus an array of line start offsets; lines are cut at
// newlines, or word-wrapped to the visible width, or (single-line mode) not at all. It supports a selection, overwrite
// mode, undo / redo records, clipboard copy / cut / paste, optional vertical and horizontal scroll bars created on demand,
// a blinking caret driven by a timer, and notification messages (type 0x9b1552d5..da, p1 = the view) to its target.
//
// Flags (0x1dc): 0x1 editable, 0x2 word wrap, 0x4 horizontal scroll bar allowed, 0x8 vertical scroll bar allowed, 0x10
// outline, 0x20 filled, 0x40 caret blinks, 0x80 Insert key toggles overwrite, 0x100 undo records, 0x200 single line, 0x400
// notify on Return, 0x800 notify on text change and on Up / Down in single-line mode, 0x1000 copy / cut / paste, 0x2000 password (echoes the mask character),
// 0x4000 overwrite mode, 0x10000 keep the selection highlighted without focus. SetFlags keeps them consistent: a password
// field is single line without wrap / bar, single line drops wrap and the bar, wrap drops the horizontal bar.
// Notifications: 0x9b1552d5 key (p2 = key, Return 0xd, Up 0x26, Down 0x28), d6 mouse down L, d7 mouse down R (not editable),
// d8 text changed, d9 kill focus, da set focus; Escape sends command-style messages 5 / 6 with p1 0x1b.
//
// Quirks kept: RemoveText(line0, col0, line1, col1) removes |i1 - i0| characters starting at the FIRST index, even when it
// is the later one, and ignores the output string; Backspace is recorded for undo like a deletion; the double-click timer
// is only compared against the 500 ms window, never rearmed.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/Font.h"
#include "gz/Win.h"
#include "gz/WinScrollbar.h"
#include "rz/cRZCallbackTimer.h"
#include "rz/cRZString.h"
#include "rz/cRZTimer.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinTextEdit = 0x231a1c57;
constexpr std::uint32_t kTextEditMsgKey = 0x9b1552d5, kTextEditMsgMouseDownL = 0x9b1552d6, kTextEditMsgMouseDownR = 0x9b1552d7,
                        kTextEditMsgChanged = 0x9b1552d8, kTextEditMsgKillFocus = 0x9b1552d9, kTextEditMsgSetFocus = 0x9b1552da;
constexpr std::uint32_t kTextEditMsgCaretBlink = 0x631b56cc, kTextEditMsgScrollTick = 0x631b56cd;
constexpr std::uint32_t kTextEditCmdScrollbarChanged = 0x887113a3;
constexpr std::uint32_t kTextEditDefaultFontStyle = 0x68963c4c;
enum TextEditFlag : std::uint32_t {
    kTEEditable = 0x1, kTEWrap = 0x2, kTEHScroll = 0x4, kTEVScroll = 0x8, kTEOutline = 0x10, kTEFilled = 0x20,
    kTECaretBlink = 0x40, kTEInsertKey = 0x80, kTEUndo = 0x100, kTESingleLine = 0x200, kTEReturnNotify = 0x400,
    kTEUpDownNotify = 0x800, kTEClipboard = 0x1000, kTEPassword = 0x2000, kTEOverwrite = 0x4000, kTEKeepHighlight = 0x10000,
};

// The system services the control reaches through the framework (clipboard, key state).
class IWinTextEditHost {
public:
    virtual ~IWinTextEditHost() = default;
    virtual bool ClipboardGetText(cIGZString& out) = 0;                 // clipboard manager vslot 0x88 (default clipboard)
    virtual void ClipboardSetText(const cIGZString& text) = 0;          // vslot 0x90 (replace all)
    virtual std::uint32_t ModifierState() = 0;                          // SDL_GetModState: 0xc0 = ctrl
    virtual bool CapsLockOn() = 0;                                      // GetKeyState(0x14)
    virtual bool ShiftDown() = 0;                                       // keyboard->IsKeyDown(shift)
};
IWinTextEditHost* WinTextEditHost();
void SetWinTextEditHost(IWinTextEditHost* host);

// A position inside the text: line, column, byte index and the two selection ends.
struct TextEditPosition {
    std::uint32_t line = 0, column = 0, index = 0, selectionA = 0, selectionB = 0;
};

class cIGZWinTextEdit : public cIGZUnknown {
public:
    enum ColorIndex : std::uint32_t { kColorText = 0, kColorTextBack = 1, kColor2 = 2, kColor3 = 3, kColorSelectedText = 4, kColorSelectedBack = 5 };
    virtual bool Init() = 0;                                                                   // [3]
    virtual bool Shutdown() = 0;                                                               // [4]
    virtual cIGZWin* AsIGZWin() = 0;                                                           // [5]
    virtual void SetArea(int l, int t, int r, int b) = 0;                                      // [6]
    virtual void GetTextArea(cRZRect& out) = 0;                                                // [7]
    virtual cIGZFont* GetFont() const = 0;                                                     // [8]
    virtual void SetFont(cIGZFont* font) = 0;                                                  // [9]
    virtual bool SetFontStyle(std::uint32_t styleId) = 0;                                      // [10]
    virtual bool GetFontStyle(std::uint32_t& styleId) = 0;                                     // [11]
    virtual bool SetFontColor(std::uint32_t packed) = 0;                                       // [12]
    virtual bool SetFontColor(ColorIndex index, std::uint32_t packed) = 0;                     // [13]
    virtual bool GetFontColor(ColorIndex index, std::uint32_t& native) const = 0;              // [14]
    virtual bool SetColor(ColorIndex index, std::uint32_t color) = 0;                          // [15]
    virtual bool GetColor(ColorIndex index, std::uint32_t& color) const = 0;                   // [16]
    virtual bool ClearFontColor(ColorIndex index) = 0;                                         // [17]
    virtual void SetCaretColor(std::uint32_t packed) = 0;                                      // [18]
    virtual bool GetCaretColor(std::uint32_t& native) const = 0;                               // [19]
    virtual void SetFillColor(std::uint32_t native) = 0;                                       // [20]
    virtual std::uint32_t GetHighlightFillColor() = 0;                                         // [22]
    virtual void SetHighlightFillColor(std::uint32_t native) = 0;                              // [23]
    virtual void GetBackgroundImage(cIGZBuffer*& image, bool& stretch) = 0;                    // [24]
    virtual void SetBackgroundImage(cIGZBuffer* image, bool stretch) = 0;                      // [25]
    virtual bool ScrollLineUp() = 0;                                                           // [26]
    virtual bool ScrollLineDown() = 0;                                                         // [27]
    virtual bool ScrollToStart() = 0;                                                          // [28]
    virtual bool ScrollToEnd() = 0;                                                            // [29]
    virtual bool ScrollPageUp() = 0;                                                           // [30]
    virtual bool ScrollPageDown() = 0;                                                         // [31]
    virtual std::uint32_t GetMaximumTextToAllow() = 0;                                         // [32]
    virtual void SetMaximumTextToAllow(std::uint32_t maximum) = 0;                             // [33]
    virtual std::uint32_t GetFirstVisibleLine() = 0;                                           // [34]
    virtual std::uint32_t GetLineCount() = 0;                                                  // [35]
    virtual std::uint32_t GetLineHeight() = 0;                                                 // [36]
    virtual void SetOutlineUse(bool on, std::uint32_t packed) = 0;                             // [37]
    virtual void GetOutlineUse(bool& on, std::uint32_t& native) const = 0;                     // [38]
    virtual std::uint32_t GetTextGutterX() = 0;                                                // [39]
    virtual void SetTextGutterX(std::uint32_t gutter) = 0;                                     // [40]
    virtual std::uint32_t GetTextGutterY() = 0;                                                // [41]
    virtual void SetTextGutterY(std::uint32_t gutter) = 0;                                     // [42]
    virtual bool ResizeWindowForExactLineHeights() = 0;                                        // [43]
    virtual const char* GetText() = 0;                                                         // [44]
    virtual std::uint32_t GetTextLength() = 0;                                                 // [45]
    virtual void GetText(cIGZString& out) = 0;                                                 // [46]
    virtual void GetText(char* buffer, std::uint32_t& length) = 0;                             // [47]
    virtual bool SetText(const char* text, std::uint32_t length, bool notify) = 0;             // [48]
    virtual void SetText(const cIGZString& text, bool notify) = 0;                             // [49]
    virtual std::uint32_t InsertText(const char* text, std::uint32_t length, std::uint32_t index) = 0;   // [50]
    virtual void InsertText(const char* text, std::uint32_t length, std::uint32_t line, std::uint32_t column) = 0;   // [51]
    virtual bool RemoveChar(std::uint32_t index, bool plain, cIGZString* removed) = 0;         // [52]
    virtual std::uint32_t RemoveText(std::uint32_t index, std::uint32_t length, cIGZString* removed) = 0;   // [53]
    virtual void RemoveText(std::uint32_t line, std::uint32_t column, std::uint32_t length, cIGZString* removed) = 0;   // [54]
    virtual void RemoveText(std::uint32_t line0, std::uint32_t column0, std::uint32_t line1, std::uint32_t column1, cIGZString* removed) = 0;   // [55]
    virtual std::uint32_t FindText(const char* text, std::uint32_t length, std::uint32_t start, bool forward) = 0;   // [56]
    virtual void GetIndexFromPosition(std::uint32_t& index, int x, int y) = 0;                 // [57]
    virtual void GetColumnFromPosition(std::uint32_t line, std::uint32_t& column, int x) = 0;  // [58]
    virtual void GetLineAndColumnFromPosition(std::uint32_t& line, std::uint32_t& column, int x, int y) = 0;   // [59]
    virtual void GetPositionFromLineAndColumn(std::uint32_t line, std::uint32_t column, int& x, int& y) = 0;   // [60]
    virtual void GetLineAndColumnFromIndex(std::uint32_t& line, std::uint32_t& column, std::uint32_t index) = 0;   // [61]
    virtual bool GetIndexFromLineAndColumn(std::uint32_t line, std::uint32_t column, std::uint32_t& index) = 0;   // [62]
    virtual std::uint32_t GetCharacterCountOfLine(std::uint32_t line) = 0;                     // [63]
    virtual bool SetInsertMode(bool insert) = 0;                                               // [64]
    virtual bool GetInsertMode() const = 0;                                                    // [65]
    virtual bool SetInsertionIndex(std::uint32_t index) = 0;                                   // [66]
    virtual bool GetInsertionIndex(std::uint32_t& index) const = 0;                            // [67]
    virtual bool SetInsertionLineAndColumn(std::uint32_t line, std::uint32_t column) = 0;      // [68]
    virtual bool GetInsertionLineAndColumn(std::uint32_t& line, std::uint32_t& column) const = 0;   // [69]
    virtual void IncrementInsertionPosition(int characters) = 0;                               // [70]
    virtual void IncrementInsertionPositionByWord(int words) = 0;                              // [71]
    virtual bool SetCaretPeriod(std::uint32_t ms) = 0;                                         // [72]
    virtual bool GetCaretPeriod(std::uint32_t& ms) const = 0;                                  // [73]
    virtual bool GetSelection(std::uint32_t& start, std::uint32_t& end) = 0;                   // [74]
    virtual bool SetSelection(std::uint32_t start, std::uint32_t end) = 0;                     // [75]
    virtual void SetFlag(std::uint32_t flag) = 0;                                              // [76]
    virtual void SetFlags(std::uint32_t flags) = 0;                                            // [77]
    virtual void ClearFlag(std::uint32_t flag) = 0;                                            // [78]
    virtual bool IsFlagSet(std::uint32_t flag) = 0;                                            // [79]
    virtual std::uint32_t GetFlags() = 0;                                                      // [80]
    virtual void SetMaxUndoLevelCount(std::uint32_t count) = 0;                                // [81]
    virtual std::uint32_t GetMaxUndoLevelCount() const = 0;                                    // [82]
    virtual bool Undo() = 0;                                                                   // [83]
    virtual bool Redo() = 0;                                                                   // [84]
    virtual bool GetScrollBarImageVertical(cIGZBuffer*& image, cRZRect& area) const = 0;       // [85]
    virtual bool GetScrollBarImageHorizontal(cIGZBuffer*& image, cRZRect& area) const = 0;     // [86]
    virtual bool SetScrollBarImageVertical(cIGZBuffer* image, const cRZRect& area) = 0;        // [87]
    virtual bool SetScrollBarImageHorizontal(cIGZBuffer* image, const cRZRect& area) = 0;      // [88]
    virtual bool SetSoundID(int which, int soundId) = 0;                                       // [89]
};

class cGZWinTextEdit : public cGZWin, public cIGZWinTextEdit {
public:
    explicit cGZWinTextEdit(cIGZWinMgr* mgr, int flags = 0x10b);   // @ 0x10025f330
    ~cGZWinTextEdit() override;
    static cGZWinTextEdit* CreateNewObject(cIGZWinMgr* mgr) { return new cGZWinTextEdit(mgr); }

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10025f7d2
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    // cGZWin overrides
    bool DoMessage(cGZMessage& message) override;
    bool Init() override;
    bool Shutdown() override;
    void SetArea(int l, int t, int r, int b) override;
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    std::uint32_t SetFlag(std::uint32_t flag, bool on) override;
    void ShowWindow() override;
    const cIGZString& GetCaption() const override { return text_; }
    void SetCaption(const cIGZString& caption) override { SetText(caption, false); }
    bool GZPaint() override;
    bool GZOnCharacter(char c) override;
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) override;
    bool GZOnMouseDownR(int x, int y, std::uint32_t modifiers) override;
    bool GZOnMouseUpL(int x, int y, std::uint32_t modifiers) override;
    bool GZOnMouseMove(int x, int y, std::uint32_t modifiers) override;
    bool GZOnMouseWheel(int x, int y, std::uint32_t modifiers, int delta) override;
    bool GZOnSetFocus(cIGZWin* previous) override;
    bool GZOnKillFocus(cIGZWin* next) override;
    void SetFillColor(std::uint32_t nativeColor) override;
    using cGZWin::SetFillColor;

    // cIGZWinTextEdit
    cIGZWin* AsIGZWin() override { return this; }
    void GetTextArea(cRZRect& out) override;
    cIGZFont* GetFont() const override { return font_; }
    void SetFont(cIGZFont* font) override;
    bool SetFontStyle(std::uint32_t styleId) override;
    bool GetFontStyle(std::uint32_t& styleId) override;
    bool SetFontColor(std::uint32_t packed) override;
    bool SetFontColor(ColorIndex index, std::uint32_t packed) override;
    bool GetFontColor(ColorIndex index, std::uint32_t& native) const override;
    bool SetColor(ColorIndex index, std::uint32_t color) override { colors_[index] = color; return true; }
    bool GetColor(ColorIndex index, std::uint32_t& color) const override { color = colors_[index]; return true; }
    bool ClearFontColor(ColorIndex index) override;
    void SetCaretColor(std::uint32_t packed) override { colors_[6] = ConvertNativeToPackedRGB(packed); }
    bool GetCaretColor(std::uint32_t& native) const override { native = ConvertPackedRGBToNative(colors_[6]); return true; }
    std::uint32_t GetHighlightFillColor() override { return ConvertPackedRGBToNative(colors_[7]); }
    void SetHighlightFillColor(std::uint32_t native) override { colors_[7] = ConvertPackedRGBToNative(native); }
    void GetBackgroundImage(cIGZBuffer*& image, bool& stretch) override { image = background_; stretch = stretchBackground_; }
    void SetBackgroundImage(cIGZBuffer* image, bool stretch) override;
    bool ScrollLineUp() override;
    bool ScrollLineDown() override;
    bool ScrollToStart() override;
    bool ScrollToEnd() override;
    bool ScrollPageUp() override;
    bool ScrollPageDown() override;
    bool ScrollLeft();
    bool ScrollRight();
    std::uint32_t GetMaximumTextToAllow() override { return maxText_; }
    void SetMaximumTextToAllow(std::uint32_t maximum) override { maxText_ = maximum; }
    std::uint32_t GetFirstVisibleLine() override { return firstLine_; }
    std::uint32_t GetLineCount() override { return static_cast<std::uint32_t>(lineStarts_.size()); }
    std::uint32_t GetLineHeight() override { return static_cast<std::uint32_t>(lineHeight_); }
    void SetOutlineUse(bool on, std::uint32_t packed) override;
    void GetOutlineUse(bool& on, std::uint32_t& native) const override;
    std::uint32_t GetTextGutterX() override { return gutterX_; }
    void SetTextGutterX(std::uint32_t gutter) override;
    std::uint32_t GetTextGutterY() override { return gutterY_; }
    void SetTextGutterY(std::uint32_t gutter) override;
    bool ResizeWindowForExactLineHeights() override;
    const char* GetText() override { return text_.Data(); }
    std::uint32_t GetTextLength() override { return text_.Strlen(); }
    void GetText(cIGZString& out) override { out.AssignFromChars(text_.Data(), text_.Strlen()); }
    void GetText(char* buffer, std::uint32_t& length) override;
    bool SetText(const char* text, std::uint32_t length, bool notify) override;
    void SetText(const cIGZString& text, bool notify) override { SetText(text.Data(), text.Strlen(), notify); }
    std::uint32_t InsertText(const char* text, std::uint32_t length, std::uint32_t index) override;
    void InsertText(const char* text, std::uint32_t length, std::uint32_t line, std::uint32_t column) override;
    bool RemoveChar(std::uint32_t index, bool plain, cIGZString* removed) override;
    std::uint32_t RemoveText(std::uint32_t index, std::uint32_t length, cIGZString* removed) override;
    void RemoveText(std::uint32_t line, std::uint32_t column, std::uint32_t length, cIGZString* removed) override;
    void RemoveText(std::uint32_t line0, std::uint32_t column0, std::uint32_t line1, std::uint32_t column1, cIGZString* removed) override;
    std::uint32_t FindText(const char* text, std::uint32_t length, std::uint32_t start, bool forward) override;
    void GetIndexFromPosition(std::uint32_t& index, int x, int y) override;
    void GetColumnFromPosition(std::uint32_t line, std::uint32_t& column, int x) override;
    void GetLineAndColumnFromPosition(std::uint32_t& line, std::uint32_t& column, int x, int y) override;
    void GetPositionFromLineAndColumn(std::uint32_t line, std::uint32_t column, int& x, int& y) override;
    void GetLineAndColumnFromIndex(std::uint32_t& line, std::uint32_t& column, std::uint32_t index) override;
    bool GetIndexFromLineAndColumn(std::uint32_t line, std::uint32_t column, std::uint32_t& index) override;
    std::uint32_t GetCharacterCountOfLine(std::uint32_t line) override;
    bool SetInsertMode(bool insert) override;
    bool GetInsertMode() const override { return (flags_ & kTEOverwrite) == 0; }
    bool SetInsertionIndex(std::uint32_t index) override;
    bool GetInsertionIndex(std::uint32_t& index) const override { index = pos_.index; return true; }
    bool SetInsertionLineAndColumn(std::uint32_t line, std::uint32_t column) override;
    bool GetInsertionLineAndColumn(std::uint32_t& line, std::uint32_t& column) const override { line = pos_.line; column = pos_.column; return true; }
    void IncrementInsertionPosition(int characters) override;
    void IncrementInsertionPositionByComposedCharacter(int characters);
    void IncrementInsertionPositionByWord(int words) override;
    bool SetCaretPeriod(std::uint32_t ms) override;
    bool GetCaretPeriod(std::uint32_t& ms) const override { ms = caretPeriod_; return true; }
    bool GetSelection(std::uint32_t& start, std::uint32_t& end) override { start = pos_.selectionA; end = pos_.selectionB; return true; }
    bool SetSelection(std::uint32_t start, std::uint32_t end) override;
    void SetFlag(std::uint32_t flag) override { if (!(flags_ & flag)) SetFlags(flags_ | flag); }
    void SetFlags(std::uint32_t flags) override;
    void ClearFlag(std::uint32_t flag) override { if (flags_ & flag) SetFlags(flags_ & ~flag); }
    bool IsFlagSet(std::uint32_t flag) override { return (flags_ & flag) != 0; }
    std::uint32_t GetFlags() override { return flags_; }
    void SetMaxUndoLevelCount(std::uint32_t count) override { maxUndo_ = count; }
    std::uint32_t GetMaxUndoLevelCount() const override { return maxUndo_; }
    bool Undo() override;
    bool Redo() override;
    bool GetScrollBarImageVertical(cIGZBuffer*& image, cRZRect& area) const override;
    bool GetScrollBarImageHorizontal(cIGZBuffer*& image, cRZRect& area) const override;
    bool SetScrollBarImageVertical(cIGZBuffer* image, const cRZRect& area) override;
    bool SetScrollBarImageHorizontal(cIGZBuffer* image, const cRZRect& area) override;
    bool SetSoundID(int which, int soundId) override;
    bool PlaySoundA(int which);

    // internals (public for tests)
    void InitializeMemberVariables(int flags);
    int CalculateVisibleWidth();
    int CalculateVisibleHeight();
    void RecalculateAll();
    void CalculateAllLineLengths(std::uint32_t fromLine);
    void AddFirstLineLength();
    void AdjustLinePositionArray(std::uint32_t length, std::uint32_t lineNumber);
    std::uint32_t LineLength(std::uint32_t line);   // vslot 0x690 GetCharacterCountOfLine
    void FixInsertionPositionIfBroken();
    void MoveInsertionPositionToBeforeReturnIfNecessary();
    void MakeCaretVisible();
    void PutInsertionPositionInView();
    void SetScrollBarSizes();
    void SetScrollBarValues(bool resetRange);
    bool TestForPossibleScrollBarAdditionOrRemoval();
    bool AddScrollBar(bool vertical, bool horizontal);
    void RemoveScrollBar(bool vertical, bool horizontal);
    void UpdateScrollBarsAndWindowPosition();
    int GetPixelWidthOfLongestLine(bool all);
    int GetHorizontalScrollPixels();
    void GetHorizontalLineMetrics(std::uint32_t line, std::uint32_t scrollPixels, std::uint32_t& start, std::uint32_t& end, int& offset);
    bool SelectCurrentWord();
    bool InsertTextHighLevel(const cRZString& text);
    bool RemoveBadCharacters(cRZString& text);
    std::uint32_t RemoveBadCharacters(char* text, std::uint32_t& length);
    int FixBadCharacterComposition(cRZString& text, const cRZString& existing, std::uint32_t at);
    void DoCursorScroll();
    static void CaretTimerCallback(void* self);
    static void ScrollTimerCallback(void* self);
    // Writes the caret-visible byte directly (the IME composition window's Init sets it before base Init).
    void SetCaretVisible(bool visible) { caretVisible_ = visible; }

    const TextEditPosition& Position() const { return pos_; }
    const std::vector<std::uint32_t>& LineStarts() const { return lineStarts_; }
    bool HasVerticalScrollBar() const { return vScroll_ != nullptr; }
    bool HasHorizontalScrollBar() const { return hScroll_ != nullptr; }
    std::size_t UndoDepth() const { return undo_.size(); }
    std::size_t RedoDepth() const { return redo_.size(); }
    int VisibleLines() const { return visibleLines_; }

private:
    // One undo / redo record: an insertion and / or a deletion, plus the caret state to restore.
    struct UndoRedoAction {
        bool inserted = false;            // +0
        std::uint32_t insertAt = 0;       // +4
        std::uint32_t insertLength = 0;   // +8
        bool deleted = false;             // +0xc
        std::uint32_t deleteAt = 0;       // +0x10
        std::string deletedText;          // +0x18
        TextEditPosition position;        // +0x40
    };
    bool AddUndoRecordForTextInsertion(const TextEditPosition& before, std::uint32_t at, std::uint32_t length,
                                       const char* replaced, std::uint32_t replacedLength);
    bool AddUndoRecordForTextDeletion(const TextEditPosition& before, std::uint32_t at, const char* text, std::uint32_t length);
    void ClearRedo() { redo_.clear(); }
    bool ApplyAction(std::vector<UndoRedoAction>& from, std::vector<UndoRedoAction>& to);
    void Notify(std::uint32_t type, std::uint64_t p2 = 0, std::uint64_t p3 = 0);
    void Repaint() { InvalidateSelfAndParents(); }
    bool Multibyte() const { return codePage_ == 8 || codePage_ > 99; }
    const char* Chars() const { return text_.Data(); }
    std::uint32_t Size() const { return text_.Strlen(); }

    cRZString text_;                       // +0x158 the text
    cRZString display_;                    // +0x180 the mask shown for password fields
    char maskChar_ = '*';                  // +0x1a8
    std::vector<std::uint32_t> lineStarts_;   // +0x1b0
    TextEditPosition pos_;                 // +0x1c8..+0x1d8
    std::uint32_t flags_ = 0;              // +0x1dc
    int codePage_ = 8;                     // +0x1e0
    std::string pendingChar_;              // +0x1f0 bytes of a multibyte character being typed
    std::uint32_t gutterX_ = 5, gutterY_ = 0;   // +0x238 / +0x23c
    int visibleWidth_ = 0, visibleHeight_ = 0, visibleLines_ = 0;   // +0x240 / +0x244 / +0x248
    std::uint32_t firstLine_ = 0;          // +0x24c
    std::int32_t wantedPixel_ = -1;        // +0x250 column to keep while moving up / down
    std::int32_t hScrollChars_ = 0;        // +0x254 first visible byte (single-line mode)
    std::int32_t hScrollPixels_ = 0;       // +0x258
    cIGZBuffer* background_ = nullptr;     // +0x260
    bool stretchBackground_ = true;        // +0x268
    std::uint32_t maxUndo_ = 0x4000;       // +0x26c
    std::vector<UndoRedoAction> undo_;     // +0x270
    std::vector<UndoRedoAction> redo_;     // +0x288
    cIGZFont* font_ = nullptr;             // +0x2a0
    std::uint32_t fontStyle_ = 0;          // +0x2a8
    std::uint32_t colors_[9] = {};         // +0x2ac text, back, 2, 3, selected text, selected back, caret, highlight, outline
    int lineHeight_ = 1;                   // +0x2d0
    std::uint32_t maxText_ = 0xffffffffu;  // +0x2d4
    cIGZWinScrollbar* vScroll_ = nullptr;  // +0x2d8
    cIGZWinScrollbar* hScroll_ = nullptr;  // +0x2e0
    cIGZBuffer* vImage_ = nullptr;         // +0x2e8
    cIGZBuffer* hImage_ = nullptr;         // +0x2f0
    cRZRect vImageRect_{}, hImageRect_{};  // +0x2f8 / +0x308
    cRZTimer clickTimer_{cRZTimer::kMilliseconds};   // +0x318
    std::uint32_t doubleClickMs_ = 500;    // +0x330
    std::uint32_t caretPeriod_ = 1000;     // +0x334
    bool caretVisible_ = false;            // +0x338
    cRZCallbackTimer caretTimer_;          // +0x340
    cRZCallbackTimer scrollTimer_;         // +0x378
    int soundIds_[4] = {0, 0, 0, 0};       // +0x3b0
};

}  // namespace gz
