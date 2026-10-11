// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc GTK front end - building the window: header bar, view stack, display, keypad, actions, menu.

#include <cstring>

#include "hcwindow.h"
#include "util.h"

// ------------------------------------------------------------------ the display box
// A GtkBox that tells the window how wide it got, at the moment it is allocated: the values shrink their font to
// fit the columns, and that has to follow every size change in the same layout pass (no frame with clipped text).

extern "C" {
typedef struct _HcDisplayBox HcDisplayBox;
typedef struct _HcDisplayBoxClass HcDisplayBoxClass;
struct _HcDisplayBox {
    GtkBox parent_instance;
    void (*cb)(void*, int, int);
    void* data;
    int page;
};
struct _HcDisplayBoxClass {
    GtkBoxClass parent_class;
};
}

G_DEFINE_TYPE(HcDisplayBox, hc_display_box, GTK_TYPE_BOX)

static void hc_display_box_size_allocate(GtkWidget* widget, int width, int height, int baseline) {
    HcDisplayBox* self = reinterpret_cast<HcDisplayBox*>(widget);
    if (self->cb) self->cb(self->data, self->page, width);
    GTK_WIDGET_CLASS(hc_display_box_parent_class)->size_allocate(widget, width, height, baseline);
}

static void hc_display_box_class_init(HcDisplayBoxClass* klass) {
    GTK_WIDGET_CLASS(klass)->size_allocate = hc_display_box_size_allocate;
}

static void hc_display_box_init(HcDisplayBox* self) {
    self->cb = nullptr;
    self->data = nullptr;
    self->page = 0;
}

namespace hcgtk {

namespace {

const int kMargin = HcWindow::kPageMargin;
const int kGap = HcWindow::kColumnGap;
const int kKeyGap = 6;      // between the keys

// The look of the app on top of libadwaita: the display fonts, the underline of the active converter field and
// the keys. Colours are libadwaita's named colours, so the system accent and the light / dark scheme apply.
const char kCss[] = R"CSS(
.hc-value-box {
  font-size: 30pt;
  min-height: 1.5em;
  border-bottom: 2px solid transparent;
}
.hc-value-box.hc-active {
  border-bottom-color: @accent_bg_color;
}
.hc-value-box text {
  padding: 0;
  min-height: 0;
  min-width: 0;
  background: none;
  box-shadow: none;
  outline: none;
}
.hc-calc-value {
  font-size: 30pt;
  min-height: 1.5em;
  border-bottom: 2px solid transparent;
}
.hc-caption {
  font-size: 10pt;
  min-height: 1.7em;
}
.hc-expr {
  font-size: 11pt;
  min-height: 1.7em;
}
button.hc-key {
  padding: 0;
  min-width: 0;
  min-height: 0;
}
button.hc-digit {
  font-size: 15pt;
  font-weight: 500;
  background-color: @card_bg_color;
}
window:not(.hc-high-contrast) button.hc-digit {
  box-shadow: 0 0 0 1px @card_shade_color;
}
button.hc-func {
  font-size: 12pt;
  background-color: alpha(currentColor, 0.06);
}
button.hc-op {
  font-size: 18pt;
  font-weight: 500;
}
button.hc-accent {
  font-size: 14pt;
  font-weight: 600;
}
)CSS";

void InstallCssOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    GtkCssProvider* p = gtk_css_provider_new();
    gtk_css_provider_load_from_string(p, kCss);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

// Glyphs as UTF-8 escapes (the sources are ASCII only).
const char kDivide[] = "\xC3\xB7";            // U+00F7
const char kMultiply[] = "\xC3\x97";          // U+00D7
const char kMinus[] = "\xE2\x88\x92";         // U+2212

}  // namespace

void InstallCss() { InstallCssOnce(); }

// ------------------------------------------------------------------ keys

void HcWindow::BuildKeys() {
    convKeys_.clear();
    calcKeys_.clear();
    std::wstring sep = hc::timemath::DecimalSeparator();
    auto text = [this](std::vector<KeyDef>& v, int tab, const wchar_t* key, const std::wstring& label, KeyStyle st, int row, int col, int span,
                       const char* name) {
        KeyDef k;
        k.owner = this;
        k.tab = tab;
        k.key = key;
        k.label = label;
        k.icon = nullptr;
        k.style = st;
        k.row = row;
        k.col = col;
        k.span = span;
        k.name = name;
        k.button = nullptr;
        v.push_back(k);
    };
    auto icon = [this](std::vector<KeyDef>& v, int tab, const wchar_t* key, const char* iconName, KeyStyle st, int row, int col, const char* name) {
        KeyDef k;
        k.owner = this;
        k.tab = tab;
        k.key = key;
        k.icon = iconName;
        k.style = st;
        k.row = row;
        k.col = col;
        k.span = 1;
        k.name = name;
        k.button = nullptr;
        v.push_back(k);
    };
    const wchar_t* digits[9] = { L"7", L"8", L"9", L"4", L"5", L"6", L"1", L"2", L"3" };

    // Converter: 3 columns, 6 rows.
    text(convKeys_, 0, L"RESET", L"Reset", KeyFunc, 0, 0, 1, "Reset");
    text(convKeys_, 0, L"CE", L"CE", KeyFunc, 0, 1, 1, "Clear entry");
    icon(convKeys_, 0, L"BACK", "edit-clear-symbolic", KeyFunc, 0, 2, "Backspace");
    for (int i = 0; i < 9; i++) text(convKeys_, 0, digits[i], digits[i], KeyDigit, 1 + i / 3, i % 3, 1, nullptr);
    text(convKeys_, 0, L":", L":", KeyDigit, 4, 0, 1, "Time separator");
    text(convKeys_, 0, L"0", L"0", KeyDigit, 4, 1, 1, nullptr);
    text(convKeys_, 0, L",", sep, KeyDigit, 4, 2, 1, "Decimal separator");
    text(convKeys_, 0, L"CONVERT", L"Convert", KeyAccent, 5, 0, 3, nullptr);

    // Calculator: 4 columns, 5 rows.
    text(calcKeys_, 1, L"CE", L"CE", KeyFunc, 0, 0, 1, "Clear entry");
    text(calcKeys_, 1, L"C", L"C", KeyFunc, 0, 1, 1, "Clear");
    icon(calcKeys_, 1, L"BACK", "edit-clear-symbolic", KeyFunc, 0, 2, "Backspace");
    text(calcKeys_, 1, L"/", FromUtf8(kDivide), KeyOperator, 0, 3, 1, "Divide");
    for (int i = 0; i < 9; i++) text(calcKeys_, 1, digits[i], digits[i], KeyDigit, 1 + i / 3, i % 3, 1, nullptr);
    text(calcKeys_, 1, L"*", FromUtf8(kMultiply), KeyOperator, 1, 3, 1, "Multiply");
    text(calcKeys_, 1, L"-", FromUtf8(kMinus), KeyOperator, 2, 3, 1, "Subtract");
    text(calcKeys_, 1, L"+", L"+", KeyOperator, 3, 3, 1, "Add");
    text(calcKeys_, 1, L":", L":", KeyDigit, 4, 0, 1, "Time separator");
    text(calcKeys_, 1, L"0", L"0", KeyDigit, 4, 1, 1, nullptr);
    text(calcKeys_, 1, L",", sep, KeyDigit, 4, 2, 1, "Decimal separator");
    text(calcKeys_, 1, L"=", L"=", KeyAccent, 4, 3, 1, "Equals");
}

static void OnKeyClicked(GtkButton*, gpointer data) {
    const KeyDef* k = static_cast<const KeyDef*>(data);
    k->owner->PressKeyOfTab(k->tab, k->key);
}

GtkWidget* HcWindow::BuildKeypad(std::vector<KeyDef>& keys) {
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(grid), kKeyGap);
    gtk_grid_set_column_spacing(GTK_GRID(grid), kKeyGap);
    gtk_widget_set_hexpand(grid, TRUE);
    gtk_widget_set_vexpand(grid, TRUE);
    for (KeyDef& k : keys) {
        GtkWidget* b = gtk_button_new();
        if (k.icon) gtk_button_set_icon_name(GTK_BUTTON(b), k.icon);
        else gtk_button_set_label(GTK_BUTTON(b), ToUtf8(k.label).c_str());
        gtk_widget_add_css_class(b, "hc-key");
        switch (k.style) {
        case KeyDigit: gtk_widget_add_css_class(b, "hc-digit"); break;
        case KeyFunc: gtk_widget_add_css_class(b, "hc-func"); break;
        case KeyOperator:
            gtk_widget_add_css_class(b, "hc-func");
            gtk_widget_add_css_class(b, "hc-op");
            break;
        case KeyAccent:
            gtk_widget_add_css_class(b, "hc-accent");
            gtk_widget_add_css_class(b, "suggested-action");
            break;
        }
        gtk_widget_set_hexpand(b, TRUE);
        gtk_widget_set_vexpand(b, TRUE);
        // The keypad types into the active field and never takes the keyboard focus (SPEC 4).
        gtk_widget_set_focusable(b, FALSE);
        gtk_widget_set_focus_on_click(b, FALSE);
        if (k.name) {
            gtk_accessible_update_property(GTK_ACCESSIBLE(b), GTK_ACCESSIBLE_PROPERTY_LABEL, k.name, -1);
            // A tool tip that only repeats the label of the key (Reset) says nothing.
            if (k.icon || ToUtf8(k.label) != k.name) gtk_widget_set_tooltip_text(b, k.name);
        }
        g_signal_connect(b, "clicked", G_CALLBACK(OnKeyClicked), &k);
        k.button = b;
        gtk_grid_attach(GTK_GRID(grid), b, k.col, k.row, k.span, 1);
    }
    return grid;
}

// ------------------------------------------------------------------ converter

GtkWidget* HcWindow::NewValueBox(int field) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(box, "hc-value-box");
    GtkWidget* t = gtk_text_new();
    gtk_widget_set_hexpand(t, TRUE);
    gtk_editable_set_alignment(GTK_EDITABLE(t), 1.0f);
    gtk_editable_set_width_chars(GTK_EDITABLE(t), 1);
    gtk_editable_set_enable_undo(GTK_EDITABLE(t), FALSE);
    gtk_text_set_input_hints(GTK_TEXT(t), static_cast<GtkInputHints>(GTK_INPUT_HINT_NO_SPELLCHECK | GTK_INPUT_HINT_NO_EMOJI));
    gtk_text_set_enable_emoji_completion(GTK_TEXT(t), FALSE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(t), GTK_ACCESSIBLE_PROPERTY_LABEL, field == 0 ? "Hours and minutes" : "Decimal hours", -1);
    g_signal_connect(t, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
                         FieldCtx* c = static_cast<FieldCtx*>(data);
                         c->w->OnFieldChanged(c->f);
                     }),
                     &ctx_[field]);
    // A press on the field: a field without the focus selects all of its text instead of placing the caret, a double
    // click selects all (SPEC 4). Capture phase, so the GtkText sees nothing of a press that is used here.
    GtkGesture* left = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(left), GDK_BUTTON_PRIMARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(left), GTK_PHASE_CAPTURE);
    g_signal_connect(left, "pressed", G_CALLBACK(+[](GtkGestureClick* g, int nPress, double x, double y, gpointer data) {
                         FieldCtx* c = static_cast<FieldCtx*>(data);
                         if (c->w->OnFieldPressed(c->f, nPress, 1, x, y)) gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
                     }),
                     &ctx_[field]);
    gtk_widget_add_controller(t, GTK_EVENT_CONTROLLER(left));
    GtkGesture* right = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right), GDK_BUTTON_SECONDARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(right), GTK_PHASE_CAPTURE);
    g_signal_connect(right, "pressed", G_CALLBACK(+[](GtkGestureClick* g, int nPress, double x, double y, gpointer data) {
                         FieldCtx* c = static_cast<FieldCtx*>(data);
                         if (c->w->OnFieldPressed(c->f, nPress, 3, x, y)) gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
                     }),
                     &ctx_[field]);
    gtk_widget_add_controller(t, GTK_EVENT_CONTROLLER(right));
    gtk_box_append(GTK_BOX(box), t);
    text_[field] = t;
    valueBox_[field] = box;
    return box;
}

// The caption under a value ("8 h 30 min") wraps onto a second line where the text is large and the column narrow, rather
// than being cut off with an ellipsis; at the usual size it is one line.
static void SetCaptionWrap(GtkWidget* label) {
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_RIGHT);
}

static GtkWidget* NewColumnLabel(const char* text) {
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    gtk_widget_add_css_class(l, "caption");
    gtk_widget_add_css_class(l, "dimmed");
    return l;
}

static GtkWidget* NewDisplayBox(HcWindow* w, int page, void (*cb)(void*, int, int)) {
    HcDisplayBox* d = static_cast<HcDisplayBox*>(g_object_new(hc_display_box_get_type(), nullptr));
    d->cb = cb;
    d->data = w;
    d->page = page;
    GtkWidget* box = GTK_WIDGET(d);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(box), GTK_ORIENTATION_HORIZONTAL);
    gtk_box_set_spacing(GTK_BOX(box), kGap);
    gtk_box_set_homogeneous(GTK_BOX(box), TRUE);
    return box;
}

static void DisplayAllocated(void* data, int page, int width) {
    static_cast<HcWindow*>(data)->OnDisplayAllocated(page, width);
}

GtkWidget* HcWindow::BuildConverterPage() {
    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(page, kMargin);
    gtk_widget_set_margin_end(page, kMargin);
    gtk_widget_set_margin_top(page, 6);
    gtk_widget_set_margin_bottom(page, kMargin);

    convDisplay_ = NewDisplayBox(this, 0, DisplayAllocated);
    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(left), NewColumnLabel("Hours : minutes"));
    gtk_box_append(GTK_BOX(left), NewValueBox(0));
    capLabel_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(capLabel_), 1.0f);
    SetCaptionWrap(capLabel_);
    gtk_widget_add_css_class(capLabel_, "hc-caption");
    gtk_widget_add_css_class(capLabel_, "dimmed");
    gtk_box_append(GTK_BOX(left), capLabel_);
    GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(right), NewColumnLabel("Decimal hours"));
    gtk_box_append(GTK_BOX(right), NewValueBox(1));
    gtk_box_append(GTK_BOX(convDisplay_), left);
    gtk_box_append(GTK_BOX(convDisplay_), right);
    gtk_box_append(GTK_BOX(page), convDisplay_);

    convPad_ = BuildKeypad(convKeys_);
    gtk_widget_set_margin_top(convPad_, kMargin);
    gtk_box_append(GTK_BOX(page), convPad_);
    return page;
}

// ------------------------------------------------------------------ calculator

GtkWidget* HcWindow::NewCalcValue(int which) {
    GtkWidget* l = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(l), 1.0f);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(l, TRUE);
    gtk_widget_add_css_class(l, "hc-calc-value");
    // The value itself is the name of the label (an explicit label would replace it for a screen reader); what it
    // stands for is the description.
    gtk_accessible_update_property(GTK_ACCESSIBLE(l), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, which == 0 ? "Result in hours and minutes" : "Result in decimal hours", -1);
    GtkGesture* right = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right), GDK_BUTTON_SECONDARY);
    g_signal_connect(right, "pressed", G_CALLBACK(+[](GtkGestureClick* g, int, double x, double y, gpointer data) {
                         FieldCtx* c = static_cast<FieldCtx*>(data);
                         c->w->OpenCopyMenu(c->f, x, y);
                         gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
                     }),
                     &ctx_[which]);
    gtk_widget_add_controller(l, GTK_EVENT_CONTROLLER(right));
    calcVal_[which] = l;
    return l;
}

GtkWidget* HcWindow::BuildCalculatorPage() {
    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(page, kMargin);
    gtk_widget_set_margin_end(page, kMargin);
    gtk_widget_set_margin_top(page, 0);
    gtk_widget_set_margin_bottom(page, kMargin);

    exprLabel_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(exprLabel_), 1.0f);
    gtk_label_set_ellipsize(GTK_LABEL(exprLabel_), PANGO_ELLIPSIZE_START);
    gtk_widget_add_css_class(exprLabel_, "hc-expr");
    gtk_widget_add_css_class(exprLabel_, "dimmed");
    {
        PangoAttrList* tn = pango_attr_list_new();
        pango_attr_list_insert(tn, pango_attr_font_features_new("tnum"));
        gtk_label_set_attributes(GTK_LABEL(exprLabel_), tn);
        pango_attr_list_unref(tn);
    }
    gtk_box_append(GTK_BOX(page), exprLabel_);

    calcDisplay_ = NewDisplayBox(this, 1, DisplayAllocated);
    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(left), NewColumnLabel("Hours : minutes"));
    gtk_box_append(GTK_BOX(left), NewCalcValue(0));
    calcCapLabel_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(calcCapLabel_), 1.0f);
    SetCaptionWrap(calcCapLabel_);
    gtk_widget_add_css_class(calcCapLabel_, "hc-caption");
    gtk_widget_add_css_class(calcCapLabel_, "dimmed");
    gtk_box_append(GTK_BOX(left), calcCapLabel_);
    GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(right), NewColumnLabel("Decimal hours"));
    gtk_box_append(GTK_BOX(right), NewCalcValue(1));
    calcValCol_[0] = left;
    calcValCol_[1] = right;
    gtk_box_append(GTK_BOX(calcDisplay_), left);
    gtk_box_append(GTK_BOX(calcDisplay_), right);
    gtk_box_append(GTK_BOX(page), calcDisplay_);

    calcPad_ = BuildKeypad(calcKeys_);
    gtk_widget_set_margin_top(calcPad_, kMargin);
    gtk_box_append(GTK_BOX(page), calcPad_);
    return page;
}

// ------------------------------------------------------------------ actions and menu

static HcWindow* Self(gpointer data) { return static_cast<HcWindow*>(data); }

void HcWindow::BuildActions() {
    static const GActionEntry entries[] = {
        { "theme", nullptr, "s", "'auto'",
          [](GSimpleAction*, GVariant* v, gpointer d) {
              const char* s = g_variant_get_string(v, nullptr);
              hc::settings::ThemeMode m = hc::settings::ThemeMode::Auto;
              if (strcmp(s, "light") == 0) m = hc::settings::ThemeMode::Light;
              else if (strcmp(s, "dark") == 0) m = hc::settings::ThemeMode::Dark;
              Self(d)->SetThemeMode(m);
          },
          { 0, 0, 0 } },
        { "places", nullptr, "i", "2", [](GSimpleAction*, GVariant* v, gpointer d) { Self(d)->SetDecimalPlaces(g_variant_get_int32(v)); }, { 0, 0, 0 } },
        { "about", [](GSimpleAction*, GVariant*, gpointer d) { Self(d)->ShowAbout(); }, nullptr, nullptr, nullptr, { 0, 0, 0 } },
        { "field-cut", [](GSimpleAction*, GVariant*, gpointer d) { Self(d)->CutField(Self(d)->ContextField()); }, nullptr, nullptr, nullptr, { 0, 0, 0 } },
        { "field-copy", [](GSimpleAction*, GVariant*, gpointer d) { Self(d)->CopyField(Self(d)->ContextField()); }, nullptr, nullptr, nullptr, { 0, 0, 0 } },
        { "field-paste", [](GSimpleAction*, GVariant*, gpointer d) { Self(d)->PasteField(Self(d)->ContextField()); }, nullptr, nullptr, nullptr, { 0, 0, 0 } },
        { "copy-main", [](GSimpleAction*, GVariant*, gpointer d) { Self(d)->CopyCalc(0); }, nullptr, nullptr, nullptr, { 0, 0, 0 } },
        { "copy-decimal", [](GSimpleAction*, GVariant*, gpointer d) { Self(d)->CopyCalc(1); }, nullptr, nullptr, nullptr, { 0, 0, 0 } },
    };
    g_action_map_add_action_entries(G_ACTION_MAP(win_), entries, G_N_ELEMENTS(entries), this);
}

GMenu* HcWindow::BuildMenuModel() {
    GMenu* root = g_menu_new();
    GMenu* theme = g_menu_new();
    g_menu_append(theme, "Auto (Follow System)", "win.theme::auto");
    g_menu_append(theme, "Light", "win.theme::light");
    g_menu_append(theme, "Dark", "win.theme::dark");
    g_menu_append_section(root, "Theme", G_MENU_MODEL(theme));
    g_object_unref(theme);
    GMenu* places = g_menu_new();
    for (int n = hc::timemath::MinDisplayDecimals; n <= hc::timemath::MaxDisplayDecimals; n++) {
        std::string label = n == hc::timemath::DefaultDecimals ? std::to_string(n) + " (Default)" : std::to_string(n);
        std::string action = "win.places(" + std::to_string(n) + ")";
        g_menu_append(places, label.c_str(), action.c_str());
    }
    g_menu_append_section(root, "Decimal Places", G_MENU_MODEL(places));
    g_object_unref(places);
    GMenu* about = g_menu_new();
    g_menu_append(about, "About HourCalc", "win.about");
    g_menu_append_section(root, nullptr, G_MENU_MODEL(about));
    g_object_unref(about);
    return root;
}

GMenuModel* HcWindow::MenuModel() {
    if (!menuModel_) menuModel_ = BuildMenuModel();
    return G_MENU_MODEL(menuModel_);
}

// ------------------------------------------------------------------ the window

void HcWindow::BuildWidgets(GtkApplication* app) {
    win_ = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win_), "HourCalc");
    gtk_window_set_default_size(GTK_WINDOW(win_), kDefaultWidth, kDefaultHeight);
    // The minimum size: 360 px wide (the width libadwaita's dialogs are made for: its About dialog does not
    // fit a narrower window) and 480 px high.
    gtk_widget_set_size_request(win_, kMinWidth, kMinHeight);
    g_object_set_data_full(G_OBJECT(win_), "hc-window", this, [](gpointer p) { delete static_cast<HcWindow*>(p); });
    ctx_[0] = { this, 0 };
    ctx_[1] = { this, 1 };

    BuildActions();
    SetThemeAction(themeMode_);
    SetPlacesAction(decimals_);

    GtkWidget* toolbar = adw_toolbar_view_new();
    toolbar_ = toolbar;
    header_ = adw_header_bar_new();
    stack_ = adw_view_stack_new();

    GtkWidget* convPage = BuildConverterPage();
    GtkWidget* calcPage = BuildCalculatorPage();
    adw_view_stack_add_titled(ADW_VIEW_STACK(stack_), convPage, "converter", "Converter");
    adw_view_stack_add_titled(ADW_VIEW_STACK(stack_), calcPage, "calculator", "Calculator");

    // The view switcher is the title of the header bar. The wide AdwViewSwitcher (icon and label side by side) needs
    // about 330 px next to the menu button and the window buttons and would cut its labels at the 300 px minimum
    // width; the inline switcher (libadwaita 1.7) shows the two labels only and keeps its pill shape.
    switcher_ = adw_inline_view_switcher_new();
    adw_inline_view_switcher_set_stack(ADW_INLINE_VIEW_SWITCHER(switcher_), ADW_VIEW_STACK(stack_));
    adw_inline_view_switcher_set_display_mode(ADW_INLINE_VIEW_SWITCHER(switcher_), ADW_INLINE_VIEW_SWITCHER_LABELS);
    adw_inline_view_switcher_set_homogeneous(ADW_INLINE_VIEW_SWITCHER(switcher_), TRUE);
    adw_inline_view_switcher_set_can_shrink(ADW_INLINE_VIEW_SWITCHER(switcher_), FALSE);       // the labels are never cut off
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header_), switcher_);

    // The menu is built on its first use (the popover with its items costs start-up time for nothing).
    menuBtn_ = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menuBtn_), "open-menu-symbolic");
    gtk_menu_button_set_primary(GTK_MENU_BUTTON(menuBtn_), TRUE);
    gtk_widget_set_tooltip_text(menuBtn_, "Main Menu");
    gtk_accessible_update_property(GTK_ACCESSIBLE(menuBtn_), GTK_ACCESSIBLE_PROPERTY_LABEL, "Main Menu", -1);
    gtk_menu_button_set_create_popup_func(
        GTK_MENU_BUTTON(menuBtn_), [](GtkMenuButton*, gpointer d) { Self(d)->MenuPopover(); }, this, nullptr);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header_), menuBtn_);
    g_signal_connect(menuBtn_, "notify::active", G_CALLBACK(+[](GObject* o, GParamSpec*, gpointer d) {
                         if (!gtk_menu_button_get_active(GTK_MENU_BUTTON(o))) Self(d)->ScheduleRefocus();
                     }),
                     this);

    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header_);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), stack_);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(win_), toolbar);

    // Keys: one capture controller on the window (SPEC 4-5: shortcuts, Enter, Esc, Tab, calculator text).
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed",
                     G_CALLBACK(+[](GtkEventControllerKey* c, guint keyval, guint, GdkModifierType state, gpointer d) -> gboolean {
                         return Self(d)->HandleKey(keyval, state, gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(c)));
                     }),
                     this);
    g_object_set_data(G_OBJECT(keys), "hc-keys", GINT_TO_POINTER(1));
    gtk_widget_add_controller(win_, keys);

    g_signal_connect(stack_, "notify::visible-child-name", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer d) { Self(d)->OnTabChanged(); }), this);
    g_signal_connect(win_, "notify::focus-widget", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer d) { Self(d)->OnFocusWidgetChanged(); }), this);
    g_signal_connect(win_, "notify::is-active", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer d) { Self(d)->OnActiveChanged(); }), this);
    g_signal_connect(win_, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer d) { Self(d)->OnDestroyed(); }), this);
    // High contrast: libadwaita draws its own borders then, and the keys must not paint over them (see the CSS).
    g_signal_connect_object(adw_style_manager_get_default(), "notify::high-contrast",
                            G_CALLBACK(+[](GObject*, GParamSpec*, gpointer window) {
                                HcWindow* w = HcWindow::Of(GTK_WINDOW(window));
                                if (w) w->UpdateHighContrast();
                            }),
                            win_, G_CONNECT_DEFAULT);
    UpdateHighContrast();

    gtk_window_set_focus(GTK_WINDOW(win_), text_[0]);
}

}  // namespace hcgtk
