// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc GTK front end - the behaviour: converter fields (typing, filtering, focus and select-all rules),
// the keypad, the calculator, keyboard routing, menu actions, settings. A port of the logic of the Windows
// front end (native/app/app_logic.cpp); the field text, caret and selection live in the GtkText widgets.

#include <algorithm>
#include <cstring>
#include <functional>

#include "appinfo.h"
#include "hcwindow.h"
#include "util.h"


namespace hcgtk {

void InstallCss();

namespace conv = hc::converter;

namespace {

std::wstring Sep() { return hc::timemath::DecimalSeparator(); }

// The operators of the expression line are shown as x, the division sign and the minus sign.
std::wstring ExpressionSymbols(std::wstring e) {
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

// A text typed on the calculator tab: the key it stands for ("" for none). Characters arrive as text, so every
// keyboard layout works.
std::wstring CalcKeyForChar(gunichar c) {
    if (c >= '0' && c <= '9') return std::wstring(1, (wchar_t)c);
    switch (c) {
    case ':': case 'u': case 'U': case 'h': case 'H':
        return L":";
    case ',': case '.':
        return L",";
    case '+': case '-': case '*': case '/':
        return std::wstring(1, (wchar_t)c);
    case '=':
        return L"=";
    }
    return L"";
}

// The character a key stands for: the keypad keys by their meaning, everything else by its Unicode value.
gunichar KeyvalChar(guint keyval) {
    if (keyval >= GDK_KEY_KP_0 && keyval <= GDK_KEY_KP_9) return (gunichar)('0' + (keyval - GDK_KEY_KP_0));
    switch (keyval) {
    case GDK_KEY_KP_Add: return '+';
    case GDK_KEY_KP_Subtract: return '-';
    case GDK_KEY_KP_Multiply: return '*';
    case GDK_KEY_KP_Divide: return '/';
    case GDK_KEY_KP_Decimal: return '.';
    case GDK_KEY_KP_Separator: return ',';
    case GDK_KEY_KP_Equal: return '=';
    }
    return gdk_keyval_to_unicode(keyval);
}

bool IsEnterKey(guint k) { return k == GDK_KEY_Return || k == GDK_KEY_KP_Enter || k == GDK_KEY_ISO_Enter; }

// True when the event is a key press of the physical key that carries 'keyval' on some level or group of the layout.
// Where the digits are on the shifted level of the number row (Belgian and French AZERTY: the keys are & and e-acute),
// GTK does not match Ctrl + that key alone to Ctrl+1, it only matches Ctrl+Shift+&. On Windows the shortcut is the key
// (the virtual key of the digit), and so it is here.
bool IsKeyOf(const GdkEvent* constEvent, guint keyval) {
    GdkEvent* event = const_cast<GdkEvent*>(constEvent);
    if (!event || gdk_event_get_event_type(event) != GDK_KEY_PRESS) return false;
    GdkDisplay* display = gdk_event_get_display(event);
    if (!display) return false;
    const guint keycode = gdk_key_event_get_keycode(event);
    GdkKeymapKey* keys = nullptr;
    int n = 0;
    bool hit = false;
    if (gdk_display_map_keyval(display, keyval, &keys, &n)) {
        for (int i = 0; i < n; ++i)
            if (keys[i].keycode == keycode) hit = true;
        g_free(keys);
    }
    return hit;
}

}  // namespace

// Restores the busy flag: a programmatic change of a field must not run the edit handler.
struct Busy {
    HcWindow* w;
    bool old;
    explicit Busy(HcWindow* win) : w(win), old(win->busy_) { w->busy_ = true; }
    ~Busy() { w->busy_ = old; }
};

// ------------------------------------------------------------------ creating and destroying

HcWindow* HcWindow::Create(GtkApplication* app, const std::wstring& settingsPath) { return new HcWindow(app, settingsPath); }

HcWindow::HcWindow(GtkApplication* app, const std::wstring& settingsPath) : settingsPath_(settingsPath), engine_(2) {
    if (!settingsPath_.empty()) {
        hc::settings::Values v = hc::settings::Load(settingsPath_);
        decimals_ = v.decimalPlaces;
        themeMode_ = v.theme;
    }
    engine_.SetDecimalPlaces(decimals_);
    ApplyThemeToManager();
    InstallCss();
    BuildKeys();
    BuildWidgets(app);
    UpdateKeyStates();
    RefreshCalc();
    SetActive(conv::Left);
}

HcWindow::~HcWindow() {
    if (menuModel_) g_object_unref(menuModel_);
}

void HcWindow::Destroy() {
    if (alive_) gtk_window_destroy(GTK_WINDOW(win_));
}

HcWindow* HcWindow::Of(GtkWindow* window) { return window ? static_cast<HcWindow*>(g_object_get_data(G_OBJECT(window), "hc-window")) : nullptr; }

void HcWindow::OnDestroyed() {
    FlushSettings();
    alive_ = false;
    life_.reset();
    about_ = nullptr;
}

void HcWindow::Present() { gtk_window_present(GTK_WINDOW(win_)); }

void HcWindow::UpdateHighContrast() {
    if (adw_style_manager_get_high_contrast(adw_style_manager_get_default())) gtk_widget_add_css_class(win_, "hc-high-contrast");
    else gtk_widget_remove_css_class(win_, "hc-high-contrast");
}

// ------------------------------------------------------------------ theme and settings

void HcWindow::ApplyThemeToManager() {
    AdwColorScheme scheme = ADW_COLOR_SCHEME_DEFAULT;
    if (themeMode_ == hc::settings::ThemeMode::Light) scheme = ADW_COLOR_SCHEME_FORCE_LIGHT;
    else if (themeMode_ == hc::settings::ThemeMode::Dark) scheme = ADW_COLOR_SCHEME_FORCE_DARK;
    adw_style_manager_set_color_scheme(adw_style_manager_get_default(), scheme);
}

void HcWindow::SetThemeAction(hc::settings::ThemeMode mode) {
    GAction* a = g_action_map_lookup_action(G_ACTION_MAP(win_), "theme");
    if (!a) return;
    const char* s = mode == hc::settings::ThemeMode::Light ? "light" : mode == hc::settings::ThemeMode::Dark ? "dark" : "auto";
    g_simple_action_set_state(G_SIMPLE_ACTION(a), g_variant_new_string(s));
}

void HcWindow::SetPlacesAction(int places) {
    GAction* a = g_action_map_lookup_action(G_ACTION_MAP(win_), "places");
    if (a) g_simple_action_set_state(G_SIMPLE_ACTION(a), g_variant_new_int32(places));
}

// The settings file holds all settings. A changed one is written with the other one as it is in the file now
// (another HourCalc may have changed that meanwhile), and only on a change. The write comes after the change
// is on the screen: it runs in an idle callback (a slow disk must not hold the change back). A write that
// fails leaves the setting in force for this session.
void HcWindow::SaveLater(bool places, bool theme) {
    if (settingsPath_.empty()) return;
    savePlacesPending_ = savePlacesPending_ || places;
    saveThemePending_ = saveThemePending_ || theme;
    if (!saveIdle_) {
        saveIdle_ = g_idle_add_full(
            G_PRIORITY_LOW,
            [](gpointer d) -> gboolean {
                HcWindow* w = static_cast<HcWindow*>(d);
                w->saveIdle_ = 0;
                w->FlushSettings();
                return G_SOURCE_REMOVE;
            },
            this, nullptr);
    }
}

void HcWindow::FlushSettings() {
    if (saveIdle_) {
        g_source_remove(saveIdle_);
        saveIdle_ = 0;
    }
    if (settingsPath_.empty()) {
        savePlacesPending_ = saveThemePending_ = false;
        return;
    }
    if (savePlacesPending_) {
        savePlacesPending_ = false;
        ++saves_;
        hc::settings::SavePlaces(settingsPath_, decimals_, themeMode_);
    }
    if (saveThemePending_) {
        saveThemePending_ = false;
        ++saves_;
        hc::settings::SaveTheme(settingsPath_, themeMode_, decimals_);
    }
}

void HcWindow::SetThemeMode(hc::settings::ThemeMode mode) {
    SetThemeAction(mode);
    if (mode == themeMode_) return;
    themeMode_ = mode;
    ApplyThemeToManager();
    SaveLater(false, true);
}

void HcWindow::SetDecimalPlaces(int places) {
    int p = hc::timemath::ClampDecimals(places);
    SetPlacesAction(p);
    if (p == decimals_) return;
    decimals_ = p;
    engine_.SetDecimalPlaces(p);
    RecomputeConverter();
    RefreshCalc();
    SaveLater(true, false);
}

// ------------------------------------------------------------------ converter: fields

FieldState HcWindow::GetField(int field) const {
    FieldState s;
    GtkEditable* e = GTK_EDITABLE(text_[field]);
    s.text = FromUtf8(gtk_editable_get_text(e));
    s.caret = gtk_editable_get_position(e);
    int a = 0, b = 0;
    if (gtk_editable_get_selection_bounds(e, &a, &b)) s.anchor = (s.caret == b) ? a : b;
    else s.anchor = s.caret;
    return s;
}

void HcWindow::SetCaret(int field, int caret, int anchor) {
    GtkEditable* e = GTK_EDITABLE(text_[field]);
    if (caret == anchor) gtk_editable_set_position(e, caret);
    else gtk_editable_select_region(e, anchor, caret);
}

// A programmatic text change of a field: the caret goes to the start (like setting the Text of a TextBox).
// The caller holds a Busy guard when the change must not run the edit handler.
void HcWindow::SetFieldRaw(int field, const std::wstring& text) {
    GtkEditable* e = GTK_EDITABLE(text_[field]);
    if (FromUtf8(gtk_editable_get_text(e)) == text) return;
    Busy b(this);
    gtk_editable_set_text(e, ToUtf8(text).c_str());
    gtk_editable_set_position(e, 0);
}

// Sets the text so the edit handler runs on it (what a change from the keyboard does).
void HcWindow::SetFieldText(int field, const std::wstring& text) {
    GtkEditable* e = GTK_EDITABLE(text_[field]);
    if (FromUtf8(gtk_editable_get_text(e)) == text) return;
    if (busy_) {
        SetFieldRaw(field, text);
        return;
    }
    gtk_editable_set_text(e, ToUtf8(text).c_str());      // "changed" -> OnFieldChanged -> ApplyFieldEdit
}

void HcWindow::OnFieldChanged(int f) {
    if (!alive_ || busy_) return;
    FieldState s = GetField(f);
    ApplyFieldEdit(f, s.text, s.caret);
}

void HcWindow::UpdateConverterLabels() {
    gtk_label_set_text(GTK_LABEL(capLabel_), ToUtf8(caption_).c_str());
}

// The single place where typed field text is sanitized and the other field recomputed.
void HcWindow::ApplyFieldEdit(int field, const std::wstring& raw, int caret) {
    if (busy_) return;
    {
        Busy b(this);
        std::wstring filtered = conv::FilterField(field, raw);
        if (FromUtf8(gtk_editable_get_text(GTK_EDITABLE(text_[field]))) != filtered) {
            int safe = std::max(0, std::min(caret, (int)raw.size()));
            int newCaret = (int)conv::FilterField(field, raw.substr(0, (size_t)safe)).size();
            GtkEditable* e = GTK_EDITABLE(text_[field]);
            gtk_editable_set_text(e, ToUtf8(filtered).c_str());
            gtk_editable_set_position(e, std::min(newCaret, (int)filtered.size()));
        }
        std::wstring normal, other, cap;
        conv::Convert(field, filtered, decimals_, normal, other, cap);
        SetFieldRaw(1 - field, other);
        caption_ = cap;
        source_ = field;
    }
    UpdateConverterLabels();
    FitConverter();
}

// Recomputes the computed field and the caption from the field that was edited last (after the decimal places
// changed). The text of the edited field is left alone.
void HcWindow::RecomputeConverter() {
    std::wstring normal, other, cap;
    conv::Convert(source_, GetField(source_).text, decimals_, normal, other, cap);
    {
        Busy b(this);
        SetFieldRaw(1 - source_, other);
    }
    caption_ = cap;
    UpdateConverterLabels();
    FitConverter();
}

// Puts 'next' in a field with the caret at 'caret' and runs the usual edit on it.
void HcWindow::EditField(int field, const std::wstring& next, int caret) {
    {
        Busy b(this);
        SetFieldRaw(field, next);
        gtk_editable_set_position(GTK_EDITABLE(text_[field]), std::min(caret, (int)next.size()));
    }
    ApplyFieldEdit(field, next, caret);
}

// Replaces the selection of a field (or inserts at the caret) and runs the usual edit.
void HcWindow::TypeText(int field, const std::wstring& s) {
    FieldState tb = GetField(field);
    std::wstring text = tb.text;
    int start = std::min(tb.selStart(), (int)text.size());
    int length = std::min(tb.selLength(), (int)text.size() - start);
    std::wstring next = text;
    next.erase((size_t)start, (size_t)length);
    next.insert((size_t)start, s);
    EditField(field, next, start + (int)s.size());
}

void HcWindow::InsertText(const std::wstring& s) {
    FocusActive();
    TypeText(active_, s);
}

void HcWindow::SetConverterText(int field, const std::wstring& text) {
    SetActive(field);
    SetFieldText(field, text);
    // The text was set as if typed: the edit is applied with the caret at the end.
    GtkEditable* e = GTK_EDITABLE(text_[field]);
    FieldState f = GetField(field);
    if (f.text == text) ApplyFieldEdit(field, f.text, (int)f.text.size());
    gtk_editable_set_position(e, -1);
}

// ------------------------------------------------------------------ active field and focus

void HcWindow::SetActive(int field) {
    active_ = field;
    for (int f = 0; f < 2; f++) {
        if (f == field) gtk_widget_add_css_class(valueBox_[f], "hc-active");
        else gtk_widget_remove_css_class(valueBox_[f], "hc-active");
    }
    UpdateKeyStates();
}

int HcWindow::KeyIndex(const std::wstring& key) const {
    const std::vector<KeyDef>& keys = tab_ == 0 ? convKeys_ : calcKeys_;
    for (size_t i = 0; i < keys.size(); i++)
        if (keys[i].key == key) return (int)i;
    return -1;
}

bool HcWindow::KeyEnabled(const std::wstring& key) const {
    int i = KeyIndex(key);
    if (i < 0) return false;
    return gtk_widget_get_sensitive(KeyAt(i).button);
}

void HcWindow::UpdateKeyStates() {
    for (KeyDef& k : convKeys_) {
        bool on = true;
        if (k.key == L":") on = active_ == 0;
        else if (k.key == L",") on = active_ == 1;
        if (k.button) gtk_widget_set_sensitive(k.button, on);
    }
    for (KeyDef& k : calcKeys_)
        if (k.button) gtk_widget_set_sensitive(k.button, engine_.IsEnabled(k.key));
}

// Moves the keyboard focus to the active input without selecting its text; the caret goes to the end, unless the focus
// comes back from a menu or a dialog (keepCaret), where caret and selection stay as they were.
void HcWindow::FocusActive(bool keepCaret) {
    if (tab_ == 0) {
        bool within = windowActive_ && focus_ == active_;
        if (!within) {
            gtk_text_grab_focus_without_selecting(GTK_TEXT(text_[active_]));
            focus_ = active_;
            if (!keepCaret) gtk_editable_set_position(GTK_EDITABLE(text_[active_]), -1);
        }
    } else {
        gtk_window_set_focus(GTK_WINDOW(win_), nullptr);
        focus_ = -1;
    }
}

void HcWindow::SetWindowActive(bool active) { windowActive_ = active; }

void HcWindow::OnActiveChanged() { windowActive_ = alive_ && gtk_window_is_active(GTK_WINDOW(win_)); }

void HcWindow::OnFocusWidgetChanged() {
    if (!alive_) return;
    GtkWidget* f = gtk_window_get_focus(GTK_WINDOW(win_));
    int idx = f == text_[0] ? 0 : f == text_[1] ? 1 : -1;
    // The focus leaves a field for something else (a menu, a dialog, a header button): its caret and selection are noted,
    // to be put back when the focus returns (GTK selects everything in a field that is given the focus again).
    if (focus_ >= 0 && idx < 0) {
        saved_[focus_] = GetField(focus_);
        hasSaved_[focus_] = true;
    }
    focus_ = idx;
    if (idx >= 0 && idx != active_) SetActive(idx);
}

void HcWindow::GotFocus(int field, bool fromField) {
    if (fromField) {
        gtk_widget_grab_focus(text_[field]);
        gtk_editable_select_region(GTK_EDITABLE(text_[field]), 0, -1);
    } else {
        gtk_text_grab_focus_without_selecting(GTK_TEXT(text_[field]));
    }
    focus_ = field;
    SetActive(field);
}

// A left click on a converter field (before it handles the click): a field that does not have the focus
// selects everything instead of placing the caret, so typing replaces a computed value.
void HcWindow::MouseDownOnField(int field) {
    bool within = windowActive_ && focus_ == field;
    if (!within) GotFocus(field, true);
}

// A press on a field, before the GtkText sees it. True: used here (the GtkText gets nothing of it).
bool HcWindow::OnFieldPressed(int f, int nPress, int button, double x, double y) {
    if (!alive_) return false;
    bool within = windowActive_ && focus_ == f;
    if (button == 3) {
        // Like a text field: the right click gives the field the focus and opens Cut / Copy / Paste at the pointer.
        if (!within) MouseDownOnField(f);
        OpenFieldMenu(f, x, y);
        return true;
    }
    if (!within) {
        MouseDownOnField(f);
        return true;
    }
    // The first click places the caret, the second and the third select everything, the fourth is a plain click again
    // (and so on): the same rhythm as in the Windows version.
    if ((nPress - 1) % 3 != 0) {
        gtk_editable_select_region(GTK_EDITABLE(text_[f]), 0, -1);
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ the keypad and the tabs

void HcWindow::PressConverter(const std::wstring& key) {
    if (key == L"RESET") {
        {
            Busy b(this);
            SetFieldRaw(0, L"");
            SetFieldRaw(1, L"");
            caption_.clear();
        }
        UpdateConverterLabels();
        source_ = conv::Left;
        SetActive(conv::Left);
        FocusActive();
        FitConverter();
        return;
    }
    if (key == L"CONVERT") {
        // Tidies the active field's text, and recomputes the other field from what is in the field (not from the
        // tidied text: 8,058 reads 8,06 afterwards, but the other field keeps the 08:03 it had).
        int f = active_;
        std::wstring normal, other, cap;
        conv::Convert(f, GetField(f).text, decimals_, normal, other, cap);
        {
            Busy b(this);
            SetFieldRaw(f, normal);
            gtk_editable_set_position(GTK_EDITABLE(text_[f]), -1);
            SetFieldRaw(1 - f, other);
            caption_ = cap;
        }
        UpdateConverterLabels();
        source_ = f;
        FitConverter();
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
        FieldState tb = GetField(f);
        if (tb.selLength() > 0) {
            std::wstring next = tb.text;
            next.erase((size_t)tb.selStart(), (size_t)tb.selLength());
            EditField(f, next, tb.selStart());
        } else if (tb.caret > 0) {
            std::wstring next = tb.text;
            next.erase((size_t)(tb.caret - 1), 1);
            EditField(f, next, tb.caret - 1);
        }
        return;
    }
    if (key.size() != 1) return;
    wchar_t c = key[0];
    if (hc::timemath::IsDigit(c)) InsertText(key);
    else if (c == L':' && active_ == conv::Left) InsertText(L":");
    else if ((c == L',' || c == L'.') && active_ == conv::Right) InsertText(Sep());
}

void HcWindow::PressCalc(const std::wstring& key) {
    engine_.Press(key);
    RefreshCalc();
    // The result is told to a screen reader when = is pressed (not at every key: it would talk over the typing).
    if (key == L"=") Announce(error_ ? calcL_ : L"Result " + calcL_ + L", " + calcR_ + L" decimal hours");
}

void HcWindow::Announce(const std::wstring& text) {
    announced_ = text;
    gtk_accessible_announce(GTK_ACCESSIBLE(win_), ToUtf8(text).c_str(), GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
}

void HcWindow::RefreshCalc() {
    error_ = engine_.IsError();
    exprFull_ = ExpressionSymbols(engine_.Expression());
    calcL_ = engine_.MainText();
    calcR_ = engine_.DecimalText();
    calcCap_ = engine_.Caption();
    // The error text may use the width of both columns, in a smaller font.
    calcFontMax_ = error_ ? kErrorMaxScale : 1.0;
    gtk_label_set_text(GTK_LABEL(calcVal_[0]), ToUtf8(calcL_).c_str());
    gtk_label_set_text(GTK_LABEL(calcVal_[1]), ToUtf8(calcR_).c_str());
    gtk_label_set_text(GTK_LABEL(calcCapLabel_), ToUtf8(calcCap_).c_str());
    gtk_widget_set_visible(calcValCol_[1], !error_);
    UpdateKeyStates();
    FitCalculator();
    FitExpression();
}

void HcWindow::Press(const std::wstring& key) { PressKeyOfTab(tab_, key); }

void HcWindow::PressKeyOfTab(int tab, const std::wstring& key) {
    if (tab == 0) PressConverter(key);
    else PressCalc(key);
}

void HcWindow::SelectTab(int index) {
    const char* want = index == 1 ? "calculator" : "converter";
    const char* cur = adw_view_stack_get_visible_child_name(ADW_VIEW_STACK(stack_));
    if (cur && strcmp(cur, want) == 0) OnTabChanged();
    else adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(stack_), want);       // notify -> OnTabChanged
}

void HcWindow::OnTabChanged() {
    if (!alive_) return;
    const char* n = adw_view_stack_get_visible_child_name(ADW_VIEW_STACK(stack_));
    tab_ = (n && strcmp(n, "calculator") == 0) ? 1 : 0;
    FocusActive();
    UpdateKeyStates();
    FitAll();
}

// ------------------------------------------------------------------ keyboard

bool HcWindow::Overlay() const {
    if (!alive_) return false;
    if (adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(win_))) return true;
    if (menuBtn_ && gtk_menu_button_get_active(GTK_MENU_BUTTON(menuBtn_))) return true;
    for (int i = 0; i < 2; i++)
        if ((fieldMenu_[i] && gtk_widget_get_mapped(fieldMenu_[i])) || (copyMenu_[i] && gtk_widget_get_mapped(copyMenu_[i]))) return true;
    return false;
}

// No widget of the content, or a converter field, has the keyboard focus (not a header button).
bool HcWindow::ContentFocused() const {
    GtkWidget* f = gtk_window_get_focus(GTK_WINDOW(win_));
    return f == nullptr || f == text_[0] || f == text_[1];
}

bool HcWindow::CalcKeyForKeyval(guint keyval, GdkModifierType state, std::wstring& key) const {
    if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_META_MASK | GDK_HYPER_MASK)) return false;
    gunichar c = KeyvalChar(keyval);
    if (c == 0) return false;
    key = CalcKeyForChar(c);
    return !key.empty();
}

// A key as the window routes it (SPEC 4-5). Runs in the capture phase of the window.
bool HcWindow::HandleKey(guint keyval, GdkModifierType stateIn, const GdkEvent* event) {
    if (!alive_ || Overlay()) return false;
    guint state = (guint)stateIn & gtk_accelerator_get_default_mod_mask();
    bool ctrl = (state & GDK_CONTROL_MASK) != 0, alt = (state & GDK_ALT_MASK) != 0;
    // Shortcuts match by the key of the layout, also on layouts where the digit or the letter is on another level
    // or group (GTK looks at all of them); without an event (tests) by the translated key.
    auto matches = [&](guint want, guint mods) -> bool {
        if (event && gdk_event_get_event_type(const_cast<GdkEvent*>(event)) == GDK_KEY_PRESS)
            return gdk_key_event_matches(const_cast<GdkEvent*>(event), want, (GdkModifierType)mods) != GDK_KEY_MATCH_NONE;
        return (keyval == want || keyval == gdk_keyval_to_upper(want)) && state == mods;
    };

    if (ctrl && !alt) {
        // Ctrl alone with the key of the digit, whatever the layout puts on it (see IsKeyOf).
        const bool ctrlOnly = state == (guint)GDK_CONTROL_MASK;
        if (matches(GDK_KEY_1, GDK_CONTROL_MASK) || matches(GDK_KEY_KP_1, GDK_CONTROL_MASK) || (ctrlOnly && IsKeyOf(event, GDK_KEY_1))) { SelectTab(0); return true; }
        if (matches(GDK_KEY_2, GDK_CONTROL_MASK) || matches(GDK_KEY_KP_2, GDK_CONTROL_MASK) || (ctrlOnly && IsKeyOf(event, GDK_KEY_2))) { SelectTab(1); return true; }
        if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab || keyval == GDK_KEY_KP_Tab) { SelectTab(1 - tab_); return true; }
        if (tab_ == 1) {
            if (matches(GDK_KEY_c, GDK_CONTROL_MASK)) { CopyCalc(0); return true; }
            if (matches(GDK_KEY_c, GDK_CONTROL_MASK | GDK_SHIFT_MASK)) { CopyCalc(1); return true; }
            if (matches(GDK_KEY_v, GDK_CONTROL_MASK)) { PasteIntoCalc(); return true; }
        }
        return false;         // the converter fields have their own Ctrl+C / X / V / A
    }

    if (tab_ == 0) {
        // The Menu key and Shift+F10: the Cut / Copy / Paste menu of the field that has the focus (a GtkText would open its
        // own, larger one).
        if (focus_ >= 0 && !alt && (keyval == GDK_KEY_Menu || (keyval == GDK_KEY_F10 && (state & GDK_SHIFT_MASK)))) {
            OpenFieldMenu(focus_);
            return true;
        }
        if (IsEnterKey(keyval) && ContentFocused()) { PressConverter(L"CONVERT"); return true; }
        if (keyval == GDK_KEY_Escape) { PressConverter(L"RESET"); return true; }
        if ((keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) && focus_ >= 0 && !alt) {
            GotFocus(1 - focus_, true);
            return true;
        }
        if (focus_ < 0 && !alt) {
            // Text typed while no field has the focus (the focus is on a header button): the active field takes it.
            gunichar c = KeyvalChar(keyval);
            if (c != 0 && (hc::timemath::IsDigit((wchar_t)c) || c == ':' || c == '.' || c == ',' || c == 'u' || c == 'U' || c == 'h' || c == 'H')) {
                InsertText(std::wstring(1, (wchar_t)c));
                return true;
            }
        }
        return false;
    }

    // Calculator.
    if (IsEnterKey(keyval)) {
        if (!ContentFocused()) return false;
        PressCalc(L"=");
        return true;
    }
    if (keyval == GDK_KEY_BackSpace) { PressCalc(L"BACK"); return true; }
    if (keyval == GDK_KEY_Delete || keyval == GDK_KEY_KP_Delete) { PressCalc(L"CE"); return true; }
    if (keyval == GDK_KEY_Escape) { PressCalc(L"C"); return true; }
    std::wstring key;
    if (CalcKeyForKeyval(keyval, (GdkModifierType)state, key)) {
        PressCalc(key);
        return true;
    }
    return false;
}

// A key for the tests: the window routing, and what the GtkText that has the focus does with a typed character.
bool HcWindow::KeyPress(guint keyval, GdkModifierType state) {
    if (HandleKey(keyval, state, nullptr)) return true;
    if (tab_ == 0 && focus_ >= 0 && !Overlay() && !((guint)state & (GDK_CONTROL_MASK | GDK_ALT_MASK))) {
        gunichar c = KeyvalChar(keyval);
        if (c >= 0x20 && c != 0x7F) {
            TypeText(focus_, std::wstring(1, (wchar_t)c));
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------ clipboard

void HcWindow::SetClipboard(const std::wstring& text) {
    gdk_clipboard_set_text(gtk_widget_get_clipboard(win_), ToUtf8(text).c_str());
}

void HcWindow::CopyCalc(int which) { SetClipboard(which == 0 ? engine_.CopyMainText() : engine_.CopyDecimalText()); }

bool HcWindow::PasteTextIntoCalc(const std::wstring& text) {
    if (!engine_.Paste(text)) return false;
    RefreshCalc();
    return true;
}

void HcWindow::PasteIntoCalc() {
    struct Ctx {
        HcWindow* w;
        std::weak_ptr<int> life;
    };
    Ctx* c = new Ctx{ this, life_ };
    gdk_clipboard_read_text_async(
        gtk_widget_get_clipboard(win_), nullptr,
        [](GObject* src, GAsyncResult* res, gpointer d) {
            Ctx* c = static_cast<Ctx*>(d);
            char* t = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(src), res, nullptr);
            if (t) {
                if (!c->life.expired() && c->w->alive_) c->w->PasteTextIntoCalc(FromUtf8(t));
                g_free(t);
            }
            delete c;
        },
        c);
}

void HcWindow::CopyField(int field) {
    if (GetField(field).selLength() > 0) g_signal_emit_by_name(text_[field], "copy-clipboard");
}

void HcWindow::CutField(int field) {
    if (GetField(field).selLength() > 0) g_signal_emit_by_name(text_[field], "cut-clipboard");      // "changed" runs the edit
}

void HcWindow::PasteField(int field) { g_signal_emit_by_name(text_[field], "paste-clipboard"); }

// ------------------------------------------------------------------ menus and dialogs

static void SetActionEnabled(GtkWidget* win, const char* name, bool on) {
    GAction* a = g_action_map_lookup_action(G_ACTION_MAP(win), name);
    if (a) g_simple_action_set_enabled(G_SIMPLE_ACTION(a), on);
}

static void PointPopoverAt(GtkWidget* popover, double x, double y) {
    if (x >= 0 && y >= 0) {
        GdkRectangle r = { (int)x, (int)y, 1, 1 };
        gtk_popover_set_pointing_to(GTK_POPOVER(popover), &r);
    } else {
        gtk_popover_set_pointing_to(GTK_POPOVER(popover), nullptr);
    }
}

// A popover goes before the widget it hangs on does (a popover that is still parented when its parent is finalized
// is a GTK warning). The libadwaita windows take their widgets down before they emit 'destroy', so the
// parent tells: a weak reference on it.
static void UnparentWithParent(gpointer popover, GObject*) {
    GtkWidget* pop = GTK_WIDGET(popover);
    if (gtk_widget_get_parent(pop)) gtk_widget_unparent(pop);
}

// A context menu of a widget, made on its first use and kept (in the slot) for the next.
static GtkWidget* ContextPopover(GtkWidget** slot, GtkWidget* parent, GMenu* model, double x, double y) {
    if (!*slot) {
        GtkWidget* pop = gtk_popover_menu_new_from_model(G_MENU_MODEL(model));
        gtk_popover_set_has_arrow(GTK_POPOVER(pop), FALSE);
        gtk_widget_set_halign(pop, GTK_ALIGN_START);
        gtk_widget_set_parent(pop, parent);
        g_object_add_weak_pointer(G_OBJECT(pop), reinterpret_cast<gpointer*>(slot));
        g_object_weak_ref(G_OBJECT(parent), UnparentWithParent, pop);
        *slot = pop;
    }
    PointPopoverAt(*slot, x, y);
    gtk_popover_popup(GTK_POPOVER(*slot));
    return *slot;
}

GtkWidget* HcWindow::OpenFieldMenu(int field, double x, double y) {
    ctxField_ = field;
    GMenu* m = g_menu_new();
    g_menu_append(m, "Cut", "win.field-cut");
    g_menu_append(m, "Copy", "win.field-copy");
    g_menu_append(m, "Paste", "win.field-paste");
    bool sel = GetField(field).selLength() > 0;
    SetActionEnabled(win_, "field-cut", sel);
    SetActionEnabled(win_, "field-copy", sel);
    GdkClipboard* cb = gtk_widget_get_clipboard(win_);
    SetActionEnabled(win_, "field-paste", gdk_content_formats_contain_gtype(gdk_clipboard_get_formats(cb), G_TYPE_STRING));
    GtkWidget* pop = ContextPopover(&fieldMenu_[field], text_[field], m, x, y);
    g_object_unref(m);
    return pop;
}

GtkWidget* HcWindow::OpenCopyMenu(int which, double x, double y) {
    GMenu* m = g_menu_new();
    g_menu_append(m, "Copy", which == 0 ? "win.copy-main" : "win.copy-decimal");
    // Nothing to copy from the empty decimal value of an error.
    SetActionEnabled(win_, which == 0 ? "copy-main" : "copy-decimal", !(which == 0 ? engine_.CopyMainText() : engine_.CopyDecimalText()).empty());
    GtkWidget* pop = ContextPopover(&copyMenu_[which], calcVal_[which], m, x, y);
    g_object_unref(m);
    return pop;
}

void HcWindow::CloseContextMenus() {
    for (int i = 0; i < 2; i++) {
        if (fieldMenu_[i]) gtk_popover_popdown(GTK_POPOVER(fieldMenu_[i]));
        if (copyMenu_[i]) gtk_popover_popdown(GTK_POPOVER(copyMenu_[i]));
    }
}

GtkWidget* HcWindow::MenuPopover() {
    if (!menuPopover_) {
        menuPopover_ = gtk_popover_menu_new_from_model(MenuModel());
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(menuBtn_), menuPopover_);
    }
    return menuPopover_;
}

void HcWindow::OpenMenu() { gtk_menu_button_popup(GTK_MENU_BUTTON(menuBtn_)); }

void HcWindow::CloseMenu() { gtk_menu_button_popdown(GTK_MENU_BUTTON(menuBtn_)); }

bool HcWindow::IsMenuOpen() const { return gtk_menu_button_get_active(GTK_MENU_BUTTON(menuBtn_)); }

// The keyboard goes back to the content once a menu or the About dialog is closed (a header button would
// otherwise keep the focus and turn Enter and Space into a press on it).
void HcWindow::RefocusContent() {
    if (!alive_) return;
    if (tab_ == 0) {
        const int f = active_;
        FocusActive(true);
        // The caret and the selection the field had when the keyboard left it.
        if (hasSaved_[f]) {
            const FieldState& old = saved_[f];
            if (GetField(f).text == old.text) SetCaret(f, old.caret, old.anchor);
        }
        hasSaved_[0] = hasSaved_[1] = false;
    } else {
        gtk_window_set_focus(GTK_WINDOW(win_), nullptr);
        focus_ = -1;
    }
}

void HcWindow::ScheduleRefocus() {
    struct Ctx {
        HcWindow* w;
        std::weak_ptr<int> life;
    };
    Ctx* c = new Ctx{ this, life_ };
    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        [](gpointer d) -> gboolean {
            Ctx* c = static_cast<Ctx*>(d);
            if (!c->life.expired() && c->w->alive_ && !c->w->Overlay()) c->w->RefocusContent();
            delete c;
            return G_SOURCE_REMOVE;
        },
        c, nullptr);
}

void HcWindow::ShowAbout() {
    if (about_) {
        adw_dialog_present(about_, win_);
        return;
    }
    AdwDialog* d = adw_about_dialog_new();
    AdwAboutDialog* a = ADW_ABOUT_DIALOG(d);
    adw_about_dialog_set_application_name(a, "HourCalc");
    adw_about_dialog_set_application_icon(a, HC_APP_ID);
    adw_about_dialog_set_version(a, HC_VERSION);
    adw_about_dialog_set_developer_name(a, "Optimey CommV");
    adw_about_dialog_set_comments(a, "Converts hours between h:mm and decimal hours, and calculates with hours.");
    adw_about_dialog_set_copyright(a, "Copyright (c) 2026 Optimey CommV");
    adw_about_dialog_set_license_type(a, GTK_LICENSE_GPL_3_0);
    adw_about_dialog_set_website(a, HC_SOURCE_URL);
    about_ = d;
    g_signal_connect(d, "closed", G_CALLBACK(+[](AdwDialog*, gpointer p) {
                         HcWindow* w = static_cast<HcWindow*>(p);
                         w->about_ = nullptr;
                         if (w->alive_) w->ScheduleRefocus();
                     }),
                     this);
    adw_dialog_present(d, win_);
}

void HcWindow::CloseAbout() {
    if (about_) adw_dialog_force_close(about_);
}

bool HcWindow::IsAboutOpen() const { return about_ != nullptr; }

// ------------------------------------------------------------------ for tests and tools

namespace {
struct Flag { bool set = false; };
}

static void WaitMainLoop(const std::function<bool()>& done, int timeoutMs) {
    Flag timedOut;
    guint t = g_timeout_add(timeoutMs, [](gpointer d) -> gboolean { static_cast<Flag*>(d)->set = true; return G_SOURCE_REMOVE; }, &timedOut);
    while (!done() && !timedOut.set) g_main_context_iteration(nullptr, TRUE);
    if (!timedOut.set) g_source_remove(t);
}

void HcWindow::Settle(int frames) {
    gtk_window_present(GTK_WINDOW(win_));
    WaitMainLoop([&] { return gtk_widget_get_mapped(win_) != FALSE; }, 5000);
    // There is no window manager on the virtual display: give the window the keyboard focus itself, so it is
    // the active window (the header bar is not in its backdrop look, the caret blinks).
    GdkSurface* s = gtk_native_get_surface(GTK_NATIVE(win_));
    if (s && GDK_IS_TOPLEVEL(s)) {
        gdk_toplevel_focus(GDK_TOPLEVEL(s), GDK_CURRENT_TIME);
        WaitMainLoop([&] { return gtk_window_is_active(GTK_WINDOW(win_)) != FALSE; }, 500);
    }
    WaitFrames(frames);
}

// The first frame after a change does the layout and the painting; a later one only exists when that layout
// asked for another (a value that shrinks its font does), so the first is waited for, the others a moment each.
void HcWindow::WaitFrames(int n) {
    GdkFrameClock* fc = gtk_widget_get_frame_clock(win_);
    if (!fc) return;
    gint64 start = gdk_frame_clock_get_frame_counter(fc);
    gtk_widget_queue_draw(win_);
    WaitMainLoop([&] { return gdk_frame_clock_get_frame_counter(fc) >= start + 1; }, 3000);
    for (int i = 1; i < n; i++) {
        gint64 seen = gdk_frame_clock_get_frame_counter(fc);
        WaitMainLoop([&] { return gdk_frame_clock_get_frame_counter(fc) > seen; }, 60);
    }
}

}  // namespace hcgtk
