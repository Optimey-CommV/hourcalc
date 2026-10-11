// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc GTK front end - the main window: an AdwApplicationWindow with a header bar (the view switcher
// Converter / Calculator as its title, the primary menu at its end) above the display and keypad of the
// active tab. The behaviour is the one of the Windows front end (native/app/app_logic.cpp), on GNOME widgets:
// the two converter values are real GtkText fields, the calculator display is read only, the theme comes from
// AdwStyleManager, the settings from the core (hc::settings).
//
// The public methods are the surface that the headless self test, the render tool and the start-up
// measurement drive; the production code uses them as well.

#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "hc_core.h"

namespace hcgtk {

class HcWindow;

enum KeyStyle { KeyDigit, KeyFunc, KeyOperator, KeyAccent };

struct KeyDef {
    HcWindow* owner;
    int tab;                  // 0: converter, 1: calculator
    std::wstring key;         // what Press() gets: "7", "CE", "BACK", "RESET", "CONVERT", "+", ...
    std::wstring label;       // text of a text key (empty for an icon key)
    const char* icon;         // icon name of an icon key, else null
    KeyStyle style;
    int row, col, span;
    const char* name;         // accessible name (null: the label is the name)
    GtkWidget* button;
};

// One converter value as the GtkText holds it: text, caret and the other end of the selection.
struct FieldState {
    std::wstring text;
    int caret = 0;
    int anchor = 0;
    int selStart() const { return caret < anchor ? caret : anchor; }
    int selEnd() const { return caret < anchor ? anchor : caret; }
    int selLength() const { return selEnd() - selStart(); }
};

class HcWindow {
public:
    // The sizes (px) of the window; the keypad and the display scale with it.
    // (constexpr, so they are inline and can be bound to a reference without a definition: std::max in a debug or
    // sanitizer build)
    static constexpr int kDefaultWidth = 360, kDefaultHeight = 600, kMinWidth = 360, kMinHeight = 480;
    static constexpr int kPageMargin = 12;     // left and right margin of a page
    static constexpr int kColumnGap = 12;      // between the two columns of a display
    // The font scale limits of the values (1.0 = the size of the style, 30 pt): the converter pair, the
    // calculator left and right value, and the largest scale of an error text (it may use both columns).
    static constexpr double kConvMinScale = 0.40, kCalcLeftMinScale = 0.30, kCalcRightMinScale = 0.40, kErrorMaxScale = 0.65;

    // settingsPath: the settings file (decimal places, theme); empty: neither read nor written.
    static HcWindow* Create(GtkApplication* app, const std::wstring& settingsPath);
    // Destroys the GTK window; this object goes with it once GTK has finalized the window.
    void Destroy();
    // The HcWindow of a window of this application (null for any other).
    static HcWindow* Of(GtkWindow* window);

    GtkWindow* Window() const { return GTK_WINDOW(win_); }
    void Present();

    // ---- state
    hc::CalcEngine& Engine() { return engine_; }
    int CurrentTab() const { return tab_; }
    int ActiveField() const { return active_; }
    int FocusField() const { return focus_; }
    int ContextField() const { return ctxField_; }
    int DecimalPlaces() const { return decimals_; }
    hc::settings::ThemeMode ThemeChoice() const { return themeMode_; }
    const std::wstring& SettingsPath() const { return settingsPath_; }
    const std::wstring& ConverterCaption() const { return caption_; }
    const std::wstring& CalcMain() const { return calcL_; }
    const std::wstring& CalcDecimal() const { return calcR_; }
    const std::wstring& CalcCaption() const { return calcCap_; }
    // What was last announced to a screen reader (the result after =).
    const std::wstring& LastAnnouncement() const { return announced_; }
    const std::wstring& ExpressionShown() const { return exprShown_; }
    bool CalcRightVisible() const { return !error_; }
    double ConverterScale() const { return convScale_; }
    double CalcScale() const { return calcScale_; }
    bool UnderlineVisible(int field) const { return tab_ == 0 && active_ == field; }
    std::wstring ConverterText(int field) const { return GetField(field).text; }
    FieldState GetField(int field) const;
    void SetCaret(int field, int caret, int anchor);

    // ---- the actions of the user
    void Press(const std::wstring& key);            // a keypad key of the visible tab ("7", "CE", "CONVERT", ...)
    void PressKeyOfTab(int tab, const std::wstring& key);
    void SelectTab(int index);
    void SetDecimalPlaces(int places);
    void SetThemeMode(hc::settings::ThemeMode mode);
    void SetConverterText(int field, const std::wstring& text);   // as if typed: the caret ends up at the end
    void TypeText(int field, const std::wstring& s);              // replaces the selection of a field (or inserts at the caret)
    // A key as the window routes it (capture phase). 'keyval' is the translated key of the layout (what the user
    // typed), 'state' the modifiers, 'event' the GDK event (null in tests). True when the window used it. In the
    // converter, text for a focused field is left to the field (false).
    bool HandleKey(guint keyval, GdkModifierType state, const GdkEvent* event);
    // The same for the tests, plus what the focused GtkText does with a typed character.
    bool KeyPress(guint keyval, GdkModifierType state = (GdkModifierType)0);
    void ShowAbout();
    void CloseAbout();
    bool KeyEnabled(const std::wstring& key) const;
    int KeyIndex(const std::wstring& key) const;
    const KeyDef& KeyAt(int i) const { return (tab_ == 0 ? convKeys_ : calcKeys_)[(size_t)i]; }
    const std::vector<KeyDef>& ConverterKeys() const { return convKeys_; }
    const std::vector<KeyDef>& CalculatorKeys() const { return calcKeys_; }

    // ---- focus rules of the converter (SPEC 4); the self test drives them directly
    void SetWindowActive(bool active);
    bool WindowActive() const { return windowActive_; }
    // The keyboard focus arrives at a field: from the other field (Tab, a click: everything selected) or from
    // outside the window (activation: the caret and the selection stay).
    void GotFocus(int field, bool fromField);
    void MouseDownOnField(int field);

    // ---- clipboard
    void CopyCalc(int which);                       // 0: the h:mm value, 1: the decimal value
    void PasteIntoCalc();                           // asynchronous: reads the clipboard
    bool PasteTextIntoCalc(const std::wstring& text);
    void CutField(int field);
    void CopyField(int field);
    void PasteField(int field);

    // ---- widgets, menus
    GtkWidget* Content() const { return toolbar_; }       // header bar and pages: the window without its decorations
    GtkWidget* Stack() const { return stack_; }
    GtkWidget* HeaderBar() const { return header_; }
    GtkWidget* MenuButton() const { return menuBtn_; }
    GtkWidget* Switcher() const { return switcher_; }
    GtkWidget* Keypad() const { return tab_ == 0 ? convPad_ : calcPad_; }
    GtkWidget* ConverterDisplay() const { return convDisplay_; }
    GtkWidget* CalculatorDisplay() const { return calcDisplay_; }
    GtkWidget* FieldWidget(int field) const { return text_[field]; }
    GtkWidget* ConverterCaptionLabel() const { return capLabel_; }
    GtkWidget* ExpressionLabel() const { return exprLabel_; }
    GtkWidget* CalcValueLabel(int which) const { return calcVal_[which]; }
    GtkWidget* CalcCaptionLabel() const { return calcCapLabel_; }
    GMenuModel* MenuModel();
    GtkWidget* MenuPopover();                       // the popover of the primary menu (created on first use)
    void OpenMenu();
    void CloseMenu();
    bool IsMenuOpen() const;
    bool IsAboutOpen() const;
    AdwDialog* AboutDialog() const { return about_; }
    // A popup menu for a converter field / a calculator value at a point of that widget (-1: its centre); returns the popover.
    GtkWidget* OpenFieldMenu(int field, double x = -1, double y = -1);
    GtkWidget* OpenCopyMenu(int which, double x = -1, double y = -1);
    void CloseContextMenus();
    bool Overlay() const;                           // a menu or dialog has the keyboard

    // Settings are written in an idle callback (so a change is on the screen first); this writes what is pending now.
    void FlushSettings();
    int SavesCount() const { return saves_; }

    // For tests and tools: presents the window and runs the main loop until it is mapped and 'frames' frames were painted.
    void Settle(int frames = 2);
    void WaitFrames(int n);

    // Callbacks of the GTK signals (public only so the trampolines in the .cpp files can reach them).
    void OnFieldChanged(int f);
    void OnTabChanged();
    void OnFocusWidgetChanged();
    void OnActiveChanged();
    void OnDisplayAllocated(int page, int width);
    void OnDestroyed();
    // The window carries the style class hc-high-contrast while libadwaita is in high contrast.
    void UpdateHighContrast();
    bool HighContrastClass() const { return gtk_widget_has_css_class(win_, "hc-high-contrast") != FALSE; }
    bool OnFieldPressed(int f, int nPress, int button, double x, double y);
    void ScheduleRefocus();

private:
    HcWindow(GtkApplication* app, const std::wstring& settingsPath);
    ~HcWindow();
    HcWindow(const HcWindow&) = delete;
    HcWindow& operator=(const HcWindow&) = delete;
    friend struct Busy;

    // building (hcwindow_build.cpp)
    void BuildKeys();
    void BuildWidgets(GtkApplication* app);
    GtkWidget* BuildConverterPage();
    GtkWidget* BuildCalculatorPage();
    GtkWidget* BuildKeypad(std::vector<KeyDef>& keys);
    GtkWidget* NewValueBox(int field);
    GtkWidget* NewCalcValue(int which);
    void BuildActions();
    GMenu* BuildMenuModel();

    // behaviour (hcwindow.cpp)
    void ApplyThemeToManager();
    void SetThemeAction(hc::settings::ThemeMode mode);
    void SetPlacesAction(int places);
    void SetActive(int field);
    void UpdateKeyStates();
    // keepCaret: the field gets the focus back from a menu or a dialog, caret and selection stay as they were.
    void FocusActive(bool keepCaret = false);
    void RefocusContent();
    void Announce(const std::wstring& text);
    void SetFieldRaw(int field, const std::wstring& text);
    void SetFieldText(int field, const std::wstring& text);
    void EditField(int field, const std::wstring& next, int caret);
    void ApplyFieldEdit(int field, const std::wstring& raw, int caret);
    void RecomputeConverter();
    void InsertText(const std::wstring& s);
    void PressConverter(const std::wstring& key);
    void PressCalc(const std::wstring& key);
    void RefreshCalc();
    void UpdateConverterLabels();
    void SetClipboard(const std::wstring& text);
    void SaveLater(bool places, bool theme);
    bool CalcKeyForKeyval(guint keyval, GdkModifierType state, std::wstring& key) const;
    bool ContentFocused() const;

    // fitting (hcwindow_fit.cpp)
    void FitAll();
    void FitConverter();
    void FitCalculator();
    void FitExpression();
public:
    // The width of a text at the font of a widget, scale 1 (what the fitting measures with).
    double TextWidth(GtkWidget* w, const std::wstring& text) const;
private:
    double FitScale(GtkWidget* w, const std::wstring& text, double avail, double maxScale, double minScale) const;
    void ApplyScale(GtkWidget* w, double scale, double* last);
    double DisplayWidth(int page) const;

    bool alive_ = true;
    GtkWidget* win_ = nullptr;
    std::wstring settingsPath_;
    hc::CalcEngine engine_;
    hc::settings::ThemeMode themeMode_ = hc::settings::ThemeMode::Auto;
    int decimals_ = 2;
    int tab_ = 0;
    bool windowActive_ = false;
    bool busy_ = false;
    int saves_ = 0;
    std::wstring announced_;
    bool savePlacesPending_ = false, saveThemePending_ = false;
    guint saveIdle_ = 0;

    // converter
    int active_ = 0;          // the field the keypad types into
    int source_ = 0;          // the field that was edited last: the other one is computed from it
    int focus_ = -1;          // the field that is the focus widget of the window (-1: none)
    FieldState saved_[2];     // caret and selection of a field when the keyboard left it (a menu or a dialog took it)
    bool hasSaved_[2] = { false, false };
    std::wstring caption_;

    // calculator
    std::wstring calcL_, calcR_, calcCap_, exprFull_, exprShown_;
    bool error_ = false;
    double calcFontMax_ = 1.0;

    // fitting
    double convScale_ = 1.0, calcScale_ = 1.0;
    double textScale_[2] = { -1.0, -1.0 }, calcValScale_[2] = { -1.0, -1.0 };      // -1: no attributes applied yet
    int displayW_[2] = { 0, 0 };

    // widgets
    GtkWidget* toolbar_ = nullptr;
    GtkWidget* header_ = nullptr;
    GtkWidget* switcher_ = nullptr;
    GtkWidget* menuBtn_ = nullptr;
    GtkWidget* stack_ = nullptr;
    GtkWidget* convDisplay_ = nullptr;
    GtkWidget* calcDisplay_ = nullptr;
    GtkWidget* convPad_ = nullptr;
    GtkWidget* calcPad_ = nullptr;
    GtkWidget* text_[2] = { nullptr, nullptr };
    GtkWidget* valueBox_[2] = { nullptr, nullptr };
    GtkWidget* capLabel_ = nullptr;
    GtkWidget* exprLabel_ = nullptr;
    GtkWidget* calcVal_[2] = { nullptr, nullptr };
    GtkWidget* calcValCol_[2] = { nullptr, nullptr };
    GtkWidget* calcCapLabel_ = nullptr;
    GtkWidget* fieldMenu_[2] = { nullptr, nullptr };      // the Cut / Copy / Paste popovers of the converter fields (made on first use)
    GtkWidget* copyMenu_[2] = { nullptr, nullptr };
    GMenu* menuModel_ = nullptr;
    GtkWidget* menuPopover_ = nullptr;
    AdwDialog* about_ = nullptr;
    int ctxField_ = 0;
    std::vector<KeyDef> convKeys_, calcKeys_;
    struct FieldCtx {
        HcWindow* w;
        int f;
    };
    FieldCtx ctx_[2];
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace hcgtk
