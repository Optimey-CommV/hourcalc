// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the behaviour: converter fields (typing, filtering, focus and select-all rules), the
// keypad, the calculator, keyboard routing. A port of the logic in legacy\wpf\src\MainWindow.cs.

#include "app.h"
#include "clipboard.h"
#include "dialogs.h"
#include "ui.h"

#include <algorithm>

namespace app {

namespace conv = hc::converter;

static std::wstring Sep() { return hc::timemath::DecimalSeparator(); }

static std::wstring ExpressionSymbols(std::wstring e) {
    auto rep = [&](const wchar_t* from, const wchar_t* to) {
        size_t pos = 0, fl = wcslen(from), tl = wcslen(to);
        while ((pos = e.find(from, pos)) != std::wstring::npos) {
            e.replace(pos, fl, to);
            pos += tl;
        }
    };
    rep(L" * ", L" \x00D7 ");
    rep(L" / ", L" \x00F7 ");
    rep(L" - ", L" \x2212 ");
    return e;
}

void MainWindow::Invalidate() {
#ifdef HC_TESTHOOKS
    ++invalidateCount_;
#endif
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::RepaintNow() {
#ifdef HC_TESTHOOKS
    ++repaintNowCount_;
#endif
    if (hwnd_) UpdateWindow(hwnd_);
}

// ------------------------------------------------------------------ theme

void MainWindow::SetPalette(bool dark, Argb accent) {
    dark_ = dark;
    accent_ = accent;
    pal_ = theme::Create(dark, accent);
    theme::ApplyCaption(hwnd_, pal_);
    if (menu_) menu_->SetPalette(pal_);
    if (copy_) copy_->SetPalette(pal_);
    if (fieldMenu_) fieldMenu_->SetPalette(pal_);
    if (tip_) tip_->SetPalette(pal_);
    dialogs::Dialog::SetPaletteOfAll(pal_);       // the About and license windows, when they are open
    Invalidate();
}

void MainWindow::ApplyTheme(bool dark) {
    SetPalette(dark, theme::SystemAccent(dark));
}

// The settings file holds all settings. A changed one is written together with the other one as it is in the file now (another
// HourCalc may have changed that in the meantime; the file is read again for it), and only on a change, by the callers. A write
// that fails (a folder that is not writable, a locked file) leaves the setting in force for this session.
void MainWindow::SavePlaces() {
#ifdef HC_TESTHOOKS
    repaintNowAtSave_ = repaintNowCount_;
    ++saves_;
#endif
    if (!settingsPath_.empty()) hc::settings::SavePlaces(settingsPath_, decimals_, themeMode_);
}

void MainWindow::SaveTheme() {
#ifdef HC_TESTHOOKS
    repaintNowAtSave_ = repaintNowCount_;
    ++saves_;
#endif
    if (!settingsPath_.empty()) hc::settings::SaveTheme(settingsPath_, themeMode_, decimals_);
}

void MainWindow::SetThemeMode(hc::settings::ThemeMode mode) {
    if (menu_) menu_->SetChecked(kCmdThemeAuto + (int)mode);
    if (mode == themeMode_) return;
    themeMode_ = mode;
    bool dark = theme::IsDarkFor(mode);
    SetPalette(dark, theme::SystemAccent(dark));
    RepaintNow();                 // on the screen before the file write, which can take a while
    SaveTheme();
}

// ------------------------------------------------------------------ converter: fields

void MainWindow::SetActive(int field) {
    active_ = field;
    Invalidate();
}

// The caret timer runs only while there is a caret to blink: a field has the keyboard focus in an active window that is
// not minimized. A timer that wakes the process twice a second for nothing keeps a laptop out of its deepest idle
// states, and this window is meant to stay open all day. 'restart' starts the blink period anew (the caret is solid
// for a full period after a key press or a click).
void MainWindow::UpdateCaretTimer(bool restart) {
    if (!hwnd_) return;
    UINT t = GetCaretBlinkTime();
    bool want = windowActive_ && focus_ >= 0 && !IsIconic(hwnd_) && t != 0 && t != INFINITE;
    if (want) {
        if (restart || !caretTimerOn_ || caretTimerMs_ != t) {
            SetTimer(hwnd_, 1, t, nullptr);
            caretTimerOn_ = true;
            caretTimerMs_ = t;
        }
    } else if (caretTimerOn_) {
        KillTimer(hwnd_, 1);
        caretTimerOn_ = false;
    }
}

void MainWindow::RestartCaret() {
    caretOn_ = true;
    UpdateCaretTimer(true);
    Invalidate();
}

void MainWindow::SetWindowActive(bool active) {
    windowActive_ = active;
    RestartCaret();
}

// Moves keyboard focus to the active input without selecting its text.
void MainWindow::FocusActive() {
    if (tab_ == 0) {
        bool within = windowActive_ && focus_ == active_;
        if (!within) {
            focus_ = active_;
            Field& f = fields_[active_];
            f.caret = f.anchor = (int)f.text.size();
            RestartCaret();
        }
    } else {
        focus_ = -1;
    }
}

void MainWindow::GotFocus(int field, bool fromField) {
    focus_ = field;
    SetActive(field);
    if (fromField) {
        Field& f = fields_[field];
        f.anchor = 0;
        f.caret = (int)f.text.size();
    }
    RestartCaret();
}

// A programmatic text change of a field (the Text property of the WPF TextBox): the caret goes to the start.
void MainWindow::SetFieldRaw(int field, const std::wstring& text) {
    Field& f = fields_[field];
    if (f.text == text) return;
    f.text = text;
    f.caret = f.anchor = 0;
}

void MainWindow::SetFieldText(int field, const std::wstring& text) {
    if (fields_[field].text == text) return;
    SetFieldRaw(field, text);
    if (!busy_) ApplyFieldEdit(field, text, 0);
}

// The single place where typed field text is sanitized and the other field recomputed.
void MainWindow::ApplyFieldEdit(int field, const std::wstring& raw, int caret) {
    if (busy_) return;
    busy_ = true;
    Field& tb = fields_[field];
    std::wstring filtered = conv::FilterField(field, raw);
    if (tb.text != filtered) {
        int safe = std::max(0, std::min(caret, (int)raw.size()));
        int newCaret = (int)conv::FilterField(field, raw.substr(0, (size_t)safe)).size();
        SetFieldRaw(field, filtered);
        tb.caret = tb.anchor = std::min(newCaret, (int)filtered.size());
    }
    std::wstring normal, other, cap;
    conv::Convert(field, filtered, decimals_, normal, other, cap);
    SetFieldRaw(1 - field, other);
    caption_ = cap;
    source_ = field;
    busy_ = false;
    FitAll();
    RestartCaret();
}

// Recomputes the computed field and the caption from the field that was edited last (after the
// decimal places changed). The text of the edited field is left alone.
void MainWindow::RecomputeConverter() {
    std::wstring normal, other, cap;
    conv::Convert(source_, fields_[source_].text, decimals_, normal, other, cap);
    busy_ = true;
    SetFieldRaw(1 - source_, other);
    caption_ = cap;
    busy_ = false;
    FitAll();
    Invalidate();
}

// Replaces the selection of a field (or inserts at the caret) and runs the usual edit.
void MainWindow::TypeText(int field, const std::wstring& s) {
    Field& tb = fields_[field];
    std::wstring text = tb.text;
    int start = std::min(tb.selStart(), (int)text.size());
    int length = std::min(tb.selLength(), (int)text.size() - start);
    std::wstring next = text;
    next.erase((size_t)start, (size_t)length);
    next.insert((size_t)start, s);
    int caret = start + (int)s.size();
    busy_ = true;
    SetFieldRaw(field, next);
    tb.text = next;
    tb.caret = tb.anchor = caret;
    busy_ = false;
    ApplyFieldEdit(field, next, caret);
}

void MainWindow::InsertText(const std::wstring& s) {
    FocusActive();
    TypeText(active_, s);
}

void MainWindow::SetConverterText(int field, const std::wstring& text) {
    SetActive(field);
    SetFieldText(field, text);
    // Text was set as if typed: the edit is applied with the caret at the end.
    // (SetFieldText already applied it when the text changed; apply once more with the real caret when it did not.)
    Field& f = fields_[field];
    if (f.text == text) ApplyFieldEdit(field, f.text, (int)f.text.size());
}

void MainWindow::PressConverter(const std::wstring& key) {
    if (key == L"RESET") {
        busy_ = true;
        SetFieldRaw(0, L"");
        SetFieldRaw(1, L"");
        caption_.clear();
        busy_ = false;
        source_ = conv::Left;
        SetActive(conv::Left);
        FocusActive();
        FitAll();
        Invalidate();
        return;
    }
    if (key == L"CONVERT") {
        // Tidies the active field's text, and recomputes the other field from what is in the field
        // (not from the tidied text: 8,058 reads 8,06 afterwards, but the other field keeps the 08:03 it had).
        int f = active_;
        std::wstring normal, other, cap;
        conv::Convert(f, fields_[f].text, decimals_, normal, other, cap);
        busy_ = true;
        SetFieldRaw(f, normal);
        fields_[f].caret = fields_[f].anchor = (int)normal.size();
        SetFieldRaw(1 - f, other);
        caption_ = cap;
        busy_ = false;
        source_ = f;
        FitAll();
        Invalidate();
        return;
    }
    if (key == L"CE") {
        FocusActive();
        SetFieldText(active_, L"");
        return;
    }
    if (key == L"BACK") {
        int f = active_;
        FocusActive();
        Field& tb = fields_[f];
        if (tb.selLength() > 0) {
            std::wstring next = tb.text;
            int start = tb.selStart();
            next.erase((size_t)start, (size_t)tb.selLength());
            tb.text = next;
            tb.caret = tb.anchor = start;
            ApplyFieldEdit(f, next, start);
        } else if (tb.caret > 0) {
            int i = tb.caret;
            std::wstring next = tb.text;
            next.erase((size_t)(i - 1), 1);
            SetFieldText(f, next);
            Field& t2 = fields_[f];
            t2.caret = t2.anchor = std::min(i - 1, (int)t2.text.size());
        }
        Invalidate();
        return;
    }
    if (key.size() != 1) return;
    wchar_t c = key[0];
    if (hc::timemath::IsDigit(c)) InsertText(key);
    else if (c == L':' && active_ == conv::Left) InsertText(L":");
    else if ((c == L',' || c == L'.') && active_ == conv::Right) InsertText(Sep());
}

// ------------------------------------------------------------------ calculator

void MainWindow::PressCalc(const std::wstring& key) {
    engine_.Press(key);
    RefreshCalc();
}

void MainWindow::RefreshCalc() {
    error_ = engine_.IsError();
    exprFull_ = ExpressionSymbols(engine_.Expression());
    calcL_ = engine_.MainText();
    calcR_ = engine_.DecimalText();
    calcCap_ = engine_.Caption();
    // The error text may use the width of both columns, in a smaller font.
    calcFontMax_ = error_ ? 26.0f : 40.0f;
    for (size_t i = 0; i < calcKeys_.size(); i++) calcEnabled_[i] = engine_.IsEnabled(calcKeys_[i].key);
    Relayout();
    Invalidate();
}

bool MainWindow::GetClipboard(std::wstring& text) {
    return clip::GetText(hwnd_, text);
}

bool MainWindow::SetClipboard(const std::wstring& text) {
    return clip::SetText(hwnd_, text);
}

// ------------------------------------------------------------------ cut, copy and paste of a field

void MainWindow::CopyField(int field) {
    const Field& f = fields_[field];
    if (f.selLength() > 0) SetClipboard(f.text.substr((size_t)f.selStart(), (size_t)f.selLength()));
}

// The selection leaves the field only when it is on the clipboard.
void MainWindow::CutField(int field) {
    Field& f = fields_[field];
    if (f.selLength() <= 0) return;
    if (!SetClipboard(f.text.substr((size_t)f.selStart(), (size_t)f.selLength()))) return;
    std::wstring next = f.text;
    int s = f.selStart();
    next.erase((size_t)s, (size_t)f.selLength());
    f.text = next;
    f.caret = f.anchor = s;
    ApplyFieldEdit(field, next, s);
}

void MainWindow::PasteField(int field) {
    std::wstring t;
    if (GetClipboard(t)) TypeText(field, t);
}

void MainWindow::PasteIntoCalc() {
    std::wstring text;
    if (!GetClipboard(text)) return;
    if (engine_.Paste(text)) RefreshCalc();
}

// ------------------------------------------------------------------ the keypad and the tabs

void MainWindow::Press(const std::wstring& key) {
    if (tab_ == 0) PressConverter(key);
    else PressCalc(key);
}

void MainWindow::SelectTab(int index) {
    tab_ = index == 1 ? 1 : 0;
    FocusActive();
    UpdateCaretTimer(false);
    Relayout();
    Invalidate();
}

void MainWindow::SetDecimalPlaces(int places) {
    int p = hc::timemath::ClampDecimals(places);
    if (menu_) {
        flyout::Flyout& m = *menu_;
        m.SetChecked(p);
    }
    if (p == decimals_) return;
    decimals_ = p;
    engine_.SetDecimalPlaces(p);
    RecomputeConverter();
    RefreshCalc();
    RepaintNow();                 // on the screen before the file write, which can take a while
    SavePlaces();
}

void MainWindow::SetTopmost(bool on) {
    topmost_ = on;
    if (hwnd_) SetWindowPos(hwnd_, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Invalidate();
}

// ------------------------------------------------------------------ keyboard

static std::wstring CalcKeyForChar(wchar_t c) {
    if (c >= L'0' && c <= L'9') return std::wstring(1, c);
    switch (c) {
    case L':': case L'u': case L'U': case L'h': case L'H':
        return L":";
    case L',': case L'.':
        return L",";
    case L'+': case L'-': case L'*': case L'/':
        return std::wstring(1, c);
    case L'=':
        return L"=";
    }
    return L"";
}

// Characters arrive as text (not as key codes), so every keyboard layout works.
void MainWindow::OnChar(wchar_t c) {
    if (c < 0x20 || c == 0x7F) return;
    if (tab_ == 1) {
        std::wstring key = CalcKeyForChar(c);
        if (!key.empty()) PressCalc(key);
        return;
    }
    std::wstring s(1, c);
    if (focus_ >= 0 && focus_ == active_) TypeText(focus_, s);
    else InsertText(s);       // nothing focused on the converter tab: type into the active field
}

// The editing keys of a focused field (what the WPF TextBox does by itself).
void MainWindow::FieldKey(int field, UINT vk, bool shift, bool ctrl, bool& handled) {
    Field& f = fields_[field];
    int n = (int)f.text.size();
    handled = true;
    auto moveTo = [&](int pos) {
        pos = std::max(0, std::min(pos, n));
        f.caret = pos;
        if (!shift) f.anchor = pos;
        RestartCaret();
    };
    auto edit = [&](const std::wstring& next, int caret) {
        f.text = next;
        f.caret = f.anchor = caret;
        ApplyFieldEdit(field, next, caret);
    };
    switch (vk) {
    case VK_LEFT:
        if (!shift && f.selLength() > 0) moveTo(f.selStart());
        else moveTo(ctrl ? 0 : f.caret - 1);
        return;
    case VK_RIGHT:
        if (!shift && f.selLength() > 0) moveTo(f.selEnd());
        else moveTo(ctrl ? n : f.caret + 1);
        return;
    case VK_HOME:
        moveTo(0);
        return;
    case VK_END:
        moveTo(n);
        return;
    case VK_BACK:
        if (f.selLength() > 0) {
            std::wstring next = f.text;
            int s = f.selStart();
            next.erase((size_t)s, (size_t)f.selLength());
            edit(next, s);
        } else if (f.caret > 0) {
            std::wstring next = f.text;
            next.erase((size_t)(f.caret - 1), 1);
            edit(next, f.caret - 1);
        }
        return;
    case VK_DELETE:
        if (shift && f.selLength() > 0) {
            CutField(field);
        } else if (f.selLength() > 0) {
            std::wstring next = f.text;
            int s = f.selStart();
            next.erase((size_t)s, (size_t)f.selLength());
            edit(next, s);
        } else if (f.caret < n) {
            std::wstring next = f.text;
            next.erase((size_t)f.caret, 1);
            edit(next, f.caret);
        }
        return;
    case VK_TAB:
        GotFocus(1 - field, true);
        return;
    case VK_INSERT:
        if (ctrl && f.selLength() > 0) CopyField(field);
        else if (shift) PasteField(field);
        else handled = false;
        return;
    }
    if (ctrl) {
        switch (vk) {
        case 'A':
            f.anchor = 0;
            f.caret = n;
            RestartCaret();
            return;
        case 'C':
            CopyField(field);
            return;
        case 'X':
            CutField(field);
            return;
        case 'V':
            PasteField(field);
            return;
        }
    }
    handled = false;
}

// Mirrors OnPreviewKeyDown of the WPF window. Returns true when the key was used.
bool MainWindow::OnKeyDown(UINT vk, bool ctrl, bool shift) {
    // Esc first closes an open menu, like any menu.
    if (vk == VK_ESCAPE && (IsMenuOpen() || IsFieldMenuOpen() || (copy_ && copy_->IsOpen()))) {
        CloseMenus();
        return true;
    }
    if (ctrl && (vk == '1' || vk == VK_NUMPAD1)) { SelectTab(0); return true; }
    if (ctrl && (vk == '2' || vk == VK_NUMPAD2)) { SelectTab(1); return true; }
    if (ctrl && vk == VK_TAB) { SelectTab(1 - tab_); return true; }

    if (tab_ == 0) {
        if (vk == VK_RETURN) { PressConverter(L"CONVERT"); return true; }
        if (vk == VK_ESCAPE) { PressConverter(L"RESET"); return true; }
        if (focus_ >= 0) {
            bool handled = false;
            FieldKey(focus_, vk, shift, ctrl, handled);
            return handled;
        }
        return false;
    }
    if (ctrl) {
        if (vk == 'C') {
            SetClipboard(shift ? engine_.CopyDecimalText() : engine_.CopyMainText());
            return true;
        }
        if (vk == 'V') {
            PasteIntoCalc();
            return true;
        }
        return false;
    }
    switch (vk) {
    case VK_RETURN: PressCalc(L"="); return true;
    case VK_BACK: PressCalc(L"BACK"); return true;
    case VK_DELETE: PressCalc(L"CE"); return true;
    case VK_ESCAPE: PressCalc(L"C"); return true;
    }
    return false;
}

// A left click on a converter field (before it handles the click): a field that does not have focus
// selects everything instead of placing the caret, so typing replaces a computed value.
void MainWindow::MouseDownOnField(int field) {
    bool within = windowActive_ && focus_ == field;
    if (!within) {
        focus_ = field;
        SetActive(field);
        Field& f = fields_[field];
        f.anchor = 0;
        f.caret = (int)f.text.size();
        RestartCaret();
    }
}

}  // namespace app
