// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the main window: a header with a menu button, two tabs (Converter, Calculator) and a
// keep-on-top toggle, above the Windows Calculator style display and keypad. Everything is
// painted by hand into one memory DIB (gfx.h); the layout is the one of the WPF build
// (legacy\wpf\src\MainWindow.cs), ported with WPF's layout rounding.

#pragma once

#include "flyout.h"
#include "gfx.h"
#include "theme.h"
#include "../core/hc_core.h"

#include <string>
#include <vector>

namespace app {

using gfx::Argb;
using gfx::RectF;

enum KeyStyle { KeyDigit, KeyFunc, KeyAccent };

// The commands of the popup menus (the ids of their items). The decimal places items use their own value (2..6) as the id; the
// theme items are kCmdThemeAuto + the number of hc::settings::ThemeMode (Auto, Light, Dark).
enum { kCmdAbout = 100, kCmdThemeAuto = 110, kCmdThemeLight = 111, kCmdThemeDark = 112,
       kCmdCopy = 200, kCmdCut = 201, kCmdFieldCopy = 202, kCmdPaste = 203 };

struct KeyDef {
    std::wstring key;         // what Press() gets: "7", "CE", "BACK", "RESET", "CONVERT", "+", ...
    std::wstring label;       // text keys
    wchar_t glyph;            // glyph keys (0 for text keys)
    KeyStyle style;
    gfx::Family fam;
    int weight;
    float size;               // DIP at UI scale 1
    bool opGlyph;             // arithmetic operator glyph: drawn at 80 % opacity
    int row, col, span;
    const wchar_t* name;      // accessible name
};

// One editable converter value: text, caret and the other end of the selection.
struct Field {
    std::wstring text;
    int caret = 0;
    int anchor = 0;
    int selStart() const { return caret < anchor ? caret : anchor; }
    int selEnd() const { return caret < anchor ? anchor : caret; }
    int selLength() const { return selEnd() - selStart(); }
};

// What the mouse is over.
enum HitKind { HitNone, HitMenu, HitPin, HitTabConv, HitTabCalc, HitField, HitCalcValue, HitKey };
struct Hit {
    HitKind kind = HitNone;
    int index = 0;            // field (0/1), calc value (0/1) or key index
    bool operator==(const Hit& o) const { return kind == o.kind && index == o.index; }
    bool operator!=(const Hit& o) const { return !(*this == o); }
};

// The geometry of one layout pass, in device pixels. Everything the painter and the hit test need.
struct Layout {
    float d = 1.0f;           // device scale: dpi / 96
    int W = 0, H = 0;         // client size, px
    float s = 1.0f;           // UI scale (0.85 .. 2.0)
    float headerH = 0;
    RectF menuBtn{}, pin{}, tab[2]{}, tabInd[2]{};
    float tabTextX[2] = { 0, 0 }, tabTextY = 0, tabTextW[2] = { 0, 0 };
    float menuGlyphX = 0, glyphY = 0, pinGlyphX = 0;
    // converter
    RectF lbl[2]{}, box[2]{}, under[2]{}, cap{};
    float boxTextTop = 0;     // line box top of a value (vertically centred in the box) at the largest font
    // calculator
    RectF expr{}, calcLbl[2]{}, calcVal[2]{}, calcCap{}, calcDisplay{};
    // keypad
    std::vector<RectF> keys;
    RectF keypad{};
    float valueRowH = 0, captionH = 0;
};

// The box of a key's text or glyph inside the key rectangle r (device pixels, scales d and s).
RectF KeyTextBox(const KeyDef& k, const RectF& r, float d, float s);

// The star distribution of the legacy WPF Grid (see app_layout.cpp): the order equal star definitions are
// resolved in, and the sizes n equal stars get out of 'avail' pixels.
void TieOrder(int n, std::vector<int>& order);
void StarSizes(double avail, int n, std::vector<float>& sizes);

class MainWindow {
public:
    explicit MainWindow(const wchar_t* settingsPath);   // null: the settings (decimal places, theme) are neither read nor written
    ~MainWindow();

    // ---- creating and showing
    bool Create(HINSTANCE hInst, bool hidden);
    HWND hwnd() const { return hwnd_; }
    static const wchar_t* ClassName();

    // ---- the test / automation surface (the same as the WPF build's)
    hc::CalcEngine& Engine() { return engine_; }
    std::wstring GetConverterText(int field) const { return fields_[field].text; }
    const std::wstring& ConverterCaption() const { return caption_; }
    int CurrentTab() const { return tab_; }
    int ActiveField() const { return active_; }
    int DecimalPlaces() const { return decimals_; }
    void SetDecimalPlaces(int places);
    // The theme choice (Auto follows the Windows app theme, Light and Dark force one). Setting it applies at once to everything
    // that is open and is saved in the settings file when it changes.
    hc::settings::ThemeMode ThemeChoice() const { return themeMode_; }
    void SetThemeMode(hc::settings::ThemeMode mode);
    void Press(const std::wstring& key);
    void SelectTab(int index);
    // Applies a palette without changing the theme choice (the snapshot tool and the tests use it; a later change of the Windows
    // theme goes back to what the choice says).
    void ApplyTheme(bool dark);
    void SetConverterText(int field, const std::wstring& text);
    // Lays the content out at a client size (px) and device scale without showing anything.
    void LayoutAt(int clientW, int clientH, float dpiScale = 1.0f);
    // Paints the current state into a canvas of the client size.
    void Render(gfx::Canvas& c);
    const Layout& GetLayout() const { return lay_; }
    const theme::Palette& Palette() const { return pal_; }
    Argb Accent() const { return accent_; }
    bool IsDark() const { return dark_; }
    const std::wstring& SettingsPath() const { return settingsPath_; }

    // Converter field state (0 = left, 1 = right).
    Field& FieldRef(int f) { return fields_[f]; }
    // The window got / lost the keyboard focus (tests drive this directly).
    void SetWindowActive(bool active);
    // Which converter field has the keyboard focus (-1: none).
    int FocusField() const { return focus_; }
    // Simulates the keyboard focus arriving at a field: from another field (Tab or click: selects all) or from outside.
    void GotFocus(int field, bool fromField);
    // A text typed into the focused field (what WM_CHAR does).
    void TypeText(int field, const std::wstring& s);
    // Characters and keys as the window procedure routes them (tests call these with synthetic input).
    void OnChar(wchar_t c);
    bool OnKeyDown(UINT vk, bool ctrl, bool shift);
    void MouseDownOnField(int field);

    // Calculator state mirrored from the engine.
    const std::wstring& CalcMain() const { return calcL_; }
    const std::wstring& CalcDecimal() const { return calcR_; }
    const std::wstring& CalcCaption() const { return calcCap_; }
    const std::wstring& ExpressionShown() const { return exprShown_; }
    bool KeyEnabled(const std::wstring& key) const;
    float ConverterFontPx() const { return convFont_; }
    float CalcFontPx() const { return calcFont_; }
    float CalcFontMax() const { return calcFontMax_; }
    bool CalcRightVisible() const { return !error_; }
    bool Topmost() const { return topmost_; }
    int KeyIndex(const std::wstring& key) const;       // the key of the current tab with this command, or -1
    const KeyDef& KeyAt(int i) const { return (tab_ == 0 ? convKeys_ : calcKeys_)[(size_t)i]; }
    bool UnderlineVisible(int field) const { return tab_ == 0 && active_ == field; }

    // Popups, for the snapshot tool: builds them for the current theme and size.
    flyout::Flyout& MenuFlyout();
    flyout::Flyout& CopyFlyout();
    flyout::Flyout& TipFlyout();
    flyout::Flyout& FieldMenuFlyout();
    void OpenMenu();
    // The screen position of the top-left corner of the menu box when it opens: below the button (4 px in, 2 px down), shifted so
    // that the box lies inside the work area of the monitor it is on ('work': a work area to use instead, for tests).
    POINT MenuPosition(const RECT* work = nullptr);
    // Opens the Cut / Copy / Paste menu of a converter field at a screen position.
    void OpenFieldMenu(int field, int screenX, int screenY);
    // What OpenFieldMenu does before it shows the menu: the field it works on, which items are enabled, the size.
    void PrepareFieldMenu(int field);
    void CloseMenus();
    bool IsMenuOpen() const { return menu_ && menu_->IsOpen(); }
    bool IsFieldMenuOpen() const { return fieldMenu_ && fieldMenu_->IsOpen(); }
    // True for a moment after the Advanced menu closed: the click that closed it must not open it again.
    bool MenuReopenBlocked() const;
    // The caret blinks only while a field has the keyboard focus in an active window: the timer is off otherwise.
    bool CaretTimerRunning() const { return caretTimerOn_; }
    void ShowAbout();
    HINSTANCE Instance() const { return hInst_; }
    void SetMain() { isMain_ = true; }
    float Scale() const { return lay_.d; }
    void SetTopmost(bool on);
    void OnFlyoutCommand(int id, int kind);
    // The text colour a key is drawn with before the operator glyphs' 80 % opacity (for tests).
    Argb KeyForeground(int index, bool pressed = false) const;
#ifdef HC_TESTHOOKS
    // Test build only. How often the window was invalidated (an unchanged Windows theme must cost no repaint), how often it was
    // painted at once (RepaintNow), and the value of that count at the moment the settings file was last written: a change must
    // be on the screen before the file write, which can be slow.
    int InvalidateCount() const { return invalidateCount_; }
    int RepaintNowCount() const { return repaintNowCount_; }
    int RepaintNowCountAtLastSave() const { return repaintNowAtSave_; }
    int SavesCount() const { return saves_; }
#endif

private:
    // layout and fitting (app_layout.cpp)
    void BuildKeys();
    void Relayout();
    void FitAll();
    void FitExpression();
    float FitSize(int which, float maxPx, float minPx, float padPx, float availW, const std::wstring& text, gfx::Family fam);
    float MeasureValue(const std::wstring& s, float px) const;
    void RefreshCalc();

    // painting (app_paint.cpp)
    void PaintHeader(gfx::Canvas& c);
    void PaintConverter(gfx::Canvas& c);
    void PaintCalculator(gfx::Canvas& c);
    void PaintKeys(gfx::Canvas& c);
    void PaintField(gfx::Canvas& c, int f);
    Argb KeyFill(const KeyDef& k, bool enabled, bool hot, bool pressed) const;
    void DrawKey(gfx::Canvas& c, const KeyDef& k, const RectF& r, bool enabled, bool hot, bool pressed);

    // logic (app_logic.cpp)
    void SetPalette(bool dark, Argb accent);
    // The settings file: a changed setting is written with the other one as it is in the file (hc::settings::SavePlaces / SaveTheme).
    void SavePlaces();
    void SaveTheme();
    // Paints what was invalidated now, not when the message loop gets to it: the write of a settings file that follows can take a
    // while (a slow or busy disk) and the change must not wait for it.
    void RepaintNow();
    void SetActive(int field);
    void FocusActive();
    void ApplyFieldEdit(int field, const std::wstring& raw, int caret);
    void RecomputeConverter();
    void InsertText(const std::wstring& s);
    void PressConverter(const std::wstring& key);
    void PressCalc(const std::wstring& key);
    void SetFieldText(int field, const std::wstring& text);     // programmatic Text set: caret 0, TextChanged
    void SetFieldRaw(int field, const std::wstring& text);      // under _busy: no TextChanged
    void PasteIntoCalc();
    bool SetClipboard(const std::wstring& text);
    bool GetClipboard(std::wstring& text);
    void CopyField(int field);
    void CutField(int field);
    void PasteField(int field);
    void FieldKey(int field, UINT vk, bool shift, bool ctrl, bool& handled);
    void Invalidate();
    void RestartCaret();
    void UpdateCaretTimer(bool restart);

    // window procedure (app_window.cpp)
    static LRESULT CALLBACK StaticProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Proc(UINT msg, WPARAM wp, LPARAM lp);
    Hit HitTest(int x, int y) const;
    void OnMouseMove(int x, int y);
    void OnLButtonDown(int x, int y, bool dbl);
    void OnLButtonUp(int x, int y);
    void OnRButtonUp(int x, int y);
    void UpdateHover(int x, int y);
    void ClickHit(const Hit& h);
    void ShowTip(const Hit& h);
    void HideTip();
    void ToggleMenu();
    void OpenCopyMenu(int which, int x, int y);
    void BuildMenu();
    void ApplyDpi(float dpiScale);
    void OnSettingChange();
    void UpdateIcons();
    int FieldCaretFromX(int field, float x) const;
    float FieldTextRight(int field) const;
    float FieldTextWidth(int field) const;

    // ---- state
    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    std::wstring settingsPath_;
    hc::CalcEngine engine_;
    theme::Palette pal_;
    Argb accent_ = 0;
    bool dark_ = false;
    hc::settings::ThemeMode themeMode_ = hc::settings::ThemeMode::Auto;
    int tab_ = 0;
    int decimals_ = 2;
    bool topmost_ = false;
    bool windowActive_ = false;
    bool isMain_ = false;
#ifdef HC_TESTHOOKS
    int invalidateCount_ = 0, repaintNowCount_ = 0, repaintNowAtSave_ = -1, saves_ = 0;
#endif

    // converter
    Field fields_[2];
    std::wstring caption_;
    int active_ = 0;        // the field the keypad types into
    int source_ = 0;        // the field that was edited last: the other one is computed from it
    bool busy_ = false;     // a programmatic text change: ignore the change notification
    int focus_ = -1;        // the field that has the keyboard focus within the window (-1: none)
    bool caretOn_ = true;
    int dragField_ = -1;    // a selection drag in progress
    bool caretTimerOn_ = false;
    UINT caretTimerMs_ = 0;
    bool tripleArmed_ = false;    // the last click was a double click: one more click in time and place selects all again
    DWORD tripleTick_ = 0;
    POINT triplePt_ = { 0, 0 };

    // calculator
    std::wstring calcL_, calcR_, calcCap_, exprFull_, exprShown_;
    bool error_ = false;
    float calcFontMax_ = 40.0f;
    std::vector<bool> calcEnabled_;     // per calcKeys_ index

    // layout
    Layout lay_;
    std::vector<KeyDef> convKeys_, calcKeys_;
    float convFont_ = 0, calcFont_ = 0;      // the shrunk font sizes of the value pairs, px
    int clientW_ = 324, clientH_ = 521;
    float dpiScale_ = 1.0f;
    float scaleApplied_ = 1.0f;      // the UI scale the layout used last (WPF: _scale)
    bool layoutValid_ = false;

    // mouse
    Hit hover_, pressed_;
    bool tracking_ = false;
    int copyWhich_ = 0;
    int ctxField_ = 0;            // the converter field the Cut / Copy / Paste menu was opened for
    DWORD menuClosedTick_ = 0;    // when the Advanced menu was last closed (a click on its button then must not reopen it)
    bool firstErase_ = true;
    int tipTimer_ = 0;
    Hit tipHit_;
    bool tipShown_ = false;

    // popups
    flyout::Flyout* menu_ = nullptr;
    flyout::Flyout* copy_ = nullptr;
    flyout::Flyout* fieldMenu_ = nullptr;
    flyout::Flyout* tip_ = nullptr;
    gfx::Canvas canvas_;
    HICON iconBig_ = nullptr, iconSmall_ = nullptr;
};

}  // namespace app
