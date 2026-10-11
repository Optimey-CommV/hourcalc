// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc GTK front end - the headless self test (hourcalc-gtk-test --selftest). It drives the real widgets
// (GtkText fields, keypad buttons, the window's key handling, the actions of the menu, AdwStyleManager, the
// settings file) and checks the behaviour of SPEC 4-6b, in a ',' and in a '.' locale (the separator is
// overridden through the core, no locale needs to be installed), with the system scheme light or dark.

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "appinfo.h"
#include "app.h"
#include "testutil.h"

namespace hctest {

namespace {

using hcgtk::FieldState;
using hcgtk::FromUtf8;
using hcgtk::ToUtf8;
namespace conv = hc::converter;
namespace tmath = hc::timemath;
using hc::settings::ThemeMode;

int g_checks = 0, g_failures = 0;
std::wstring g_sep = L",";
bool g_systemDark = false;
bool g_verbose = false;
const char* g_group = "";
std::string g_only;          // run only the groups whose name contains this
bool g_strictMin = true;     // the default font: the content gets as narrow as 300 px (other fonts need what they need)

void Fail(const std::wstring& what, const std::wstring& detail) {
    ++g_failures;
    std::fprintf(stderr, "FAIL [%s] %s: %s\n", g_group, ToUtf8(what).c_str(), ToUtf8(detail).c_str());
}

void Eq(const std::wstring& what, const std::wstring& expected, const std::wstring& actual) {
    ++g_checks;
    if (expected != actual) Fail(what, L"expected <" + expected + L"> got <" + actual + L">");
    else if (g_verbose) std::printf("ok   %s\n", ToUtf8(what).c_str());
}

void True(const std::wstring& what, bool v) {
    ++g_checks;
    if (!v) Fail(what, L"is false");
    else if (g_verbose) std::printf("ok   %s\n", ToUtf8(what).c_str());
}

std::wstring I(long long v) { return std::to_wstring(v); }

// A literal with ',' as the decimal separator, in the separator of the run.
std::wstring S(const wchar_t* s) {
    std::wstring r(s);
    std::wstring out;
    for (wchar_t c : r) {
        if (c == L',') out += g_sep;
        else out += c;
    }
    return out;
}

std::wstring LT(HcWindow& w) { return w.ConverterText(0); }
std::wstring RT(HcWindow& w) { return w.ConverterText(1); }

// Presses a row of keypad keys: every character is a key ("7:30+7:20=").
void Keys(HcWindow& w, const wchar_t* keys) {
    for (const wchar_t* p = keys; *p; ++p) w.Press(std::wstring(1, *p));
}

// Typed text: every character is a key press of the window (what the GtkText would insert is done by KeyPress).
void RaiseText(HcWindow& w, const std::wstring& text) {
    for (wchar_t c : text) w.KeyPress(gdk_unicode_to_keyval((guint32)c));
}

void RaiseKey(HcWindow& w, guint key, guint state = 0) { w.KeyPress(key, (GdkModifierType)state); }

void NoFocus(HcWindow& w) { gtk_window_set_focus(w.Window(), nullptr); }

GtkWidget* W(HcWindow& w) { return GTK_WIDGET(w.Window()); }

bool Bounds(GtkWidget* widget, GtkWidget* in, graphene_rect_t* r) { return gtk_widget_compute_bounds(widget, in, r) != FALSE; }

// ------------------------------------------------------------------ converter

void Check(HcWindow& w, int field, const wchar_t* input, const wchar_t* expectedOther) {
    w.Press(L"RESET");
    w.SetConverterText(field, S(input));
    Eq(L"field " + I(field) + L" <" + input + L"> other", S(expectedOther), w.ConverterText(1 - field));
}

void ConverterWiring() {
    Win wp;
    HcWindow& w = *wp;
    Eq(L"starts on the converter", L"0", I(w.CurrentTab()));
    Eq(L"left field is active at start", L"0", I(w.ActiveField()));
    Eq(L"left field has the focus at start", L"0", I(w.FocusField()));
    Eq(L"left empty", L"", LT(w));
    Eq(L"right empty", L"", RT(w));

    Keys(w, L"830");
    Eq(L"keypad left", L"830", LT(w));
    Eq(L"keypad right follows", S(L"8,50"), RT(w));
    Eq(L"caption", L"8 h 30 min", w.ConverterCaption());
    Eq(L"caption label", L"8 h 30 min", FromUtf8(gtk_label_get_text(GTK_LABEL(w.ConverterCaptionLabel()))));
    w.Press(L"CONVERT");
    Eq(L"convert normalizes left", L"08:30", LT(w));
    Eq(L"convert keeps right", S(L"8,50"), RT(w));

    w.Press(L"RESET");
    Keys(w, L"8,");
    Eq(L"',' is ignored while left is active", L"8", LT(w));
    Keys(w, L":3");
    Eq(L"':' accepted on the left", L"8:3", LT(w));
    Eq(L"right follows 8:3", S(L"8,05"), RT(w));
    w.Press(L"0");
    Eq(L"8:30", L"8:30", LT(w));
    Eq(L"8:30 right", S(L"8,50"), RT(w));
    w.Press(L"5");
    Eq(L"third minute digit is rejected", L"8:30", LT(w));

    w.SetConverterText(1, S(L"1,5"));
    Eq(L"right made active", L"1", I(w.ActiveField()));
    w.Press(L"RESET");
    Eq(L"reset left", L"", LT(w));
    Eq(L"reset right", L"", RT(w));
    Eq(L"reset caption", L"", w.ConverterCaption());
    Eq(L"reset activates left", L"0", I(w.ActiveField()));

    w.SetConverterText(1, L"");
    Keys(w, L"8,50");
    Eq(L"keypad right", S(L"8,50"), RT(w));
    Eq(L"left follows", L"08:30", LT(w));
    w.Press(L":");
    Eq(L"':' is ignored while right is active", S(L"8,50"), RT(w));
    w.Press(L",");
    Eq(L"second ',' is ignored", S(L"8,50"), RT(w));

    w.Press(L"BACK");
    Eq(L"backspace right", S(L"8,5"), RT(w));
    Eq(L"backspace left follows", L"08:30", LT(w));
    w.Press(L"BACK");
    w.Press(L"BACK");
    Eq(L"backspace to the separator", L"8", RT(w));
    w.Press(L"CE");
    Eq(L"CE clears the active field", L"", RT(w));
    Eq(L"CE clears the other field", L"", LT(w));
    Eq(L"CE clears the caption", L"", w.ConverterCaption());

    w.SetConverterText(1, S(L"8,5"));
    w.Press(L"CONVERT");
    Eq(L"convert normalizes right", S(L"8,50"), RT(w));
    Eq(L"convert left", L"08:30", LT(w));
    w.SetConverterText(0, L"0:90");
    w.Press(L"CONVERT");
    Eq(L"convert 0:90", L"01:30", LT(w));
    Eq(L"convert 0:90 right", S(L"1,50"), RT(w));

    Check(w, 0, L"08:30", L"8,50");
    Check(w, 1, L"8,50", L"08:30");
    Check(w, 1, L"8,5", L"08:30");
    Check(w, 0, L"08:20", L"8,33");
    Check(w, 1, L"8,33", L"08:20");
    Check(w, 0, L"830", L"8,50");
    Check(w, 0, L"8.30", L"8,50");
    Check(w, 0, L"8u30", L"8,50");
    Check(w, 0, L"0:45", L"0,75");
    Check(w, 1, L"0,1", L"00:06");
    Check(w, 0, L"123:45", L"123,75");
    Check(w, 1, L"8,51", L"08:31");
    Check(w, 0, L"0:90", L"1,50");
}

void ConverterEditing() {
    Win wp;
    HcWindow& w = *wp;
    True(L"colon key exists", w.KeyIndex(L":") >= 0);
    True(L"decimal key exists", w.KeyIndex(L",") >= 0);
    True(L"colon enabled when left is active", w.KeyEnabled(L":"));
    True(L"decimal disabled when left is active", !w.KeyEnabled(L","));
    w.SetConverterText(1, L"");
    True(L"colon disabled when right is active", !w.KeyEnabled(L":"));
    True(L"decimal enabled when right is active", w.KeyEnabled(L","));
    Eq(L"decimal key label is the culture's separator", tmath::DecimalSeparator(), w.KeyAt(w.KeyIndex(L",")).label);
    Eq(L"decimal key label in the button", tmath::DecimalSeparator(), FromUtf8(gtk_button_get_label(GTK_BUTTON(w.KeyAt(w.KeyIndex(L",")).button))));
    w.Press(L"RESET");
    True(L"reset re-enables the colon", w.KeyEnabled(L":"));
    True(L"reset disables the decimal key", !w.KeyEnabled(L","));

    w.SetConverterText(0, L"8,30");
    Eq(L"comma ignored in the left field", L"830", LT(w));
    w.SetConverterText(0, L"8h30");
    Eq(L"h becomes a colon", L"8:30", LT(w));
    w.SetConverterText(1, L"8:50");
    Eq(L"colon ignored in the right field", L"850", RT(w));
    w.SetConverterText(1, L"8.5");
    Eq(L"dot shown as the culture's separator", S(L"8,5"), RT(w));
    w.SetConverterText(1, L"0,1234567");
    Eq(L"at most 6 decimals", S(L"0,123456"), RT(w));
    w.SetConverterText(0, L"abc");
    Eq(L"letters rejected", L"", LT(w));
    Eq(L"nothing to convert", L"", RT(w));
}

// What the real GTK editing paths do (insert, delete, cut, paste, selection): they all end in "changed".
void RealEditing() {
    Win wp;
    HcWindow& w = *wp;
    GtkEditable* e0 = GTK_EDITABLE(w.FieldWidget(0));
    GtkEditable* e1 = GTK_EDITABLE(w.FieldWidget(1));
    int pos = 0;
    gtk_editable_insert_text(e0, "8u30", -1, &pos);
    Eq(L"insert_text 8u30", L"8:30", LT(w));
    Eq(L"insert_text 8u30: right", S(L"8,50"), RT(w));
    Eq(L"insert_text 8u30: caption", L"8 h 30 min", w.ConverterCaption());
    pos = 4;
    gtk_editable_insert_text(e0, "5", -1, &pos);
    Eq(L"a third minute digit is dropped", L"8:30", LT(w));
    gtk_editable_delete_text(e0, 2, 4);
    Eq(L"delete_text", L"8:", LT(w));
    Eq(L"delete_text: right follows", S(L"8,00"), RT(w));
    gtk_editable_select_region(e0, 0, -1);
    gtk_editable_delete_selection(e0);
    Eq(L"delete selection clears left", L"", LT(w));
    Eq(L"delete selection clears right", L"", RT(w));
    Eq(L"delete selection clears the caption", L"", w.ConverterCaption());

    pos = 0;
    gtk_editable_insert_text(e1, "8.5", -1, &pos);
    Eq(L"right: dot is the separator", S(L"8,5"), RT(w));
    Eq(L"right: left follows", L"08:30", LT(w));

    // GtkText key bindings: backspace, delete, cut.
    gtk_editable_set_position(e1, -1);
    g_signal_emit_by_name(e1, "delete-from-cursor", GTK_DELETE_CHARS, -1);
    Eq(L"backspace key binding", S(L"8,"), RT(w));
    Eq(L"backspace key binding: left follows", L"08:00", LT(w));
    gtk_editable_select_region(e1, 0, -1);
    g_signal_emit_by_name(e1, "cut-clipboard");
    Eq(L"cut clears the field", L"", RT(w));
    Eq(L"cut clears the other field", L"", LT(w));
}

// ------------------------------------------------------------------ calculator

void CalculatorWiring() {
    Win wp;
    HcWindow& w = *wp;
    w.SelectTab(1);
    Eq(L"calculator tab", L"1", I(w.CurrentTab()));
    Keys(w, L"7:30+7:20=");
    Eq(L"calc left", L"14:50", w.Engine().MainText());
    Eq(L"calc right", S(L"14,83"), w.Engine().DecimalText());
    Eq(L"calc expression", L"07:30 + 07:20 =", w.Engine().Expression());
    Eq(L"calc left label", L"14:50", FromUtf8(gtk_label_get_text(GTK_LABEL(w.CalcValueLabel(0)))));
    Eq(L"calc right label", S(L"14,83"), FromUtf8(gtk_label_get_text(GTK_LABEL(w.CalcValueLabel(1)))));
    Eq(L"calc caption label", L"14 h 50 min", FromUtf8(gtk_label_get_text(GTK_LABEL(w.CalcCaptionLabel()))));
    Eq(L"calc expression label", L"07:30 + 07:20 =", FromUtf8(gtk_label_get_text(GTK_LABEL(w.ExpressionLabel()))));
    w.Press(L"C");
    Eq(L"calc cleared", L"00:00", w.Engine().MainText());
    Eq(L"calc cleared right label", S(L"0,00"), FromUtf8(gtk_label_get_text(GTK_LABEL(w.CalcValueLabel(1)))));

    Keys(w, L"7:30/0=");
    Eq(L"div0 text", L"Cannot divide by zero", w.Engine().MainText());
    Eq(L"div0 label", L"Cannot divide by zero", FromUtf8(gtk_label_get_text(GTK_LABEL(w.CalcValueLabel(0)))));
    True(L"div0 hides the right value", !w.CalcRightVisible() && !gtk_widget_get_visible(gtk_widget_get_parent(w.CalcValueLabel(1))));
    True(L"div0 smaller font", w.CalcScale() <= HcWindow::kErrorMaxScale + 1e-9);
    True(L"plus disabled in the error state", !w.KeyEnabled(L"+"));
    True(L"equals disabled in the error state", !w.KeyEnabled(L"="));
    True(L"digit enabled in the error state", w.KeyEnabled(L"5"));
    w.Press(L"C");
    True(L"plus enabled again after C", w.KeyEnabled(L"+"));
    True(L"equals enabled again after C", w.KeyEnabled(L"="));
    True(L"right value visible again", w.CalcRightVisible() && gtk_widget_get_visible(gtk_widget_get_parent(w.CalcValueLabel(1))));

    w.SelectTab(0);
    Keys(w, L"12");
    Eq(L"converter after calculator use", L"12", LT(w));
    Eq(L"converter value", S(L"12,00"), RT(w));
    w.SelectTab(1);
    Eq(L"calculator state survives a tab switch", L"00:00", w.Engine().MainText());
}

struct CalcCase {
    const wchar_t* keys;
    const wchar_t* left;
    const wchar_t* right;
};

void SpecTables() {
    Win wp;
    HcWindow& w = *wp;
    w.SelectTab(1);
    // SPEC 5: the required examples.
    const CalcCase cases[] = {
        { L"7:30+7:20=", L"14:50", L"14,83" },
        { L"8+0:30=", L"08:30", L"8,50" },
        { L"7,5+7:20=", L"14:50", L"14,83" },
        { L"7:30*5=", L"37:30", L"37,50" },
        { L"7:30/4=", L"01:53", L"1,88" },
        { L"7:00-8:30=", L"-01:30", L"-1,50" },
        { L"7:30+1:00==", L"09:30", L"9,50" },
        { L"0:90=", L"01:30", L"1,50" },
        { L"1:00+2:00*2=", L"06:00", L"6,00" },
        { L"7:30/0=", L"Cannot divide by zero", L"" },
    };
    for (const CalcCase& c : cases) {
        w.Press(L"C");
        Keys(w, c.keys);
        Eq(std::wstring(L"calc ") + c.keys + L" left", S(c.left), w.CalcMain());
        Eq(std::wstring(L"calc ") + c.keys + L" right", S(c.right), w.CalcDecimal());
        Eq(std::wstring(L"calc ") + c.keys + L" left label", S(c.left), FromUtf8(gtk_label_get_text(GTK_LABEL(w.CalcValueLabel(0)))));
        Eq(std::wstring(L"calc ") + c.keys + L" right label", S(c.right), FromUtf8(gtk_label_get_text(GTK_LABEL(w.CalcValueLabel(1)))));
    }
    w.Press(L"C");
    Keys(w, L"1,5*2=");
    Eq(L"decimal entry 1,5 h times 2", L"03:00", w.CalcMain());
    w.Press(L"C");
    Keys(w, L"7:30/4=*4=");
    Eq(L"the accumulator keeps full precision", L"07:30", w.CalcMain());

    // SPEC 6: decimal places.
    w.SelectTab(0);
    const struct { int places; const wchar_t* e; const wchar_t* d; const wchar_t* m; } places[] = {
        { 2, L"8,33", L"1,88", L"0,02" }, { 3, L"8,333", L"1,875", L"0,017" }, { 4, L"8,3333", L"1,8750", L"0,0167" },
        { 5, L"8,33333", L"1,87500", L"0,01667" }, { 6, L"8,333333", L"1,875000", L"0,016667" },
    };
    for (const auto& p : places) {
        w.SetDecimalPlaces(p.places);
        w.Press(L"RESET");
        w.SetConverterText(0, L"08:20");
        Eq(L"08:20 with " + I(p.places) + L" places", S(p.e), RT(w));
        w.Press(L"RESET");
        w.SetConverterText(0, L"0:01");
        Eq(L"0:01 with " + I(p.places) + L" places", S(p.m), RT(w));
        w.SelectTab(1);
        w.Press(L"C");
        Keys(w, L"7:30/4=");
        Eq(L"7:30 / 4 with " + I(p.places) + L" places", S(p.d), w.CalcDecimal());
        w.SelectTab(0);
    }
    w.SetDecimalPlaces(2);
}

// ------------------------------------------------------------------ keyboard routing

void KeyboardRouting() {
    {
        Win wp;
        HcWindow& w = *wp;
        w.SelectTab(1);
        RaiseText(w, L"7:30+7:20");
        Eq(L"calc typed expression", L"07:30 +", w.Engine().Expression());
        Eq(L"calc typed entry", L"7:20", w.Engine().MainText());
        RaiseKey(w, GDK_KEY_Return);
        Eq(L"calc Enter is equals", L"14:50", w.Engine().MainText());
        Eq(L"calc Enter expression", L"07:30 + 07:20 =", w.Engine().Expression());
        RaiseKey(w, GDK_KEY_Escape);
        Eq(L"calc Esc is C", L"00:00", w.Engine().MainText());
        RaiseText(w, L"7u30");
        Eq(L"calc u is a time separator", L"7:30", w.Engine().MainText());
        RaiseText(w, L"x");
        Eq(L"calc unknown character ignored", L"7:30", w.Engine().MainText());
        RaiseKey(w, GDK_KEY_BackSpace);
        Eq(L"calc Backspace", L"7:3", w.Engine().MainText());
        RaiseKey(w, GDK_KEY_Delete);
        Eq(L"calc Delete is CE", L"0", w.Engine().MainText());
        RaiseText(w, L"8");
        RaiseText(w, L".");
        RaiseText(w, L"5");
        RaiseText(w, L"=");
        Eq(L"calc decimal point and =", L"08:30", w.Engine().MainText());
        RaiseText(w, L"*2=");
        Eq(L"calc * as text", L"17:00", w.Engine().MainText());
        RaiseText(w, L"/0=");
        Eq(L"calc / as text", L"Cannot divide by zero", w.Engine().MainText());
        RaiseText(w, L"5");
        Eq(L"calc digit after error", L"5", w.Engine().MainText());
    }
    {
        // The keypad keys of the number block, and Shift for digits (AZERTY types 1 as Shift+&).
        Win wp;
        HcWindow& w = *wp;
        w.SelectTab(1);
        RaiseKey(w, GDK_KEY_KP_7);
        RaiseKey(w, GDK_KEY_KP_Decimal);
        RaiseKey(w, GDK_KEY_KP_5);
        RaiseKey(w, GDK_KEY_KP_Add);
        RaiseKey(w, GDK_KEY_1, GDK_SHIFT_MASK);
        RaiseKey(w, GDK_KEY_KP_Enter);
        Eq(L"number block: 7,5 + 1 = (digit with Shift)", L"08:30", w.Engine().MainText());
        RaiseKey(w, GDK_KEY_KP_Multiply);
        RaiseKey(w, GDK_KEY_KP_2);
        RaiseKey(w, GDK_KEY_KP_Equal);
        Eq(L"number block: * 2 =", L"17:00", w.Engine().MainText());
        RaiseKey(w, GDK_KEY_KP_Divide);
        RaiseKey(w, GDK_KEY_KP_4);
        RaiseKey(w, GDK_KEY_KP_Subtract);
        RaiseKey(w, GDK_KEY_KP_1);
        RaiseKey(w, GDK_KEY_KP_Enter);
        Eq(L"number block: / 4 - 1 =", L"03:15", w.Engine().MainText());
        RaiseKey(w, GDK_KEY_KP_Delete);
        Eq(L"number block Delete is CE", L"00:00", w.Engine().MainText());
        // Ctrl+digit is a shortcut, not text.
        RaiseKey(w, GDK_KEY_5, GDK_CONTROL_MASK);
        Eq(L"Ctrl+5 is not text", L"00:00", w.Engine().MainText());
        Eq(L"Ctrl+5 leaves the calculator tab", L"1", I(w.CurrentTab()));
    }
    {
        Win wp;
        HcWindow& w = *wp;
        NoFocus(w);
        RaiseText(w, L"8u30");
        Eq(L"converter text without focus", L"8:30", LT(w));
        Eq(L"converter text without focus, right", S(L"8,50"), RT(w));
        w.SetConverterText(0, L"830");
        RaiseKey(w, GDK_KEY_Return);
        Eq(L"converter Enter converts", L"08:30", LT(w));
        RaiseKey(w, GDK_KEY_Escape);
        Eq(L"converter Esc resets left", L"", LT(w));
        Eq(L"converter Esc resets right", L"", RT(w));
        Eq(L"converter Esc activates left", L"0", I(w.ActiveField()));
        w.SetConverterText(1, L"");
        NoFocus(w);
        RaiseText(w, L"8,5");
        Eq(L"converter text goes to the active (right) field", S(L"8,5"), RT(w));
        Eq(L"converter right to left", L"08:30", LT(w));
        // A focused field gets its text from the GtkText: the window leaves characters alone.
        w.GotFocus(1, true);
        True(L"a printable key is not used by the window while a field has the focus", !w.HandleKey(GDK_KEY_5, (GdkModifierType)0, nullptr));
    }
    {
        // Ctrl+1 / Ctrl+2 / Ctrl+Tab switch the tab.
        Win wp;
        HcWindow& w = *wp;
        RaiseKey(w, GDK_KEY_2, GDK_CONTROL_MASK);
        Eq(L"Ctrl+2 selects the calculator", L"1", I(w.CurrentTab()));
        RaiseKey(w, GDK_KEY_1, GDK_CONTROL_MASK);
        Eq(L"Ctrl+1 selects the converter", L"0", I(w.CurrentTab()));
        RaiseKey(w, GDK_KEY_Tab, GDK_CONTROL_MASK);
        Eq(L"Ctrl+Tab switches to the other tab", L"1", I(w.CurrentTab()));
        RaiseKey(w, GDK_KEY_Tab, GDK_CONTROL_MASK);
        Eq(L"Ctrl+Tab switches back", L"0", I(w.CurrentTab()));
        RaiseKey(w, GDK_KEY_KP_2, GDK_CONTROL_MASK);
        Eq(L"Ctrl+Numpad2 selects the calculator", L"1", I(w.CurrentTab()));
        RaiseKey(w, GDK_KEY_ISO_Left_Tab, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
        Eq(L"Ctrl+Shift+Tab switches back", L"0", I(w.CurrentTab()));
    }
    {
        // The window's key controller itself: the signal of the capture controller reaches HandleKey.
        Win wp;
        HcWindow& w = *wp;
        w.SelectTab(1);
        GListModel* ctrls = gtk_widget_observe_controllers(W(w));
        GtkEventController* key = nullptr;
        for (guint i = 0; i < g_list_model_get_n_items(ctrls); i++) {
            GObject* o = static_cast<GObject*>(g_list_model_get_item(ctrls, i));
            if (GTK_IS_EVENT_CONTROLLER_KEY(o) && g_object_get_data(o, "hc-keys"))
                key = GTK_EVENT_CONTROLLER(o);
            g_object_unref(o);
        }
        g_object_unref(ctrls);
        True(L"the window has a capture key controller", key != nullptr);
        if (key) {
            gboolean ret = FALSE;
            g_signal_emit_by_name(key, "key-pressed", (guint)GDK_KEY_7, 0u, (GdkModifierType)0, &ret);
            True(L"the key controller uses a digit on the calculator", ret != FALSE);
            Eq(L"... and the calculator got it", L"7", w.Engine().MainText());
            g_signal_emit_by_name(key, "key-pressed", (guint)GDK_KEY_a, 0u, (GdkModifierType)0, &ret);
            True(L"the key controller leaves a letter alone", ret == FALSE);
        }
    }
    {
        // The keypad buttons: a click is a press of the key.
        Win wp;
        HcWindow& w = *wp;
        for (const hcgtk::KeyDef& k : w.ConverterKeys()) True(L"converter key " + k.key + L" has a button", k.button != nullptr);
        const hcgtk::KeyDef& seven = w.KeyAt(w.KeyIndex(L"7"));
        g_signal_emit_by_name(seven.button, "clicked");
        Eq(L"a click on the 7 key types a 7", L"7", LT(w));
        const hcgtk::KeyDef& convert = w.KeyAt(w.KeyIndex(L"CONVERT"));
        g_signal_emit_by_name(convert.button, "clicked");
        Eq(L"a click on Convert converts", L"07:00", LT(w));
        w.SelectTab(1);
        g_signal_emit_by_name(w.KeyAt(w.KeyIndex(L"8")).button, "clicked");
        g_signal_emit_by_name(w.KeyAt(w.KeyIndex(L"+")).button, "clicked");
        g_signal_emit_by_name(w.KeyAt(w.KeyIndex(L"2")).button, "clicked");
        g_signal_emit_by_name(w.KeyAt(w.KeyIndex(L"=")).button, "clicked");
        Eq(L"clicks on the calculator keys", L"10:00", w.CalcMain());
        for (const hcgtk::KeyDef& k : w.CalculatorKeys()) {
            True(L"calculator key " + k.key + L" cannot take the focus", !gtk_widget_get_focusable(k.button));
        }
        for (const hcgtk::KeyDef& k : w.ConverterKeys()) {
            True(L"converter key " + k.key + L" cannot take the focus", !gtk_widget_get_focusable(k.button));
        }
    }
}

// ------------------------------------------------------------------ the converter fields (focus, typing)

void WindowTyping() {
    Win wp;
    HcWindow& w = *wp;
    w.GotFocus(0, true);
    w.TypeText(0, L"8u30");
    Eq(L"left typed 8u30", L"8:30", LT(w));
    Eq(L"left typed 8u30 caret", L"4", I(w.GetField(0).caret));
    Eq(L"left typed 8u30 right", S(L"8,50"), RT(w));
    Eq(L"left typed 8u30 caption", L"8 h 30 min", w.ConverterCaption());
    w.TypeText(0, L"5");
    Eq(L"third minute digit rejected, text", L"8:30", LT(w));
    Eq(L"third minute digit rejected, caret", L"4", I(w.GetField(0).caret));

    w.Press(L"RESET");
    w.TypeText(0, L"8.30");
    Eq(L"left typed 8.30", L"8:30", LT(w));
    w.Press(L"RESET");
    w.TypeText(0, L"8h30");
    Eq(L"left typed 8h30", L"8:30", LT(w));
    w.Press(L"RESET");
    w.TypeText(0, L"8,30");
    Eq(L"left typed 8,30: ',' is ignored", L"830", LT(w));
    Eq(L"left typed 8,30: value", S(L"8,50"), RT(w));
    w.Press(L"RESET");
    w.TypeText(0, L"8x:3");
    Eq(L"left typed 8x:3: letters ignored", L"8:3", LT(w));
    Eq(L"left typed 8x:3: caret", L"3", I(w.GetField(0).caret));
    w.Press(L"RESET");
    w.TypeText(0, L"12345678");
    Eq(L"left typed 8 digits", L"1234567", LT(w));

    w.Press(L"RESET");
    w.GotFocus(1, true);
    Eq(L"right made active by focus", L"1", I(w.ActiveField()));
    w.TypeText(1, L"8.5");
    Eq(L"right typed 8.5", S(L"8,5"), RT(w));
    Eq(L"right typed 8.5 caret", L"3", I(w.GetField(1).caret));
    Eq(L"right typed 8.5 left", L"08:30", LT(w));
    w.Press(L"RESET");
    w.GotFocus(1, true);
    w.TypeText(1, L"8,5");
    Eq(L"right typed 8,5", S(L"8,5"), RT(w));
    Eq(L"right typed 8,5 left", L"08:30", LT(w));
    w.Press(L"RESET");
    w.GotFocus(1, true);
    w.TypeText(1, L"8:30");
    Eq(L"right typed 8:30: ':' is ignored", L"830", RT(w));
    w.Press(L"RESET");
    w.GotFocus(1, true);
    w.TypeText(1, L"1,12345678");
    Eq(L"right typed 8 decimals", S(L"1,123456"), RT(w));
    w.Press(L"RESET");
    w.GotFocus(1, true);
    w.TypeText(1, L",5");
    Eq(L"right typed ,5", S(L",5"), RT(w));
    Eq(L"right typed ,5 left", L"00:30", LT(w));

    w.Press(L"RESET");
    w.GotFocus(0, true);
    w.TypeText(0, L"830");
    w.SetCaret(0, 1, 1);
    w.TypeText(0, L":");
    Eq(L"':' typed in the middle", L"8:30", LT(w));
    Eq(L"':' typed in the middle caret", L"2", I(w.GetField(0).caret));
    w.TypeText(0, L"5");
    Eq(L"digit in the middle pushes the rest on, 2 minute digits at most", L"8:53", LT(w));
    w.SetCaret(0, (int)LT(w).size(), 0);
    w.TypeText(0, L"9");
    Eq(L"typing over a selection", L"9", LT(w));
    Eq(L"typing over a selection: right", S(L"9,00"), RT(w));

    w.SetCaret(0, (int)LT(w).size(), 0);
    w.TypeText(0, L"");       // deleting the selection
    Eq(L"deleting the selection clears the left", L"", LT(w));
    Eq(L"deleting the selection clears the right", L"", RT(w));
    Eq(L"deleting the selection clears the caption", L"", w.ConverterCaption());

    w.Press(L"RESET");
    w.GotFocus(0, true);
    w.TypeText(0, L"8:30");
    Eq(L"left source gives the right", S(L"8,50"), RT(w));
    w.GotFocus(1, true);
    w.TypeText(1, L"9");
    Eq(L"typing over the selected right value", L"9", RT(w));
    Eq(L"the left follows the right", L"09:00", LT(w));
    Eq(L"caption follows the right", L"9 h 0 min", w.ConverterCaption());
}

void WindowConverterState() {
    Win wp;
    HcWindow& w = *wp;
    auto expectActive = [&](const wchar_t* step, int field) {
        Eq(std::wstring(step) + L": active field", I(field), I(w.ActiveField()));
        True(std::wstring(step) + L": ':' enabled only for the left field", w.KeyEnabled(L":") == (field == 0));
        True(std::wstring(step) + L": decimal key enabled only for the right field", w.KeyEnabled(L",") == (field == 1));
        True(std::wstring(step) + L": underline left", w.UnderlineVisible(0) == (field == 0));
        True(std::wstring(step) + L": underline right", w.UnderlineVisible(1) == (field == 1));
        // the underline is a style class of the value box
        GtkWidget* box0 = gtk_widget_get_parent(w.FieldWidget(0));
        GtkWidget* box1 = gtk_widget_get_parent(w.FieldWidget(1));
        True(std::wstring(step) + L": underline class left", (gtk_widget_has_css_class(box0, "hc-active") != FALSE) == (field == 0));
        True(std::wstring(step) + L": underline class right", (gtk_widget_has_css_class(box1, "hc-active") != FALSE) == (field == 1));
    };
    expectActive(L"start", 0);
    w.SetConverterText(1, S(L"8,5"));
    expectActive(L"after typing in the right field", 1);
    w.Press(L"CE");
    expectActive(L"after CE", 1);
    w.Press(L"5");
    w.Press(L"BACK");
    expectActive(L"after BACK", 1);
    w.Press(L"CONVERT");
    expectActive(L"after Convert", 1);
    w.SelectTab(1);
    w.Press(L"7");
    w.SelectTab(0);
    expectActive(L"after a tab round trip", 1);
    w.Press(L"RESET");
    expectActive(L"after Reset", 0);
    w.GotFocus(1, true);
    expectActive(L"after the right field got focus", 1);
    w.GotFocus(0, true);
    expectActive(L"after the left field got focus", 0);

    w.Press(L"RESET");
    w.Press(L",");
    Eq(L"',' ignored while left is active", L"", LT(w));
    w.Press(L"8");
    w.Press(L":");
    w.Press(L":");
    w.Press(L"3");
    Eq(L"keypad left", L"8:3", LT(w));
    w.Press(L"RESET");
    w.GotFocus(1, true);
    w.Press(L":");
    Eq(L"':' ignored while right is active", L"", RT(w));
    w.Press(L",");
    w.Press(L"5");
    Eq(L"keypad right: lone ',' then 5", S(L",5"), RT(w));
    Eq(L"keypad right: left follows", L"00:30", LT(w));
    w.Press(L",");
    Eq(L"second ',' ignored", S(L",5"), RT(w));
    for (int i = 0; i < 8; i++) w.Press(L"7");
    Eq(L"keypad right stops at 6 decimals", S(L",577777"), RT(w));
    w.Press(L"RESET");
    for (int i = 0; i < 9; i++) w.Press(L"9");
    Eq(L"keypad left stops at 7 hour digits", L"9999999", LT(w));

    w.Press(L"RESET");
    w.SetConverterText(0, L"8:30");
    w.GotFocus(1, true);
    Eq(L"focus selects all of the right field", I((long long)RT(w).size()), I(w.GetField(1).selLength()));
    w.Press(L"9");
    Eq(L"keypad digit replaces the selected value", L"9", RT(w));
    Eq(L"and the left follows", L"09:00", LT(w));
    w.GotFocus(0, true);
    Eq(L"focus selects all of the left field", I((long long)LT(w).size()), I(w.GetField(0).selLength()));
    w.Press(L"BACK");
    Eq(L"BACK deletes a selection", L"", LT(w));
    Eq(L"BACK with the left selection clears the right", L"", RT(w));

    w.SelectTab(1);
    Keys(w, L"7:30/0=");
    w.SelectTab(0);
    w.Press(L"RESET");
    True(L"converter ':' key unaffected by a calculator error", w.KeyEnabled(L":"));
    w.SelectTab(1);
    True(L"calculator '+' disabled in the error state", !w.KeyEnabled(L"+"));
    True(L"calculator ':' disabled in the error state", !w.KeyEnabled(L":"));
    True(L"calculator ',' disabled in the error state", !w.KeyEnabled(L","));
    True(L"calculator BACK disabled in the error state", !w.KeyEnabled(L"BACK"));
    True(L"calculator digit enabled in the error state", w.KeyEnabled(L"5"));
    True(L"calculator CE enabled in the error state", w.KeyEnabled(L"CE"));
    True(L"calculator C enabled in the error state", w.KeyEnabled(L"C"));
    w.Press(L"C");
    for (const wchar_t* k : { L"+", L"-", L"*", L"/", L"=", L"BACK", L":", L",", L"5", L"CE", L"C" })
        True(std::wstring(L"calculator ") + k + L" enabled after C", w.KeyEnabled(k));
}

// A field selects all its text when it gets focus by click or Tab. Focus that arrives from outside the window
// (it was activated again after Alt+Tab, or a keypad key was clicked in an inactive window) must keep the caret,
// or the next key replaces the value.
void WindowFocusSelection() {
    Win wp;
    HcWindow& w = *wp;
    w.SetWindowActive(true);
    w.SetConverterText(0, L"8:3");
    w.GotFocus(0, true);
    w.SetCaret(0, 3, 3);
    w.GotFocus(0, false);
    Eq(L"activation keeps the caret (nothing selected)", L"0", I(w.GetField(0).selLength()));
    Eq(L"... and the caret", L"3", I(w.GetField(0).caret));
    w.Press(L"0");
    Eq(L"... so a keypad digit continues the typed text", L"8:30", LT(w));
    Eq(L"... and the other field follows", S(L"8,50"), RT(w));

    w.GotFocus(1, false);
    Eq(L"activation of the right field keeps the caret", L"0", I(w.GetField(1).selLength()));

    w.GotFocus(0, true);
    Eq(L"Tab from the other field selects all", I((long long)LT(w).size()), I(w.GetField(0).selLength()));
    w.Press(L"9");
    Eq(L"... so the next key replaces the value", L"9", LT(w));

    w.SetConverterText(0, L"8:30");
    w.GotFocus(0, false);
    w.SetCaret(0, 2, 2);
    w.MouseDownOnField(1);
    Eq(L"a click on the other field selects all of it", I((long long)RT(w).size()), I(w.GetField(1).selLength()));
    Eq(L"... the right field is active", L"1", I(w.ActiveField()));
    w.Press(L"7");
    Eq(L"... and the next key replaces it", L"7", RT(w));
    Eq(L"... the left field follows", L"07:00", LT(w));

    // A click on the field that has the focus places the caret (the GtkText does that): the window leaves it alone.
    w.MouseDownOnField(1);
    w.SetCaret(1, 1, 1);
    w.MouseDownOnField(1);
    Eq(L"a click on the focused field does not select all", L"0", I(w.GetField(1).selLength()));

    // The window was deactivated and activated again: the caret stays, nothing is selected.
    w.SetConverterText(0, L"8:30");
    w.GotFocus(0, false);
    w.SetCaret(0, 4, 4);
    w.SetWindowActive(false);
    w.SetWindowActive(true);
    Eq(L"re-activating the window keeps the selection state", L"0", I(w.GetField(0).selLength()));
    Eq(L"re-activating the window keeps the caret", L"4", I(w.GetField(0).caret));

    // Inactive window: a click on the field that holds the focus selects all (the click activates the window).
    w.SetWindowActive(false);
    True(L"inactive window: the press on a field is used (select all)", w.OnFieldPressed(0, 1, 1, 5, 5));
    Eq(L"inactive window: everything selected", I((long long)LT(w).size()), I(w.GetField(0).selLength()));
}

// The pointer on a converter field: SPEC 4.
void PointerOnFields() {
    Win wp(true);
    HcWindow& w = *wp;
    w.SetWindowActive(true);
    w.SetConverterText(0, L"8:30");
    w.GotFocus(1, false);
    True(L"press on the unfocused field is used", w.OnFieldPressed(0, 1, 1, 10, 10));
    Eq(L"... it selects everything", L"4", I(w.GetField(0).selLength()));
    Eq(L"... and focuses the field", L"0", I(w.FocusField()));
    True(L"a second plain press is left to the GtkText (caret)", !w.OnFieldPressed(0, 1, 1, 10, 10));
    True(L"a double click is used", w.OnFieldPressed(0, 2, 1, 10, 10));
    Eq(L"... and selects everything", L"4", I(w.GetField(0).selLength()));
    True(L"a triple click is used", w.OnFieldPressed(0, 3, 1, 10, 10));

    // The right click: Cut / Copy / Paste.
    w.SetCaret(0, 2, 2);
    GtkWidget* pop = w.OpenFieldMenu(0, 10, 10);
    True(L"the field menu opens", pop != nullptr);
    WaitUntil([&] { return gtk_widget_get_mapped(pop) != FALSE; }, 2000);
    True(L"the field menu is shown", gtk_widget_get_mapped(pop) != FALSE);
    True(L"a menu has the keyboard", w.Overlay());
    GActionMap* map = G_ACTION_MAP(w.Window());
    auto enabled = [&](const char* n) { return g_action_get_enabled(g_action_map_lookup_action(map, n)) != FALSE; };
    True(L"Cut is disabled without a selection", !enabled("field-cut"));
    True(L"Copy is disabled without a selection", !enabled("field-copy"));
    w.CloseContextMenus();
    WaitUntil([&] { return gtk_widget_get_mapped(pop) == FALSE; }, 2000);
    w.SetCaret(0, 4, 0);
    w.OpenFieldMenu(0, 10, 10);
    True(L"Cut is enabled with a selection", enabled("field-cut"));
    True(L"Copy is enabled with a selection", enabled("field-copy"));
    w.CloseContextMenus();
    WaitUntil([&] { return !w.Overlay(); }, 2000);
    True(L"closed menus give the keyboard back", !w.Overlay());
    True(L"a right press is used", w.OnFieldPressed(1, 1, 3, 10, 10));
    Eq(L"... it makes the field the active one", L"1", I(w.ActiveField()));
    Eq(L"... and selects its text", I((long long)RT(w).size()), I(w.GetField(1).selLength()));
    WaitUntil([&] { return w.Overlay(); }, 2000);
    True(L"... and opens the menu", w.Overlay());
    w.CloseContextMenus();
    WaitUntil([&] { return !w.Overlay(); }, 2000);
    WaitUntil([&] { return w.FocusField() == 1; }, 1000);
    Eq(L"... the field has the focus again when the menu is closed", L"1", I(w.FocusField()));
}

void WindowConvertKeepsValue() {
    for (const wchar_t* input : { L"8,058", L"0,0083", L"8,0083", L"0,996", L"8,335", L"12,3456", L"1,5", L"0,125", L"9,994" }) {
        Win wp;
        HcWindow& w = *wp;
        w.SetConverterText(1, S(input));
        std::wstring leftLive = LT(w), capLive = w.ConverterCaption();
        w.Press(L"CONVERT");
        Eq(std::wstring(L"Convert on right <") + S(input) + L"> keeps the left (" + leftLive + L")", leftLive, LT(w));
        Eq(std::wstring(L"Convert on right <") + S(input) + L"> keeps the caption", capLive, w.ConverterCaption());
        Eq(std::wstring(L"Convert on right <") + S(input) + L"> shows two decimals", conv::Normalize(conv::Right, S(input)), RT(w));
    }
    for (const wchar_t* input : { L"8:3", L"830", L"0:90", L"8", L"8u30", L"899" }) {
        Win wp;
        HcWindow& w = *wp;
        w.SetConverterText(0, input);
        std::wstring rightLive = RT(w);
        w.Press(L"CONVERT");
        Eq(std::wstring(L"Convert on left <") + input + L"> keeps the right", rightLive, RT(w));
    }
    {
        Win wp;
        HcWindow& w = *wp;
        w.SetConverterText(1, S(L"9999999,999"));
        Eq(L"right 9999999,999 gives left", L"10000000:00", LT(w));
        w.Press(L"CONVERT");
        Eq(L"Convert right 9999999,999: right", S(L"10000000,00"), RT(w));
        Eq(L"Convert right 9999999,999: left", L"10000000:00", LT(w));
    }
    {
        Win wp;
        HcWindow& w = *wp;
        w.SetConverterText(0, L"9999999:60");
        Eq(L"left 9999999:60 gives right", S(L"10000000,00"), RT(w));
        w.Press(L"CONVERT");
        Eq(L"Convert left 9999999:60: left", L"10000000:00", LT(w));
        Eq(L"Convert left 9999999:60: right", S(L"10000000,00"), RT(w));
    }
    {
        Win wp;
        HcWindow& w = *wp;
        w.SetConverterText(0, L"9999999:99");
        std::wstring rightShown = RT(w);
        w.GotFocus(1, true);
        w.Press(L"CONVERT");
        Eq(L"Convert on an untouched 8 digit right value keeps it", rightShown, RT(w));
        Eq(L"and the left is that same value (10000000:39)", L"10000000:39", LT(w));
    }
    {
        Win wp;
        HcWindow& w = *wp;
        w.SetConverterText(1, S(L"8,5"));
        w.Press(L"CE");
        Eq(L"CE clears right", L"", RT(w));
        Eq(L"CE clears left", L"", LT(w));
        Eq(L"CE clears caption", L"", w.ConverterCaption());
        w.Press(L"CONVERT");
        Eq(L"Convert on empty fields: left", L"", LT(w));
        Eq(L"Convert on empty fields: right", L"", RT(w));
        w.SetConverterText(0, L":");
        w.Press(L"CONVERT");
        Eq(L"Convert on a lone separator clears the field", L"", LT(w));
    }
}

const wchar_t* SpecKeyForChar(wchar_t c) {
    static wchar_t buf[2];
    if (c >= L'0' && c <= L'9') { buf[0] = c; buf[1] = 0; return buf; }
    if (c == L':' || c == L'u' || c == L'U' || c == L'h' || c == L'H') return L":";
    if (c == L',' || c == L'.') return L",";
    if (c == L'+' || c == L'-' || c == L'*' || c == L'/' || c == L'=') { buf[0] = c; buf[1] = 0; return buf; }
    return nullptr;
}

std::wstring State(hc::CalcEngine& e) { return e.MainText() + L"|" + e.Expression() + L"|" + e.DecimalText(); }

void KeyboardMapping() {
    Win wp;
    HcWindow& w = *wp;
    w.SelectTab(1);
    std::vector<wchar_t> chars;
    for (wchar_t c = L' '; c <= L'~'; c++) chars.push_back(c);
    for (wchar_t c : { (wchar_t)0x00E9, (wchar_t)0x00E8, (wchar_t)0x00E7, (wchar_t)0x00E0, (wchar_t)0x00A7, (wchar_t)0x00B0, (wchar_t)0x00B2, (wchar_t)0x00B5,
                       (wchar_t)0x20AC, (wchar_t)0x00D7, (wchar_t)0x00F7, (wchar_t)0x2212, (wchar_t)'\t', (wchar_t)'\r', (wchar_t)0x001B })
        chars.push_back(c);
    int bad = 0;
    std::wstring first;
    for (wchar_t c : chars) {
        w.Press(L"C");
        RaiseText(w, std::wstring(L"7") + c + L"3");
        hc::CalcEngine e;
        e.Press(L"7");
        const wchar_t* k = SpecKeyForChar(c);
        if (k) e.Press(k);
        e.Press(L"3");
        std::wstring a = State(w.Engine()), x = State(e);
        if (a != x) {
            if (!bad) first = L"U+" + I(c) + L" gave " + a + L", expected " + x;
            bad++;
        }
    }
    Eq(L"calculator text mapping for " + I((long long)chars.size()) + L" characters (bad count; first: " + first + L")", L"0", I(bad));

    w.Press(L"C");
    RaiseText(w, L"7u30+7h20=");
    Eq(L"one text with a whole expression", L"14:50", w.Engine().MainText());
    Eq(L"... and its expression line", L"07:30 + 07:20 =", w.Engine().Expression());
    w.Press(L"C");
    RaiseText(w, L"7,5+7.5=");
    Eq(L"comma and dot are both the decimal separator", L"15:00", w.Engine().MainText());

    w.Press(L"C");
    RaiseText(w, L"7:30+1");
    RaiseKey(w, GDK_KEY_Return);
    Eq(L"Enter is equals", L"08:30", w.Engine().MainText());
    RaiseKey(w, GDK_KEY_Return);
    Eq(L"Enter again repeats", L"09:30", w.Engine().MainText());
    RaiseKey(w, GDK_KEY_BackSpace);
    Eq(L"Backspace after a result does nothing", L"09:30", w.Engine().MainText());
    RaiseKey(w, GDK_KEY_Delete);
    Eq(L"Delete is CE (a result resets)", L"00:00", w.Engine().MainText());
    RaiseText(w, L"1:3");
    RaiseKey(w, GDK_KEY_BackSpace);
    Eq(L"Backspace edits the entry", L"1:", w.Engine().MainText());
    RaiseKey(w, GDK_KEY_Delete);
    Eq(L"Delete clears the entry", L"0", w.Engine().MainText());
    RaiseText(w, L"5+5");
    RaiseKey(w, GDK_KEY_Escape);
    Eq(L"Escape is C", L"00:00", w.Engine().MainText());
    Eq(L"Escape clears the expression", L"", w.Engine().Expression());
    RaiseText(w, L"7:30/0");
    RaiseKey(w, GDK_KEY_Return);
    Eq(L"Enter on x / 0 shows the error", hc::CalcEngine::DivideByZeroText, w.Engine().MainText());
    RaiseKey(w, GDK_KEY_Return);
    Eq(L"Enter in the error state does nothing", hc::CalcEngine::DivideByZeroText, w.Engine().MainText());
    RaiseKey(w, GDK_KEY_BackSpace);
    Eq(L"Backspace in the error state does nothing", hc::CalcEngine::DivideByZeroText, w.Engine().MainText());
    RaiseText(w, L"+");
    Eq(L"'+' in the error state does nothing", hc::CalcEngine::DivideByZeroText, w.Engine().MainText());
    RaiseText(w, L":");
    Eq(L"':' in the error state does nothing", hc::CalcEngine::DivideByZeroText, w.Engine().MainText());
    RaiseText(w, L"2");
    Eq(L"a digit leaves the error state", L"2", w.Engine().MainText());

    Eq(L"calculator keystrokes did not touch the converter", L"", LT(w));
    w.SelectTab(0);
    w.Press(L"C");
    std::wstring before = State(w.Engine());
    NoFocus(w);
    RaiseText(w, L"7:30+7:20");
    Eq(L"converter keystrokes did not touch the calculator", before, State(w.Engine()));
    Eq(L"converter typed text (the + is filtered, a third minute digit and a second ':' are rejected)", L"7:30", LT(w));
    RaiseKey(w, GDK_KEY_Return);
    Eq(L"converter Enter converts", L"07:30", LT(w));
    RaiseKey(w, GDK_KEY_Escape);
    Eq(L"converter Esc resets", L"", LT(w));
    Eq(L"converter Esc keeps the calculator", before, State(w.Engine()));

    Win wq;
    HcWindow& v = *wq;
    NoFocus(v);
    RaiseText(v, L"8u30");
    Eq(L"unfocused text to the left field", L"8:30", LT(v));
    v.SetConverterText(1, L"");
    NoFocus(v);
    RaiseText(v, L"8:30");
    Eq(L"unfocused ':' is ignored on the right", L"830", RT(v));
    v.Press(L"RESET");
    v.SetConverterText(1, L"");
    NoFocus(v);
    RaiseText(v, L"8.5");
    Eq(L"unfocused '.' on the right", S(L"8,5"), RT(v));
    Eq(L"unfocused '.' on the right: left", L"08:30", LT(v));
}

// ------------------------------------------------------------------ the calculator display

void CalcMirror() {
    Win wp;
    HcWindow& w = *wp;
    w.SelectTab(1);
    Eq(L"idle left display", L"00:00", w.CalcMain());
    Eq(L"idle right display", S(L"0,00"), w.CalcDecimal());
    Eq(L"idle caption", L"", w.CalcCaption());
    Eq(L"idle expression", L"", w.ExpressionShown());

    const wchar_t* pool[] = { L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L":", L",", L"+", L"-", L"*", L"/", L"=", L"=", L"CE", L"C", L"BACK" };
    const int poolSize = (int)(sizeof pool / sizeof pool[0]);
    unsigned seed = 31;
    auto next = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7FFF; };
    int bad = 0;
    std::wstring first;
    for (int n = 0; n < 500; n++) {
        w.Press(L"C");
        int len = 1 + (int)(next() % 13);
        std::wstring log;
        for (int i = 0; i < len; i++) {
            const wchar_t* k = pool[next() % (unsigned)poolSize];
            log += std::wstring(k) + L" ";
            w.Press(k);
            hc::CalcEngine& e = w.Engine();
            bool err = e.IsError();
            bool ok = w.CalcMain() == e.MainText() && w.CalcDecimal() == e.DecimalText() && w.CalcCaption() == e.Caption();
            ok = ok && (w.CalcRightVisible() == !err);
            ok = ok && FromUtf8(gtk_label_get_text(GTK_LABEL(w.CalcValueLabel(0)))) == e.MainText();
            ok = ok && FromUtf8(gtk_label_get_text(GTK_LABEL(w.CalcValueLabel(1)))) == e.DecimalText();
            ok = ok && FromUtf8(gtk_label_get_text(GTK_LABEL(w.CalcCaptionLabel()))) == e.Caption();
            ok = ok && (gtk_widget_get_visible(gtk_widget_get_parent(w.CalcValueLabel(1))) == (err ? FALSE : TRUE));
            for (const wchar_t* key : { L"0", L"1", L"5", L"9", L":", L",", L"+", L"-", L"*", L"/", L"=", L"CE", L"C", L"BACK" }) ok = ok && (w.KeyEnabled(key) == e.IsEnabled(key));
            std::wstring shown = w.ExpressionShown();
            std::wstring full = e.Expression();
            auto rep = [&](const wchar_t* a, const wchar_t* b) {
                size_t pos = 0;
                while ((pos = full.find(a, pos)) != std::wstring::npos) { full.replace(pos, wcslen(a), b); pos += wcslen(b); }
            };
            rep(L" * ", L" \x00D7 ");
            rep(L" / ", L" \x00F7 ");
            rep(L" - ", L" \x2212 ");
            ok = ok && (shown == full || (shown.compare(0, 2, L"\x2026 ") == 0 && shown.size() > 2 && full.size() >= shown.size() - 2 &&
                                          full.compare(full.size() - (shown.size() - 2), shown.size() - 2, shown.substr(2)) == 0));
            ok = ok && FromUtf8(gtk_label_get_text(GTK_LABEL(w.ExpressionLabel()))) == shown;
            if (!ok) {
                if (!bad) first = log + L"=> UI <" + w.CalcMain() + L"|" + w.CalcDecimal() + L"|" + w.CalcCaption() + L"|" + shown + L">, engine <" + e.MainText() + L"|" + e.DecimalText() + L"|" + e.Caption() + L"|" + e.Expression() + L">";
                bad++;
            }
        }
    }
    Eq(L"window display always mirrors the engine (bad count; first: " + first + L")", L"0", I(bad));

    w.Press(L"C");
    Keys(w, L"7:30+7:20=");
    Eq(L"copy left", L"14:50", w.Engine().CopyMainText());
    Eq(L"copy right", S(L"14,83"), w.Engine().CopyDecimalText());
}

void ExpressionLine() {
    Win wp;
    HcWindow& w = *wp;
    w.SelectTab(1);
    Keys(w, L"7:30*2/4-1=");
    Eq(L"operators are shown as x / division sign / minus sign", L"07:30 \x00D7 2 \x00F7 4 \x2212 01:00 =", w.ExpressionShown());
    w.Press(L"C");
    Eq(L"C clears the expression line", L"", w.ExpressionShown());
    Keys(w, L"7:30*1:30=");
    Eq(L"factor shown as a decimal", S(L"07:30 \x00D7 1,5 ="), w.ExpressionShown());
    w.Press(L"C");
    Keys(w, L"7:30/0=");
    Eq(L"error keeps the failing operation", L"07:30 \x00F7 0 =", w.ExpressionShown());
    w.Press(L"C");
}

// ------------------------------------------------------------------ the menu, theme, settings

std::string ItemString(GMenuModel* m, int i, const char* attr) {
    char* s = nullptr;
    std::string r;
    if (g_menu_model_get_item_attribute(m, i, attr, "s", &s)) {
        r = s;
        g_free(s);
    } else {
        r = "<none>";
    }
    return r;
}

void MenuStructure() {
    Win wp;
    HcWindow& w = *wp;
    GMenuModel* m = w.MenuModel();
    Eq(L"the primary menu has three sections", L"3", I(g_menu_model_get_n_items(m)));
    struct Section { const char* label; std::vector<std::pair<const char*, const char*>> items; };
    std::vector<Section> want = {
        { "Theme", { { "Auto (Follow System)", "win.theme" }, { "Light", "win.theme" }, { "Dark", "win.theme" } } },
        { "Decimal Places", { { "2 (Default)", "win.places" }, { "3", "win.places" }, { "4", "win.places" }, { "5", "win.places" }, { "6", "win.places" } } },
        { "<none>", { { "About HourCalc", "win.about" } } },
    };
    for (int s = 0; s < 3 && s < (int)g_menu_model_get_n_items(m); s++) {
        Eq(L"section " + I(s) + L" label", FromUtf8(want[(size_t)s].label), FromUtf8(ItemString(m, s, "label")));
        GMenuModel* sec = g_menu_model_get_item_link(m, s, G_MENU_LINK_SECTION);
        True(L"section " + I(s) + L" is a section", sec != nullptr);
        if (!sec) continue;
        Eq(L"section " + I(s) + L" size", I((long long)want[(size_t)s].items.size()), I(g_menu_model_get_n_items(sec)));
        for (int i = 0; i < (int)want[(size_t)s].items.size() && i < (int)g_menu_model_get_n_items(sec); i++) {
            Eq(L"item label " + I(s) + L"." + I(i), FromUtf8(want[(size_t)s].items[(size_t)i].first), FromUtf8(ItemString(sec, i, "label")));
            Eq(L"item action " + I(s) + L"." + I(i), FromUtf8(want[(size_t)s].items[(size_t)i].second), FromUtf8(ItemString(sec, i, "action")));
        }
        g_object_unref(sec);
    }
    GMenuModel* themes = g_menu_model_get_item_link(m, 0, G_MENU_LINK_SECTION);
    const char* targets[] = { "auto", "light", "dark" };
    for (int i = 0; i < 3 && themes; i++) {
        GVariant* t = g_menu_model_get_item_attribute_value(themes, i, "target", nullptr);
        Eq(L"theme target " + I(i), FromUtf8(targets[i]), t ? FromUtf8(g_variant_get_string(t, nullptr)) : L"<none>");
        if (t) g_variant_unref(t);
    }
    if (themes) g_object_unref(themes);
    GMenuModel* places = g_menu_model_get_item_link(m, 1, G_MENU_LINK_SECTION);
    for (int i = 0; i < 5 && places; i++) {
        GVariant* t = g_menu_model_get_item_attribute_value(places, i, "target", nullptr);
        Eq(L"places target " + I(i), I(2 + i), t ? I(g_variant_get_int32(t)) : L"<none>");
        if (t) g_variant_unref(t);
    }
    if (places) g_object_unref(places);
    True(L"every action of the menu exists in the window",
         [&] {
             GActionGroup* g = G_ACTION_GROUP(w.Window());
             return g_action_group_has_action(g, "theme") && g_action_group_has_action(g, "places") && g_action_group_has_action(g, "about");
         }());
}

std::wstring ThemeState(HcWindow& w) {
    GVariant* v = g_action_group_get_action_state(G_ACTION_GROUP(w.Window()), "theme");
    std::wstring s = v ? FromUtf8(g_variant_get_string(v, nullptr)) : L"<none>";
    if (v) g_variant_unref(v);
    return s;
}

int PlacesState(HcWindow& w) {
    GVariant* v = g_action_group_get_action_state(G_ACTION_GROUP(w.Window()), "places");
    int p = v ? g_variant_get_int32(v) : -1;
    if (v) g_variant_unref(v);
    return p;
}

void ThemeSwitching() {
    Win wp(true);
    HcWindow& w = *wp;
    AdwStyleManager* sm = adw_style_manager_get_default();
    Eq(L"the theme starts at Auto", L"auto", ThemeState(w));
    True(L"Auto is the default scheme", adw_style_manager_get_color_scheme(sm) == ADW_COLOR_SCHEME_DEFAULT);
    True(L"Auto follows the system scheme", (adw_style_manager_get_dark(sm) != FALSE) == g_systemDark);
    w.SetThemeMode(ThemeMode::Dark);
    True(L"Dark forces the dark scheme", adw_style_manager_get_color_scheme(sm) == ADW_COLOR_SCHEME_FORCE_DARK);
    True(L"Dark is dark", adw_style_manager_get_dark(sm) != FALSE);
    Eq(L"Dark in the menu state", L"dark", ThemeState(w));
    w.SetThemeMode(ThemeMode::Light);
    True(L"Light forces the light scheme", adw_style_manager_get_color_scheme(sm) == ADW_COLOR_SCHEME_FORCE_LIGHT);
    True(L"Light is light, whatever the system says", adw_style_manager_get_dark(sm) == FALSE);
    Eq(L"Light in the menu state", L"light", ThemeState(w));
    w.SetThemeMode(ThemeMode::Auto);
    True(L"Auto again is the default scheme", adw_style_manager_get_color_scheme(sm) == ADW_COLOR_SCHEME_DEFAULT);
    True(L"Auto again follows the system", (adw_style_manager_get_dark(sm) != FALSE) == g_systemDark);

    // Through the action, as the menu items activate it.
    GActionGroup* g = G_ACTION_GROUP(w.Window());
    g_action_group_activate_action(g, "theme", g_variant_new_string("dark"));
    Eq(L"menu: Dark", L"2", I((int)w.ThemeChoice()));
    True(L"menu: Dark is dark", adw_style_manager_get_dark(sm) != FALSE);
    g_action_group_activate_action(g, "theme", g_variant_new_string("light"));
    Eq(L"menu: Light", L"1", I((int)w.ThemeChoice()));
    True(L"menu: Light is light", adw_style_manager_get_dark(sm) == FALSE);
    g_action_group_activate_action(g, "theme", g_variant_new_string("auto"));
    Eq(L"menu: Auto", L"0", I((int)w.ThemeChoice()));

    w.SetThemeMode(ThemeMode::Dark);
    w.WaitFrames(2);
    w.SetThemeMode(ThemeMode::Auto);
}

void DecimalPlacesMenu() {
    Win wp;
    HcWindow& w = *wp;
    Eq(L"decimal places start at 2", L"2", I(w.DecimalPlaces()));
    Eq(L"menu state starts at 2", L"2", I(PlacesState(w)));
    // Both tabs are re-formatted at once, from the full value.
    w.SetConverterText(0, L"08:20");
    Eq(L"08:20 right, 2 places", S(L"8,33"), RT(w));
    w.SelectTab(1);
    Keys(w, L"7:30/4=");
    Eq(L"7:30 / 4, 2 places", S(L"1,88"), w.CalcDecimal());
    GActionGroup* g = G_ACTION_GROUP(w.Window());
    g_action_group_activate_action(g, "places", g_variant_new_int32(4));
    Eq(L"menu: 4 places", L"4", I(w.DecimalPlaces()));
    Eq(L"menu state: 4", L"4", I(PlacesState(w)));
    Eq(L"the calculator re-formats at once", S(L"1,8750"), w.CalcDecimal());
    Eq(L"... also in its label", S(L"1,8750"), FromUtf8(gtk_label_get_text(GTK_LABEL(w.CalcValueLabel(1)))));
    Eq(L"... and the engine keeps it for new results", L"4", I(w.Engine().DecimalPlaces()));
    w.SelectTab(0);
    Eq(L"the converter re-formats at once", S(L"8,3333"), RT(w));
    Eq(L"... the typed side is left alone", L"08:20", LT(w));
    w.SetDecimalPlaces(9);
    Eq(L"an invalid place count falls back to 2", L"2", I(w.DecimalPlaces()));
    w.SetDecimalPlaces(2);
}

std::string ReadFile(const std::string& path) {
    gchar* c = nullptr;
    gsize n = 0;
    std::string r = "<missing>";
    if (g_file_get_contents(path.c_str(), &c, &n, nullptr)) {
        r.assign(c, n);
        g_free(c);
    }
    return r;
}

std::wstring NoCr(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return FromUtf8(s);
}

void SettingsPersistence() {
    char* tmp = g_dir_make_tmp("hcgtk-selftest-XXXXXX", nullptr);
    if (!tmp) {
        Fail(L"settings", L"cannot make a temporary folder");
        return;
    }
    std::string dir = tmp;
    g_free(tmp);
    setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
    std::wstring wpath = hc::settings::DefaultPath();
    std::string path = ToUtf8(wpath);
    Eq(L"the settings file is $XDG_CONFIG_HOME/hourcalc/hourcalc.ini", FromUtf8(dir + "/hourcalc/hourcalc.ini"), wpath);
    AdwStyleManager* sm = adw_style_manager_get_default();
    {
        Win a(false, wpath);
        Eq(L"no file is written at start", L"<missing>", FromUtf8(ReadFile(path)));
        a->SetThemeMode(ThemeMode::Dark);
        Eq(L"the write waits for an idle moment (the change is on screen first)", L"<missing>", FromUtf8(ReadFile(path)));
        Pump();
        Eq(L"the theme is saved", L"DecimalPlaces=2\nTheme=dark\n", NoCr(ReadFile(path)));
        a->SetDecimalPlaces(4);
        a->FlushSettings();
        Eq(L"the places are saved and the theme is kept", L"DecimalPlaces=4\nTheme=dark\n", NoCr(ReadFile(path)));
        int saves = a->SavesCount();
        a->SetThemeMode(ThemeMode::Dark);
        a->SetDecimalPlaces(4);
        a->FlushSettings();
        Eq(L"nothing is written when nothing changed", I(saves), I(a->SavesCount()));
        a->SetThemeMode(ThemeMode::Light);
        a->FlushSettings();
        Eq(L"light is saved", L"DecimalPlaces=4\nTheme=light\n", NoCr(ReadFile(path)));
    }
    {
        Win b(false, wpath);
        Eq(L"a new window reads the places", L"4", I(b->DecimalPlaces()));
        Eq(L"a new window reads the theme", L"1", I((int)b->ThemeChoice()));
        True(L"... and applies it", adw_style_manager_get_dark(sm) == FALSE && adw_style_manager_get_color_scheme(sm) == ADW_COLOR_SCHEME_FORCE_LIGHT);
        Eq(L"... and shows it in the menu", L"light", ThemeState(*b));
        Eq(L"... and the places in the menu", L"4", I(PlacesState(*b)));
        b->SetConverterText(0, L"08:20");
        Eq(L"... and uses the places", S(L"8,3333"), RT(*b));
        b->SetThemeMode(ThemeMode::Auto);
        // the window is closed with the write still pending: it is flushed on close
    }
    Eq(L"a pending write is flushed when the window closes", L"DecimalPlaces=4\nTheme=auto\n", NoCr(ReadFile(path)));
    {
        // A second running HourCalc: a change of one setting keeps the other as it is on disk.
        Win a(false, wpath), b(false, wpath);
        a->SetThemeMode(ThemeMode::Dark);
        a->FlushSettings();
        b->SetDecimalPlaces(6);
        b->FlushSettings();
        Eq(L"a change keeps what the other window saved", L"DecimalPlaces=6\nTheme=dark\n", NoCr(ReadFile(path)));
        a->SetDecimalPlaces(3);
        a->FlushSettings();
        Eq(L"... in the other direction too", L"DecimalPlaces=3\nTheme=dark\n", NoCr(ReadFile(path)));
    }
    {
        // Invalid values mean the defaults; Theme is case-insensitive.
        std::ofstream(path, std::ios::binary | std::ios::trunc) << "DecimalPlaces=17\r\nTheme=bogus\r\n";
        Win c(false, wpath);
        Eq(L"an invalid place count reads as 2", L"2", I(c->DecimalPlaces()));
        Eq(L"an invalid theme reads as Auto", L"0", I((int)c->ThemeChoice()));
    }
    {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << "theme=DARK\r\ndecimalplaces=5\r\n";
        Win c(false, wpath);
        Eq(L"keys and values in any case: places", L"5", I(c->DecimalPlaces()));
        Eq(L"keys and values in any case: theme", L"2", I((int)c->ThemeChoice()));
    }
    {
        // A folder that cannot be written: the setting applies for this session only.
        std::string blocked = dir + "/blocked";
        std::ofstream(blocked, std::ios::binary | std::ios::trunc) << "x";
        Win d(false, FromUtf8(blocked + "/hourcalc.ini"));
        d->SetDecimalPlaces(5);
        d->FlushSettings();
        Eq(L"an unwritable folder: the setting holds for the session", L"5", I(d->DecimalPlaces()));
        d->SetConverterText(0, L"08:20");
        Eq(L"... and is used", S(L"8,33333"), RT(*d));
    }
    {
        // No settings path at all: nothing is read or written.
        Win e(false, L"");
        e->SetThemeMode(ThemeMode::Dark);
        e->FlushSettings();
        Eq(L"no path: no write", L"0", I(e->SavesCount()));
        e->SetThemeMode(ThemeMode::Auto);
    }
    adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_DEFAULT);         // the next tests start from the default scheme
    unsetenv("XDG_CONFIG_HOME");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ------------------------------------------------------------------ the dialogs and menus on screen

void AboutAndMenus() {
    Win wp(true);
    HcWindow& w = *wp;
    w.Press(L"RESET");
    w.SelectTab(1);
    Keys(w, L"7:30+");
    w.ShowAbout();
    WaitUntil([&] { return w.IsAboutOpen(); }, 2000);
    w.WaitFrames(2);
    True(L"About opens", w.IsAboutOpen());
    AdwAboutDialog* a = ADW_ABOUT_DIALOG(w.AboutDialog());
    Eq(L"About: name", L"HourCalc", FromUtf8(adw_about_dialog_get_application_name(a)));
    Eq(L"About: developer", L"Optimey CommV", FromUtf8(adw_about_dialog_get_developer_name(a)));
    Eq(L"About: copyright", L"Copyright (c) 2026 Optimey CommV", FromUtf8(adw_about_dialog_get_copyright(a)));
    True(L"About: GPL-3.0-or-later", adw_about_dialog_get_license_type(a) == GTK_LICENSE_GPL_3_0);
    Eq(L"About: version", FromUtf8(HC_VERSION), FromUtf8(adw_about_dialog_get_version(a)));
    Eq(L"About: website", FromUtf8(HC_SOURCE_URL), FromUtf8(adw_about_dialog_get_website(a)));
    Eq(L"About: icon", FromUtf8(HC_APP_ID), FromUtf8(adw_about_dialog_get_application_icon(a)));
    True(L"a dialog has the keyboard", w.Overlay());
    True(L"Esc does not reach the calculator behind the dialog", !w.HandleKey(GDK_KEY_Escape, (GdkModifierType)0, nullptr));
    Eq(L"... the calculator keeps its expression", L"07:30 +", w.Engine().Expression());
    w.CloseAbout();
    WaitUntil([&] { return !w.IsAboutOpen(); }, 2000);
    True(L"About closes", !w.IsAboutOpen());
    Pump();
    True(L"the keyboard is back", !w.Overlay());

    // The primary menu.
    w.OpenMenu();
    WaitUntil([&] { return w.IsMenuOpen(); }, 2000);
    w.WaitFrames(2);
    True(L"the primary menu opens", w.IsMenuOpen());
    True(L"... and has the keyboard", w.Overlay());
    GtkWidget* pop = w.MenuPopover();
    True(L"... its popover is mapped", gtk_widget_get_mapped(pop) != FALSE);
    True(L"Esc and digits do not reach the window behind the menu", !w.HandleKey(GDK_KEY_5, (GdkModifierType)0, nullptr));
    w.CloseMenu();
    WaitUntil([&] { return !w.IsMenuOpen(); }, 2000);
    Pump();
    True(L"the menu closes", !w.IsMenuOpen());
    True(L"... and the keyboard is back", !w.Overlay());
    Eq(L"... and the content has the focus again (no header button)", L"-1", I(w.FocusField()));
    True(L"the calculator has no focus widget", gtk_window_get_focus(w.Window()) == nullptr);
    Keys(w, L"1");
    Eq(L"typing continues after the menu", L"1", w.Engine().MainText());

    // The Copy menu of the calculator values.
    GtkWidget* cp = w.OpenCopyMenu(0, 10, 10);
    WaitUntil([&] { return gtk_widget_get_mapped(cp) != FALSE; }, 2000);
    True(L"the Copy menu opens", gtk_widget_get_mapped(cp) != FALSE);
    w.CloseContextMenus();
    WaitUntil([&] { return !w.Overlay(); }, 2000);
}

// ------------------------------------------------------------------ the clipboard

std::wstring ReadClipboard(HcWindow& w) {
    std::wstring out = L"<none>";
    bool done = false;
    struct Ctx { std::wstring* out; bool* done; } ctx{ &out, &done };
    gdk_clipboard_read_text_async(
        gtk_widget_get_clipboard(W(w)), nullptr,
        [](GObject* src, GAsyncResult* res, gpointer d) {
            Ctx* c = static_cast<Ctx*>(d);
            char* t = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(src), res, nullptr);
            *c->out = t ? FromUtf8(t) : L"<null>";
            g_free(t);
            *c->done = true;
        },
        &ctx);
    WaitUntil([&] { return done; }, 3000);
    return out;
}

void Clipboard() {
    Win wp(true);
    HcWindow& w = *wp;
    w.SelectTab(1);
    Keys(w, L"7:30+7:20=");
    w.CopyCalc(0);
    Eq(L"Copy: the h:mm value", L"14:50", ReadClipboard(w));
    w.CopyCalc(1);
    Eq(L"Copy: the decimal value", S(L"14,83"), ReadClipboard(w));
    // Ctrl+C / Ctrl+Shift+C through the key handling.
    w.HandleKey(GDK_KEY_c, GDK_CONTROL_MASK, nullptr);
    Eq(L"Ctrl+C copies the h:mm value", L"14:50", ReadClipboard(w));
    w.HandleKey(GDK_KEY_C, (GdkModifierType)(GDK_CONTROL_MASK | GDK_SHIFT_MASK), nullptr);
    Eq(L"Ctrl+Shift+C copies the decimal value", S(L"14,83"), ReadClipboard(w));
    // Paste into the calculator.
    w.Press(L"C");
    w.CopyCalc(0);
    gdk_clipboard_set_text(gtk_widget_get_clipboard(W(w)), "8:30");
    w.HandleKey(GDK_KEY_v, GDK_CONTROL_MASK, nullptr);
    WaitUntil([&] { return w.Engine().HasEntry(); }, 2000);
    Eq(L"Ctrl+V pastes an entry that parses", L"8:30", w.CalcMain());
    Keys(w, L"+0:30=");
    Eq(L"... and the entry is used", L"09:00", w.CalcMain());
    True(L"a text that does not parse is refused", !w.PasteTextIntoCalc(L"hello"));
    True(L"a decimal entry with a dot is taken", w.PasteTextIntoCalc(L"7.5"));
    Eq(L"... as 7,5 hours", S(L"7,5"), w.CalcMain());
    True(L"a leading separator gets a zero", w.PasteTextIntoCalc(L":30"));
    Eq(L"... 0:30", L"0:30", w.CalcMain());

    // A converter field: Copy, Cut and Paste.
    w.SelectTab(0);
    w.Press(L"RESET");
    w.SetConverterText(0, L"8:30");
    w.SetCaret(0, 4, 0);
    w.CopyField(0);
    Eq(L"field Copy", L"8:30", ReadClipboard(w));
    w.CutField(0);
    Eq(L"field Cut empties the field", L"", LT(w));
    Eq(L"... and the other field", L"", RT(w));
    Eq(L"... the clipboard has it", L"8:30", ReadClipboard(w));
    w.PasteField(1);
    WaitUntil([&] { return !RT(w).empty(); }, 2000);
    Eq(L"field Paste into the right field is filtered", L"830", RT(w));
    w.Press(L"RESET");
    gdk_clipboard_set_text(gtk_widget_get_clipboard(W(w)), "8.30");
    w.GotFocus(0, true);
    w.PasteField(0);
    WaitUntil([&] { return !LT(w).empty(); }, 2000);
    Eq(L"field Paste into the left field is filtered", L"8:30", LT(w));
    Eq(L"... and converted", S(L"8,50"), RT(w));
}

// ------------------------------------------------------------------ text scaling

// Large text (the desktop setting, 1.5 and 2 times): the captions under the values are not cut off (they wrap instead),
// every key and every text stays inside its window.
bool Ellipsized(GtkWidget* label);
void CheckInside(HcWindow& w, GtkWidget* widget, const std::wstring& what);
void CheckKeys(HcWindow& w, const std::vector<hcgtk::KeyDef>& keys, const std::wstring& tag);

void LargeText() {
    GtkSettings* settings = gtk_settings_get_default();
    int oldDpi = 0;
    g_object_get(settings, "gtk-xft-dpi", &oldDpi, nullptr);
    for (double scale : { 1.5, 2.0 }) {
        g_object_set(settings, "gtk-xft-dpi", (int)std::lround(96.0 * 1024.0 * scale), nullptr);
        std::wstring t = L"text x" + std::to_wstring((int)std::lround(scale * 100)) + L" %";
        Win wp(true);
        HcWindow& w = *wp;
        w.WaitFrames(3);
        const wchar_t* longs[] = { L"8:30", L"9999999:99", L"1234567:59" };
        for (const wchar_t* text : longs) {
            w.Press(L"RESET");
            w.SetConverterText(0, text);
            w.WaitFrames(3);
            True(t + L" converter <" + text + L">: the caption is not cut off", !Ellipsized(w.ConverterCaptionLabel()));
            CheckInside(w, w.ConverterCaptionLabel(), t + L" converter <" + text + L"> caption inside");
        }
        w.Press(L"RESET");
        CheckKeys(w, w.ConverterKeys(), t + L" converter");
        w.SelectTab(1);
        w.WaitFrames(3);
        Keys(w, L"1234567:59*7=");
        w.WaitFrames(3);
        True(t + L" calculator: the caption is not cut off", !Ellipsized(w.CalcCaptionLabel()));
        CheckInside(w, w.CalcCaptionLabel(), t + L" calculator caption inside");
        CheckKeys(w, w.CalculatorKeys(), t + L" calculator");
        w.Press(L"C");
    }
    g_object_set(settings, "gtk-xft-dpi", oldDpi, nullptr);
}

// ------------------------------------------------------------------ the keyboard and the pointer in the menus

// Opening and closing the primary menu or the About dialog leaves the caret and the selection of the field as they were
// (in the Windows version the menu never takes the focus, so nothing changes there).
void MenuKeepsCaret() {
    Win wp(true);
    HcWindow& w = *wp;
    w.SetConverterText(0, L"830");
    for (int what = 0; what < 4; ++what) {
        const bool selected = what >= 2;
        const bool about = what % 2 == 1;
        const std::wstring tag = std::wstring(selected ? L"selection" : L"caret") + (about ? L" (About)" : L" (menu)");
        if (selected) w.SetCaret(0, 3, 0);
        else w.SetCaret(0, 1, 1);
        FieldState before = w.GetField(0);
        if (about) {
            w.ShowAbout();
            WaitUntil([&] { return w.IsAboutOpen(); }, 2000);
            w.WaitFrames(2);
            w.CloseAbout();
            WaitUntil([&] { return !w.IsAboutOpen(); }, 2000);
        } else {
            w.OpenMenu();
            WaitUntil([&] { return w.IsMenuOpen(); }, 2000);
            w.WaitFrames(2);
            w.CloseMenu();
            WaitUntil([&] { return !w.IsMenuOpen(); }, 2000);
        }
        WaitUntil([&] { return w.FocusField() == 0; }, 1500);
        Pump();
        Eq(L"after the " + tag + L": the field has the focus again", L"0", I(w.FocusField()));
        FieldState after = w.GetField(0);
        Eq(L"after the " + tag + L": the caret is where it was", I(before.caret), I(after.caret));
        Eq(L"after the " + tag + L": the selection is what it was", I(before.selLength()), I(after.selLength()));
        Eq(L"after the " + tag + L": the text is what it was", before.text, after.text);
    }
}

// The Menu key and Shift+F10 open the Cut / Copy / Paste menu of the field that has the focus (the field is not asked
// for its own, different, menu); the calculator has no such menu.
void FieldMenuFromKeyboard() {
    Win wp(true);
    HcWindow& w = *wp;
    w.SetConverterText(0, L"830");
    for (int field = 0; field < 2; ++field) {
        w.GotFocus(field, false);
        for (int style = 0; style < 2; ++style) {
            const std::wstring tag = std::wstring(L"field ") + I(field) + (style == 0 ? L", the Menu key" : L", Shift+F10");
            bool used = style == 0 ? w.HandleKey(GDK_KEY_Menu, (GdkModifierType)0, nullptr) : w.HandleKey(GDK_KEY_F10, GDK_SHIFT_MASK, nullptr);
            True(tag + L": the key is used", used);
            WaitUntil([&] { return w.Overlay(); }, 2000);
            True(tag + L": the Cut / Copy / Paste menu is open", w.Overlay() && !w.IsMenuOpen());
            Eq(tag + L": it is the menu of the focused field", I(field), I(w.ContextField()));
            w.CloseContextMenus();
            WaitUntil([&] { return !w.Overlay(); }, 2000);
            Pump();
        }
    }
    w.SelectTab(1);
    True(L"the calculator: the Menu key is not used", !w.HandleKey(GDK_KEY_Menu, (GdkModifierType)0, nullptr));
}

// The right button on a calculator value opens the Copy menu where the pointer is.
void CopyMenuAtPointer() {
    Win wp(true);
    HcWindow& w = *wp;
    w.SelectTab(1);
    Keys(w, L"7:30+1=");
    for (int which = 0; which < 2; ++which) {
        GtkWidget* label = w.CalcValueLabel(which);
        GtkGestureClick* secondary = nullptr;
        GListModel* controllers = gtk_widget_observe_controllers(label);
        for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i) {
            GObject* c = static_cast<GObject*>(g_list_model_get_item(controllers, i));
            if (GTK_IS_GESTURE_CLICK(c) && gtk_gesture_single_get_button(GTK_GESTURE_SINGLE(c)) == GDK_BUTTON_SECONDARY) secondary = GTK_GESTURE_CLICK(c);
            g_object_unref(c);
        }
        g_object_unref(controllers);
        True(L"the value " + I(which) + L" has a gesture for the right button", secondary != nullptr);
        if (!secondary) continue;
        g_signal_emit_by_name(secondary, "pressed", 1, 20.0, 9.0);
        GtkWidget* pop = nullptr;
        for (GtkWidget* c = gtk_widget_get_first_child(label); c; c = gtk_widget_get_next_sibling(c))
            if (GTK_IS_POPOVER(c)) pop = c;
        True(L"the right click on value " + I(which) + L" opens the Copy menu", pop != nullptr);
        if (pop) {
            WaitUntil([&] { return gtk_widget_get_mapped(pop) != FALSE; }, 2000);
            GdkRectangle r = { -1, -1, 0, 0 };
            bool has = gtk_popover_get_pointing_to(GTK_POPOVER(pop), &r) != FALSE;
            True(L"... pointing at the pointer", has && r.x == 20 && r.y == 9);
        }
        w.CloseContextMenus();
        WaitUntil([&] { return !w.Overlay(); }, 2000);
        Pump();
    }
}

// Clicks in a field that has the focus: the first places the caret, the second and the third select everything, a
// fourth places the caret again (and so on).
void RepeatedClicks() {
    Win wp(true);
    HcWindow& w = *wp;
    w.SetConverterText(0, L"08:30");
    w.GotFocus(0, false);
    for (int n = 1; n <= 7; ++n) {
        const bool selects = (n - 1) % 3 != 0;
        w.SetCaret(0, 2, 2);
        bool used = w.OnFieldPressed(0, n, 1, 5, 5);
        Eq(L"click number " + I(n) + (selects ? L" selects all" : L" is left to the field"), selects ? L"1" : L"0", I(used));
        if (selects) Eq(L"click number " + I(n) + L": all five characters are selected", L"5", I(w.GetField(0).selLength()));
    }
}

// What a screen reader is told when = is pressed.
void Announcements() {
    Win wp(true);
    HcWindow& w = *wp;
    w.SelectTab(1);
    Keys(w, L"7:30+1");
    Eq(L"nothing is announced while typing", L"", w.LastAnnouncement());
    Keys(w, L"=");
    Eq(L"the result is announced after =", L"Result 08:30, 8" + g_sep + L"50 decimal hours", w.LastAnnouncement());
    w.Press(L"C");
    Keys(w, L"5/0=");
    True(L"an error is announced as the error", !w.LastAnnouncement().empty() && w.LastAnnouncement().find(L"Result") == std::wstring::npos);
    Eq(L"... with the text of the display", w.CalcMain(), w.LastAnnouncement());
    // The value labels keep their text as their name: the explanation is a description, not a replacement label.
    Eq(L"the left value label shows the value", w.CalcMain(), FromUtf8(gtk_label_get_text(GTK_LABEL(w.CalcValueLabel(0)))));
}

// The look of the keys: a tool tip only where it says more than the key does, the libadwaita 1.6 style class for secondary
// text, bigger operator glyphs.
void KeyLook() {
    Win wp(true);
    HcWindow& w = *wp;
    for (int tab = 0; tab < 2; ++tab) {
        const std::vector<hcgtk::KeyDef>& keys = tab == 0 ? w.ConverterKeys() : w.CalculatorKeys();
        for (const hcgtk::KeyDef& k : keys) {
            if (!k.name) continue;
            const char* tip = gtk_widget_get_tooltip_text(k.button);
            const bool repeats = !k.icon && ToUtf8(k.label) == k.name;
            True(L"key " + k.key + L" (" + FromUtf8(k.name) + L"): " + (repeats ? L"no tool tip, it would repeat the label" : L"a tool tip"), repeats ? tip == nullptr : tip != nullptr);
        }
    }
    for (const hcgtk::KeyDef& k : w.CalculatorKeys()) {
        const bool op = k.key == L"/" || k.key == L"*" || k.key == L"-" || k.key == L"+";
        True(L"calculator key " + k.key + (op ? L" is an operator key (big glyph)" : L" is not"), (gtk_widget_has_css_class(k.button, "hc-op") != FALSE) == op);
    }
    True(L"the converter caption uses the dimmed class", gtk_widget_has_css_class(w.ConverterCaptionLabel(), "dimmed") != FALSE);
    True(L"... and not the deprecated dim-label", gtk_widget_has_css_class(w.ConverterCaptionLabel(), "dim-label") == FALSE);
}

// In libadwaita's high contrast mode the window carries hc-high-contrast (the CSS leaves the borders of the keys to
// libadwaita then). The self test also runs once with ADW_DEBUG_HIGH_CONTRAST=1 and HC_REQUIRE_HIGH_CONTRAST=1.
void HighContrast() {
    Win wp(true);
    const bool on = adw_style_manager_get_high_contrast(adw_style_manager_get_default()) != FALSE;
    True(L"the window carries hc-high-contrast exactly when libadwaita is in high contrast", wp->HighContrastClass() == on);
    const char* require = std::getenv("HC_REQUIRE_HIGH_CONTRAST");
    if (require && *require) True(L"libadwaita is in high contrast (ADW_DEBUG_HIGH_CONTRAST)", on);
}

// The renderer: the user's choice wins, and cairo only where the machine has no usable GPU.
void RendererChoice() {
    using hcgtk::RendererWish;
    Eq(L"GSK_RENDERER set: the user chose", L"", FromUtf8(RendererWish("gl", nullptr, true)));
    Eq(L"... also with HOURCALC_RENDERER", L"", FromUtf8(RendererWish("ngl", "cairo", true)));
    Eq(L"no GPU: cairo", L"cairo", FromUtf8(RendererWish(nullptr, nullptr, true)));
    Eq(L"a GPU: GTK chooses", L"", FromUtf8(RendererWish(nullptr, nullptr, false)));
    Eq(L"empty variables are not set (no GPU)", L"cairo", FromUtf8(RendererWish("", "", true)));
    Eq(L"empty variables are not set (a GPU)", L"", FromUtf8(RendererWish("", "", false)));
    Eq(L"HOURCALC_RENDERER=gtk: GTK chooses, whatever the machine", L"", FromUtf8(RendererWish(nullptr, "gtk", true)));
    Eq(L"HOURCALC_RENDERER=gl is passed on (no GPU)", L"gl", FromUtf8(RendererWish(nullptr, "gl", true)));
    Eq(L"HOURCALC_RENDERER=cairo is passed on (a GPU)", L"cairo", FromUtf8(RendererWish(nullptr, "cairo", false)));

    char* tmp = g_dir_make_tmp("hcgtk-dri-XXXXXX", nullptr);
    if (!tmp) {
        Fail(L"renderer", L"cannot make a temporary folder");
        return;
    }
    std::string dir = tmp;
    g_free(tmp);
    True(L"a folder that does not exist has no render node", !hcgtk::HasRenderNode((dir + "/none").c_str()));
    True(L"an empty folder has none", !hcgtk::HasRenderNode(dir.c_str()));
    std::ofstream(dir + "/card0") << "x";
    True(L"a card without a render node does not count", !hcgtk::HasRenderNode(dir.c_str()));
    std::ofstream(dir + "/renderD128") << "x";
    True(L"a render node the user can open counts", hcgtk::HasRenderNode(dir.c_str()));
    chmod((dir + "/renderD128").c_str(), 0);
    if (access((dir + "/renderD128").c_str(), R_OK | W_OK) != 0) True(L"... and one the user cannot open does not", !hcgtk::HasRenderNode(dir.c_str()));
    chmod((dir + "/renderD128").c_str(), 0600);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ------------------------------------------------------------------ tabs and focus

void TabsAndFocus() {
    Win wp(true);
    HcWindow& w = *wp;
    Eq(L"converter focus at start", L"0", I(w.FocusField()));
    w.SelectTab(1);
    Eq(L"calculator tab is the visible child", L"calculator", FromUtf8(adw_view_stack_get_visible_child_name(ADW_VIEW_STACK(w.Stack()))));
    True(L"the calculator has no focus widget (Enter is equals)", gtk_window_get_focus(w.Window()) == nullptr);
    Eq(L"... and no focus field", L"-1", I(w.FocusField()));
    w.SelectTab(0);
    True(L"back on the converter the active field has the focus", gtk_window_get_focus(w.Window()) == w.FieldWidget(0));
    // Tab moves between the fields and selects all.
    w.SetConverterText(0, L"8:30");
    w.GotFocus(0, false);
    True(L"Tab is used", w.HandleKey(GDK_KEY_Tab, (GdkModifierType)0, nullptr));
    Eq(L"Tab moves to the right field", L"1", I(w.FocusField()));
    Eq(L"... and selects all of it", I((long long)RT(w).size()), I(w.GetField(1).selLength()));
    True(L"Shift+Tab is used", w.HandleKey(GDK_KEY_ISO_Left_Tab, GDK_SHIFT_MASK, nullptr));
    Eq(L"Shift+Tab moves back", L"0", I(w.FocusField()));
    Eq(L"... and selects all of it", I((long long)LT(w).size()), I(w.GetField(0).selLength()));
    // Switching the tab by the view switcher's stack (a click on a switcher button does the same).
    adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(w.Stack()), "calculator");
    Eq(L"the switcher selects the calculator", L"1", I(w.CurrentTab()));
    True(L"... and the focus leaves the header", gtk_window_get_focus(w.Window()) == nullptr);
    adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(w.Stack()), "converter");
    Eq(L"the switcher selects the converter", L"0", I(w.CurrentTab()));
    // Enter converts in the converter, whatever field has the focus.
    w.Press(L"RESET");
    w.SetConverterText(1, S(L"8,5"));
    w.HandleKey(GDK_KEY_Return, (GdkModifierType)0, nullptr);
    Eq(L"Enter converts the active field", S(L"8,50"), RT(w));
}

// ------------------------------------------------------------------ layout, fitting, nothing clipped

// 1 when the text of a GtkText is scrolled (it does not fit); for text aligned to the right the offset is
// negative while it fits and the room is left of it.
int ScrollOffset(GtkWidget* t) {
    int off = 0;
    g_object_get(t, "scroll-offset", &off, nullptr);
    return off > 0 ? 1 : 0;
}

bool Ellipsized(GtkWidget* label) { return pango_layout_is_ellipsized(gtk_label_get_layout(GTK_LABEL(label))) != FALSE; }

void CheckInside(HcWindow& w, GtkWidget* widget, const std::wstring& what) {
    graphene_rect_t r;
    if (!Bounds(widget, W(w), &r)) {
        Fail(what, L"no bounds");
        return;
    }
    int ww = gtk_widget_get_width(W(w)), wh = gtk_widget_get_height(W(w));
    ++g_checks;
    if (r.origin.x < -0.5f || r.origin.y < -0.5f || r.origin.x + r.size.width > ww + 0.5f || r.origin.y + r.size.height > wh + 0.5f)
        Fail(what, L"outside the window");
}

void CheckKeys(HcWindow& w, const std::vector<hcgtk::KeyDef>& keys, const std::wstring& tag) {
    for (const hcgtk::KeyDef& k : keys) {
        graphene_rect_t r;
        std::wstring what = tag + L" key " + k.key;
        if (!Bounds(k.button, W(w), &r)) { Fail(what, L"no bounds"); continue; }
        True(what + L" is large enough to press", r.size.width >= 40 && r.size.height >= 30);
        CheckInside(w, k.button, what + L" is inside the window");
        // its label is not cut off
        GtkWidget* child = gtk_button_get_child(GTK_BUTTON(k.button));
        if (child && GTK_IS_LABEL(child)) True(what + L" label not ellipsized", !Ellipsized(child));
    }
}

double KeypadY(HcWindow& w) {
    graphene_rect_t r;
    Bounds(w.Keypad(), W(w), &r);
    return r.origin.y;
}

void LayoutAtSize(int width, int height, const wchar_t* tag, bool atMinimum) {
    Win wp;
    HcWindow& w = *wp;
    gtk_window_set_default_size(w.Window(), width, height);
    gint64 lapStart = g_get_monotonic_time();
    auto lap = [&](const char* what) {
        if (g_verbose) std::printf("      layout %ls: %s at %.1f s\n", tag, what, (double)(g_get_monotonic_time() - lapStart) / 1e6);
    };
    w.Settle(3);
    lap("settled");
    std::wstring t = tag;
    // The content is as large as asked, less the room the decorations of the window take (about 10 px on a
    // display without a compositor); a window asked to be tiny is as small as the minimum size says.
    int cw = gtk_widget_get_width(w.Content()), ch = gtk_widget_get_height(w.Content());
    if (atMinimum) {
        int minContent = 0;
        gtk_widget_measure(w.Content(), GTK_ORIENTATION_HORIZONTAL, -1, &minContent, nullptr, nullptr, nullptr);
        True(t + L": the content is at least as wide as its content needs (" + I(cw) + L" for " + I(minContent) + L")", cw >= minContent);
        // libadwaita's minimum width (360 px, less the room of the decorations); a font that needs more makes it wider
        True(t + L": content width at the minimum (" + I(cw) + L")", cw <= std::max(HcWindow::kMinWidth, minContent + 1) && cw >= HcWindow::kMinWidth - 12);
        True(t + L": content height at the minimum (" + I(ch) + L")", ch <= HcWindow::kMinHeight && ch >= HcWindow::kMinHeight - 12);
    } else {
        True(t + L": content width (asked " + I(width) + L", is " + I(cw) + L")", cw <= width && cw >= width - 12);
        True(t + L": content height (asked " + I(height) + L", is " + I(ch) + L")", ch <= height && ch >= height - 12);
    }
    // ---- converter
    lap("sized");
    CheckKeys(w, w.ConverterKeys(), t + L" converter");
    CheckInside(w, w.FieldWidget(0), t + L" left field inside");
    CheckInside(w, w.FieldWidget(1), t + L" right field inside");
    double y0 = KeypadY(w);
    lap("converter keys checked");
    const wchar_t* longs[] = { L"", L"8:30", L"123:45", L"9999999:99", L"1234567:59" };
    for (const wchar_t* text : longs) {
        w.Press(L"RESET");
        w.SetConverterText(0, text);
        w.WaitFrames(2);
        std::wstring what = t + L" converter <" + text + L">";
        Eq(what + L": keypad stays put", I((long long)std::lround(y0)), I((long long)std::lround(KeypadY(w))));
        Eq(what + L": left text not scrolled", L"0", I(ScrollOffset(w.FieldWidget(0))));
        Eq(what + L": right text not scrolled", L"0", I(ScrollOffset(w.FieldWidget(1))));
        True(what + L": scale within limits", w.ConverterScale() <= 1.0 + 1e-9 && w.ConverterScale() >= HcWindow::kConvMinScale - 1e-9);
        if (std::wstring(text).size() >= 9 && width <= HcWindow::kDefaultWidth) True(what + L": a long value shrinks the font", w.ConverterScale() < 1.0);
        if (std::wstring(text).size() <= 5) True(what + L": a short value keeps the full size", w.ConverterScale() == 1.0);
        True(what + L": caption not ellipsized", !Ellipsized(w.ConverterCaptionLabel()));
    }
    w.Press(L"RESET");
    w.SetConverterText(1, S(L"9999999,999999"));
    w.WaitFrames(2);
    Eq(t + L": the longest right value is not scrolled", L"0", I(ScrollOffset(w.FieldWidget(1))));
    Eq(t + L": ... nor the left", L"0", I(ScrollOffset(w.FieldWidget(0))));
    // the two values share one size: setting the scale of one is the scale of the pair
    True(t + L": the pair shares a size", w.ConverterScale() >= HcWindow::kConvMinScale - 1e-9);
    w.Press(L"RESET");

    lap("converter done");
    // ---- calculator
    w.SelectTab(1);
    w.WaitFrames(2);
    CheckKeys(w, w.CalculatorKeys(), t + L" calculator");
    double yc = KeypadY(w);
    lap("calculator keys checked");
    struct { const wchar_t* keys; const wchar_t* tag; } states[] = {
        { L"", L"idle" }, { L"7:30+7:20=", L"result" }, { L"7:30+7:2", L"entry" }, { L"7:30/0=", L"divide by zero" },
        { L"9999999:99+9999999:99=", L"overflow" }, { L"1234567:59*7=", L"long result" }, { L"9999999:5", L"long entry" },
    };
    for (auto& s : states) {
        w.Press(L"C");
        Keys(w, s.keys);
        w.WaitFrames(2);
        std::wstring what = t + L" calculator " + s.tag;
        Eq(what + L": keypad stays put", I((long long)std::lround(yc)), I((long long)std::lround(KeypadY(w))));
        True(what + L": left value not ellipsized", !Ellipsized(w.CalcValueLabel(0)));
        if (w.CalcRightVisible()) True(what + L": right value not ellipsized", !Ellipsized(w.CalcValueLabel(1)));
        True(what + L": caption not ellipsized", !Ellipsized(w.CalcCaptionLabel()));
        True(what + L": expression not ellipsized", !Ellipsized(w.ExpressionLabel()));
        True(what + L": scale within limits", w.CalcScale() <= 1.0 + 1e-9 && w.CalcScale() >= HcWindow::kCalcLeftMinScale - 1e-9);
        CheckInside(w, w.CalcValueLabel(0), what + L": left value inside");
    }
    lap("calculator states done");
    // the expression line keeps the end of a long chain
    w.Press(L"C");
    for (int i = 0; i < 14; i++) Keys(w, L"7:30+");
    w.WaitFrames(2);
    std::wstring shown = w.ExpressionShown();
    True(t + L": a long chain gets a leading ellipsis", shown.compare(0, 2, L"\x2026 ") == 0);
    True(t + L": ... and ends with the operator", !shown.empty() && shown.back() == L'+');
    True(t + L": ... and fits", !Ellipsized(w.ExpressionLabel()));
    Eq(t + L": ... and the keypad stays put", I((long long)std::lround(yc)), I((long long)std::lround(KeypadY(w))));
    lap("chain done");
    // the error text may use both columns
    w.Press(L"C");
    Keys(w, L"7:30/0=");
    w.WaitFrames(2);
    graphene_rect_t lv, disp;
    Bounds(w.CalcValueLabel(0), W(w), &lv);
    Bounds(w.CalculatorDisplay(), W(w), &disp);
    True(t + L": the error text uses the full width of the display", lv.size.width >= disp.size.width - 1.5f);
    w.Press(L"C");
    w.SelectTab(0);
}

void Layouts() {
    LayoutAtSize(HcWindow::kDefaultWidth, HcWindow::kDefaultHeight, L"default size", false);
    LayoutAtSize(100, 100, L"minimum size", true);
    LayoutAtSize(600, 800, L"large size", false);

    // The header: view switcher and menu button fit at the minimum width.
    Win wp;
    HcWindow& w = *wp;
    gtk_window_set_default_size(w.Window(), 100, 100);
    w.Settle(3);
    graphene_rect_t sw, mb, hb;
    Bounds(w.Switcher(), W(w), &sw);
    Bounds(w.MenuButton(), W(w), &mb);
    Bounds(w.HeaderBar(), W(w), &hb);
    True(L"header: the view switcher fits in the header bar", sw.origin.x >= 0 && sw.origin.x + sw.size.width <= hb.size.width + 0.5f);
    True(L"header: switcher and menu button do not overlap", sw.origin.x + sw.size.width <= mb.origin.x + 0.5f);
    True(L"header: the menu button is inside the window", mb.origin.x + mb.size.width <= hb.size.width + 0.5f);
    int minW = 0;
    gtk_widget_measure(w.Content(), GTK_ORIENTATION_HORIZONTAL, -1, &minW, nullptr, nullptr, nullptr);
    if (g_strictMin) True(L"the header with both labels and the menu button fits in less than the minimum width (" + I(minW) + L")", minW <= HcWindow::kMinWidth - 10);
    int minH = 0;
    gtk_widget_measure(W(w), GTK_ORIENTATION_VERTICAL, -1, &minH, nullptr, nullptr, nullptr);
    True(L"the window's minimum height is 480 (" + I(minH) + L")", minH <= HcWindow::kMinHeight);
}

// The decimal separator of the process locale (setlocale(LC_ALL, "") at start), without the override of the tests:
// the core takes it from the radix of the locale. Run with LC_ALL=nl_BE.UTF-8 (LOCPATH=a folder made with localedef)
// to see ',' come from the real locale.
void ProcessLocale() {
    tmath::SetDecimalSeparatorOverride(L"");
    g_sep = FromUtf8(localeconv()->decimal_point);
    std::printf("   process locale %s, decimal separator '%s'\n", setlocale(LC_NUMERIC, nullptr), localeconv()->decimal_point);
    Win wp;
    HcWindow& w = *wp;
    Eq(L"the separator is the radix of the locale", g_sep, tmath::DecimalSeparator());
    Eq(L"the decimal key shows it", g_sep, w.KeyAt(w.KeyIndex(L",")).label);
    Check(w, 1, L"8,5", L"08:30");
    Check(w, 0, L"08:20", L"8,33");
    Check(w, 0, L"830", L"8,50");
    w.SelectTab(1);
    Keys(w, L"7:30+7:20=");
    Eq(L"calculator right value in the process locale", S(L"14,83"), w.CalcDecimal());
    Eq(L"calculator right label", S(L"14,83"), FromUtf8(gtk_label_get_text(GTK_LABEL(w.CalcValueLabel(1)))));
    w.Press(L"C");
    Keys(w, L"7,5+1=");
    Eq(L"a decimal entry in the process locale", L"08:30", w.CalcMain());
    w.Press(L"C");
    RaiseText(w, L"7.5" + std::wstring(1, L'+') + L"1");
    RaiseKey(w, GDK_KEY_Return);
    Eq(L"typed 7.5 + 1 (the dot is a decimal separator in any locale)", L"08:30", w.CalcMain());
}

// Input that goes through the X server, where the XTest extension can be used.
void RealInput() {
    bool ran = RealInputTests([](const std::wstring& what, bool ok) { True(what, ok); });
    if (!ran) std::printf("   (the real input group did not run: libXtst is not installed here)\n");
}

// ------------------------------------------------------------------ all of it, in one separator

void Group(const char* name, void (*fn)()) {
    if (!g_only.empty() && std::string(name).find(g_only) == std::string::npos) return;
    g_group = name;
    if (g_verbose) std::printf("-- %s\n", name);
    gint64 t0 = g_get_monotonic_time();
    fn();
    double seconds = (double)(g_get_monotonic_time() - t0) / 1e6;
    if (g_verbose || seconds > 5.0) std::printf("   %s: %.1f s\n", name, seconds);
}

void RunAll(const std::wstring& separator) {
    g_sep = separator;
    tmath::SetDecimalSeparatorOverride(separator);
    std::printf("-- decimal separator '%s'\n", ToUtf8(separator).c_str());
    Group("converter", ConverterWiring);
    Group("converter editing", ConverterEditing);
    Group("real editing", RealEditing);
    Group("calculator", CalculatorWiring);
    Group("spec tables", SpecTables);
    Group("keyboard routing", KeyboardRouting);
    Group("typing", WindowTyping);
    Group("converter state", WindowConverterState);
    Group("focus selection", WindowFocusSelection);
    Group("pointer", PointerOnFields);
    Group("convert keeps value", WindowConvertKeepsValue);
    Group("keyboard mapping", KeyboardMapping);
    Group("calculator mirror", CalcMirror);
    Group("expression line", ExpressionLine);
    Group("menu structure", MenuStructure);
    Group("theme", ThemeSwitching);
    Group("decimal places", DecimalPlacesMenu);
    Group("settings", SettingsPersistence);
    Group("about and menus", AboutAndMenus);
    Group("clipboard", Clipboard);
    Group("menu keeps caret", MenuKeepsCaret);
    Group("field menu keyboard", FieldMenuFromKeyboard);
    Group("copy menu pointer", CopyMenuAtPointer);
    Group("repeated clicks", RepeatedClicks);
    Group("announcements", Announcements);
    Group("key look", KeyLook);
    Group("high contrast", HighContrast);
    Group("renderer", RendererChoice);
    Group("tabs and focus", TabsAndFocus);
    Group("layout", Layouts);
    Group("large text", LargeText);
    Group("real input", RealInput);
}

}  // namespace

int SelfTest(bool systemDark, bool verbose, const std::string& onlyGroup, bool strictMin) {
    g_strictMin = strictMin;
    g_only = onlyGroup;
    g_systemDark = systemDark;
    g_verbose = verbose;
    std::printf("selftest: GTK %u.%u.%u, libadwaita %u.%u.%u, system scheme %s\n", gtk_get_major_version(), gtk_get_minor_version(),
                gtk_get_micro_version(), adw_get_major_version(), adw_get_minor_version(), adw_get_micro_version(), systemDark ? "dark" : "light");
    RunAll(L",");
    RunAll(L".");
    std::printf("-- the separator of the process locale\n");
    Group("process locale", ProcessLocale);
    tmath::SetDecimalSeparatorOverride(L"");
    std::printf("selftest (system scheme %s): %d checks, %d failures\n", systemDark ? "dark" : "light", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

}  // namespace hctest

namespace hctest {

// Prints the minimum and natural sizes of the parts of the window and where they end up (for tuning the layout).
int LayoutInfo() {
    InitToolkit("Cantarell 11");
    Win wp;
    HcWindow& w = *wp;
    gtk_window_set_default_size(w.Window(), 100, 100);
    w.Settle(3);
    auto measure = [&](const char* name, GtkWidget* widget) {
        int minW = 0, natW = 0, minH = 0, natH = 0;
        gtk_widget_measure(widget, GTK_ORIENTATION_HORIZONTAL, -1, &minW, &natW, nullptr, nullptr);
        gtk_widget_measure(widget, GTK_ORIENTATION_VERTICAL, -1, &minH, &natH, nullptr, nullptr);
        graphene_rect_t r;
        graphene_rect_init(&r, 0, 0, 0, 0);
        if (!gtk_widget_compute_bounds(widget, GTK_WIDGET(w.Window()), &r)) return;
        std::printf("%-22s min %4dx%-4d nat %4dx%-4d  at %6.1f,%6.1f size %6.1fx%-6.1f\n", name, minW, minH, natW, natH, r.origin.x, r.origin.y, r.size.width,
                    r.size.height);
    };
    measure("window", GTK_WIDGET(w.Window()));
    measure("content (toolbar)", w.Content());
    measure("header bar", w.HeaderBar());
    measure("view switcher", w.Switcher());
    measure("menu button", w.MenuButton());
    measure("stack", w.Stack());
    measure("converter display", w.ConverterDisplay());
    measure("left field", w.FieldWidget(0));
    measure("right field", w.FieldWidget(1));
    measure("caption", w.ConverterCaptionLabel());
    measure("converter keypad", w.Keypad());
    w.SelectTab(1);
    w.WaitFrames(2);
    measure("calculator display", w.CalculatorDisplay());
    measure("calc left value", w.CalcValueLabel(0));
    measure("calc expression", w.ExpressionLabel());
    measure("calculator keypad", w.Keypad());
    return 0;
}

}  // namespace hctest
