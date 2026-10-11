// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// --selftest: the behaviour of SPEC 4-6b on the real widgets (offscreen), once for every decimal separator
// (',' and '.'): the converter (typing, filtering, focus and select-all rules, the keypad, Convert), the
// calculator (the keypad, the keyboard, the engine mirrored on the display, copy and paste), the menu
// (decimal places, theme, Keep on top), the settings file, forced and followed themes, the About dialog,
// and the layout at several sizes (nothing clipped). Real key and mouse events are used where the behaviour
// is that of the widget (QTest), direct calls where it is that of the page.

#include "testhooks.h"

#include "aboutdialog.h"
#include "calculatorpage.h"
#include "converterpage.h"
#include "displaywidgets.h"
#include "keybutton.h"
#include "layoutconst.h"
#include "mainwindow.h"
#include "scheme.h"
#include "util.h"

#include "hc_core.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QStyleHints>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolButton>
#include <QToolTip>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace hcqt {
namespace testing {

namespace {

// ------------------------------------------------------------------ the little test framework

int g_checks = 0;
int g_failures = 0;
QString g_tag;                // which run: the decimal separator
QString g_sep = QStringLiteral(",");

void report(const QString& what, const QString& detail) {
    ++g_failures;
    std::fprintf(stderr, "FAIL [%s] %s: %s\n", g_tag.toLocal8Bit().constData(), what.toLocal8Bit().constData(), detail.toLocal8Bit().constData());
}

void eq(const QString& what, const QString& expected, const QString& actual) {
    ++g_checks;
    if (expected != actual) report(what, QStringLiteral("expected <%1> got <%2>").arg(expected, actual));
}

void yes(const QString& what, bool value) {
    ++g_checks;
    if (!value) report(what, QStringLiteral("not true"));
}

QString I(qint64 v) { return QString::number(v); }

// An expected text in the culture of the run: the ',' of the tables is the decimal separator.
QString S(QString text) { return text.replace(QLatin1Char(','), g_sep); }

QString E(const std::wstring& s) { return q(s); }

// ------------------------------------------------------------------ windows and input

using Win = std::unique_ptr<MainWindow>;

void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
}

// A window on the offscreen platform, shown and active, so focus, selection and shortcuts work as they do
// for a user. 'keepOnTop' as for the constructor.
Win newWin(const QString& settings = QString(), int keepOnTop = 0) {
    Win w = std::make_unique<MainWindow>(settings, keepOnTop);
    w->show();
    w->activateWindow();
    const bool active = QTest::qWaitForWindowActive(w.get());
    yes(QStringLiteral("the test window becomes active"), active);
    settle();
    return w;
}

QWidget* target(MainWindow& w) {
    QWidget* f = QApplication::focusWidget();
    return f ? f : &w;
}

// Characters as the keyboard delivers them: one key event each, to the widget that has the focus (the window when
// none has). The key code is not set: the text is what counts, whatever the layout.
void sendText(MainWindow& w, const QString& text) {
    for (QChar c : text) {
        QWidget* t = target(w);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(c));
        QApplication::sendEvent(t, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, QString(c));
        QApplication::sendEvent(t, &release);
    }
}

void key(MainWindow& w, Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::keyClick(target(w), k, m); }

// The keypad: every character is a key of the current tab.
void keys(MainWindow& w, const QString& text) {
    for (QChar c : text) w.press(QString(c));
}

QString L(MainWindow& w) { return w.converter()->fieldText(0); }
QString R(MainWindow& w) { return w.converter()->fieldText(1); }
QString cap(MainWindow& w) { return w.converter()->caption(); }

// A field gets the focus the way Tab gives it: with its text selected.
void focusField(MainWindow& w, int field) {
    w.converter()->edit(field)->setFocus(Qt::TabFocusReason);
    settle();
}

// What a paste into a field does: the text replaces the selection.
void insertInto(MainWindow& w, int field, const QString& text) { w.converter()->edit(field)->insert(text); }

QString state(hc::CalcEngine& e) { return E(e.MainText()) + QLatin1Char('|') + E(e.Expression()) + QLatin1Char('|') + E(e.DecimalText()); }

// ------------------------------------------------------------------ converter

void check(MainWindow& w, int field, const QString& input, const QString& expectedOther) {
    w.press(QStringLiteral("RESET"));
    w.converter()->setFieldText(field, input);
    eq(QStringLiteral("field %1 <%2> other").arg(field).arg(input), expectedOther, field == 0 ? R(w) : L(w));
}

// The same example typed on the keyboard, a key at a time.
void checkTyped(MainWindow& w, int field, const QString& input, const QString& expectedOther) {
    w.press(QStringLiteral("RESET"));
    focusField(w, field);
    sendText(w, input);
    eq(QStringLiteral("field %1 <%2> typed, other").arg(field).arg(input), expectedOther, field == 0 ? R(w) : L(w));
}

void converterWiring() {
    Win wp = newWin();
    MainWindow& w = *wp;
    ConverterPage* c = w.converter();
    eq(QStringLiteral("starts on the converter"), QStringLiteral("0"), I(w.currentTab()));
    eq(QStringLiteral("left field is active at start"), QStringLiteral("0"), I(c->activeField()));
    eq(QStringLiteral("left field has the keyboard focus at start"), QStringLiteral("0"), I(c->focusField()));
    eq(QStringLiteral("left empty"), QString(), L(w));
    eq(QStringLiteral("right empty"), QString(), R(w));

    keys(w, QStringLiteral("830"));
    eq(QStringLiteral("keypad left"), QStringLiteral("830"), L(w));
    eq(QStringLiteral("keypad right follows"), S(QStringLiteral("8,50")), R(w));
    eq(QStringLiteral("caption"), QStringLiteral("8 h 30 min"), cap(w));
    w.press(QStringLiteral("CONVERT"));
    eq(QStringLiteral("convert normalizes left"), QStringLiteral("08:30"), L(w));
    eq(QStringLiteral("convert keeps right"), S(QStringLiteral("8,50")), R(w));

    w.press(QStringLiteral("RESET"));
    keys(w, QStringLiteral("8,"));
    eq(QStringLiteral("',' is ignored while left is active"), QStringLiteral("8"), L(w));
    keys(w, QStringLiteral(":3"));
    eq(QStringLiteral("':' accepted on the left"), QStringLiteral("8:3"), L(w));
    eq(QStringLiteral("right follows 8:3"), S(QStringLiteral("8,05")), R(w));
    w.press(QStringLiteral("0"));
    eq(QStringLiteral("8:30"), QStringLiteral("8:30"), L(w));
    eq(QStringLiteral("8:30 right"), S(QStringLiteral("8,50")), R(w));
    w.press(QStringLiteral("5"));
    eq(QStringLiteral("third minute digit is rejected"), QStringLiteral("8:30"), L(w));

    c->setFieldText(1, S(QStringLiteral("1,5")));
    eq(QStringLiteral("right made active"), QStringLiteral("1"), I(c->activeField()));
    w.press(QStringLiteral("RESET"));
    eq(QStringLiteral("reset left"), QString(), L(w));
    eq(QStringLiteral("reset right"), QString(), R(w));
    eq(QStringLiteral("reset caption"), QString(), cap(w));
    eq(QStringLiteral("reset activates left"), QStringLiteral("0"), I(c->activeField()));
    eq(QStringLiteral("reset gives the left field the keyboard focus"), QStringLiteral("0"), I(c->focusField()));

    c->setFieldText(1, QString());
    keys(w, S(QStringLiteral("8,50")));
    eq(QStringLiteral("keypad right"), S(QStringLiteral("8,50")), R(w));
    eq(QStringLiteral("left follows"), QStringLiteral("08:30"), L(w));
    w.press(QStringLiteral(":"));
    eq(QStringLiteral("':' is ignored while right is active"), S(QStringLiteral("8,50")), R(w));
    w.press(QStringLiteral(","));
    eq(QStringLiteral("second ',' is ignored"), S(QStringLiteral("8,50")), R(w));

    w.press(QStringLiteral("BACK"));
    eq(QStringLiteral("backspace right"), S(QStringLiteral("8,5")), R(w));
    eq(QStringLiteral("backspace left follows"), QStringLiteral("08:30"), L(w));
    w.press(QStringLiteral("BACK"));
    w.press(QStringLiteral("BACK"));
    eq(QStringLiteral("backspace to the separator"), QStringLiteral("8"), R(w));
    w.press(QStringLiteral("CE"));
    eq(QStringLiteral("CE clears the active field"), QString(), R(w));
    eq(QStringLiteral("CE clears the other field"), QString(), L(w));
    eq(QStringLiteral("CE clears the caption"), QString(), cap(w));

    c->setFieldText(1, S(QStringLiteral("8,5")));
    w.press(QStringLiteral("CONVERT"));
    eq(QStringLiteral("convert normalizes right"), S(QStringLiteral("8,50")), R(w));
    eq(QStringLiteral("convert left"), QStringLiteral("08:30"), L(w));
    c->setFieldText(0, QStringLiteral("0:90"));
    w.press(QStringLiteral("CONVERT"));
    eq(QStringLiteral("convert 0:90"), QStringLiteral("01:30"), L(w));
    eq(QStringLiteral("convert 0:90 right"), S(QStringLiteral("1,50")), R(w));

    // SPEC 5: the converter examples
    struct Row {
        int field;
        const char* in;
        const char* out;
    };
    const Row rows[] = {{0, "08:30", "8,50"}, {1, "8,50", "08:30"}, {1, "8,5", "08:30"}, {0, "08:20", "8,33"}, {1, "8,33", "08:20"},
                        {0, "830", "8,50"},   {0, "8.30", "8,50"},  {0, "8u30", "8,50"},  {0, "0:45", "0,75"},  {1, "0,1", "00:06"},
                        {0, "123:45", "123,75"}, {1, "8,51", "08:31"}, {0, "0:90", "1,50"}};
    for (const Row& r : rows) {
        const QString in = r.field == 1 ? S(QLatin1String(r.in)) : QLatin1String(r.in);
        const QString out = r.field == 0 ? S(QLatin1String(r.out)) : QLatin1String(r.out);
        check(w, r.field, in, out);
        checkTyped(w, r.field, in, out);
    }
}

void converterEditing() {
    Win wp = newWin();
    MainWindow& w = *wp;
    ConverterPage* c = w.converter();
    yes(QStringLiteral("colon key exists"), c->key(QStringLiteral(":")) != nullptr);
    yes(QStringLiteral("decimal key exists"), c->key(QStringLiteral(",")) != nullptr);
    yes(QStringLiteral("colon enabled when left is active"), c->keyEnabled(QStringLiteral(":")));
    yes(QStringLiteral("decimal disabled when left is active"), !c->keyEnabled(QStringLiteral(",")));
    c->setFieldText(1, QString());
    yes(QStringLiteral("colon disabled when right is active"), !c->keyEnabled(QStringLiteral(":")));
    yes(QStringLiteral("decimal enabled when right is active"), c->keyEnabled(QStringLiteral(",")));
    eq(QStringLiteral("decimal key label is the culture's separator"), g_sep, c->key(QStringLiteral(","))->label());
    w.press(QStringLiteral("RESET"));
    yes(QStringLiteral("reset re-enables the colon"), c->keyEnabled(QStringLiteral(":")));

    c->setFieldText(0, QStringLiteral("8,30"));
    eq(QStringLiteral("comma ignored in the left field"), QStringLiteral("830"), L(w));
    c->setFieldText(0, QStringLiteral("8h30"));
    eq(QStringLiteral("h becomes a colon"), QStringLiteral("8:30"), L(w));
    c->setFieldText(1, QStringLiteral("8:50"));
    eq(QStringLiteral("colon ignored in the right field"), QStringLiteral("850"), R(w));
    c->setFieldText(1, QStringLiteral("8.5"));
    eq(QStringLiteral("dot shown as the culture's separator"), S(QStringLiteral("8,5")), R(w));
    c->setFieldText(1, QStringLiteral("0,1234567"));
    eq(QStringLiteral("at most 6 decimals"), S(QStringLiteral("0,123456")), R(w));
    c->setFieldText(0, QStringLiteral("abc"));
    eq(QStringLiteral("letters rejected"), QString(), L(w));
    eq(QStringLiteral("nothing to convert"), QString(), R(w));
}

// What the field does with text typed into it: the filter, the caret, the other field.
void converterTyping() {
    Win wp = newWin();
    MainWindow& w = *wp;
    ConverterPage* c = w.converter();
    DisplayEdit* bl = c->edit(0);
    DisplayEdit* br = c->edit(1);

    focusField(w, 0);
    insertInto(w, 0, QStringLiteral("8u30"));
    eq(QStringLiteral("left typed 8u30"), QStringLiteral("8:30"), bl->text());
    eq(QStringLiteral("left typed 8u30 caret"), QStringLiteral("4"), I(bl->cursorPosition()));
    eq(QStringLiteral("left typed 8u30 right"), S(QStringLiteral("8,50")), br->text());
    eq(QStringLiteral("left typed 8u30 caption"), QStringLiteral("8 h 30 min"), cap(w));
    insertInto(w, 0, QStringLiteral("5"));
    eq(QStringLiteral("third minute digit rejected, text"), QStringLiteral("8:30"), bl->text());
    eq(QStringLiteral("third minute digit rejected, caret"), QStringLiteral("4"), I(bl->cursorPosition()));

    struct Case {
        const char* in;
        const char* text;
    };
    for (const Case& k : {Case{"8.30", "8:30"}, Case{"8h30", "8:30"}, Case{"8,30", "830"}, Case{"12345678", "1234567"}}) {
        w.press(QStringLiteral("RESET"));
        insertInto(w, 0, QLatin1String(k.in));
        eq(QStringLiteral("left typed %1").arg(QLatin1String(k.in)), QLatin1String(k.text), bl->text());
    }
    w.press(QStringLiteral("RESET"));
    insertInto(w, 0, QStringLiteral("8x:3"));
    eq(QStringLiteral("left typed 8x:3: letters ignored"), QStringLiteral("8:3"), bl->text());
    eq(QStringLiteral("left typed 8x:3: caret"), QStringLiteral("3"), I(bl->cursorPosition()));

    w.press(QStringLiteral("RESET"));
    focusField(w, 1);
    eq(QStringLiteral("right made active by focus"), QStringLiteral("1"), I(c->activeField()));
    insertInto(w, 1, QStringLiteral("8.5"));
    eq(QStringLiteral("right typed 8.5"), S(QStringLiteral("8,5")), br->text());
    eq(QStringLiteral("right typed 8.5 caret"), QStringLiteral("3"), I(br->cursorPosition()));
    eq(QStringLiteral("right typed 8.5 left"), QStringLiteral("08:30"), bl->text());
    w.press(QStringLiteral("RESET"));
    focusField(w, 1);
    insertInto(w, 1, QStringLiteral("8,5"));
    eq(QStringLiteral("right typed 8,5"), S(QStringLiteral("8,5")), br->text());
    eq(QStringLiteral("right typed 8,5 left"), QStringLiteral("08:30"), bl->text());
    w.press(QStringLiteral("RESET"));
    focusField(w, 1);
    insertInto(w, 1, QStringLiteral("8:30"));
    eq(QStringLiteral("right typed 8:30: ':' is ignored"), QStringLiteral("830"), br->text());
    w.press(QStringLiteral("RESET"));
    focusField(w, 1);
    insertInto(w, 1, QStringLiteral("1,12345678"));
    eq(QStringLiteral("right typed 8 decimals"), S(QStringLiteral("1,123456")), br->text());
    w.press(QStringLiteral("RESET"));
    focusField(w, 1);
    insertInto(w, 1, QStringLiteral(",5"));
    eq(QStringLiteral("right typed ,5"), S(QStringLiteral(",5")), br->text());
    eq(QStringLiteral("right typed ,5 left"), QStringLiteral("00:30"), bl->text());

    // in the middle of the text, and over a selection
    w.press(QStringLiteral("RESET"));
    focusField(w, 0);
    insertInto(w, 0, QStringLiteral("830"));
    bl->setCursorPosition(1);
    insertInto(w, 0, QStringLiteral(":"));
    eq(QStringLiteral("':' typed in the middle"), QStringLiteral("8:30"), bl->text());
    eq(QStringLiteral("':' typed in the middle caret"), QStringLiteral("2"), I(bl->cursorPosition()));
    insertInto(w, 0, QStringLiteral("5"));
    eq(QStringLiteral("digit in the middle pushes the rest on, 2 minute digits at most"), QStringLiteral("8:53"), bl->text());
    bl->selectAll();
    insertInto(w, 0, QStringLiteral("9"));
    eq(QStringLiteral("typing over a selection"), QStringLiteral("9"), bl->text());
    eq(QStringLiteral("typing over a selection: right"), S(QStringLiteral("9,00")), br->text());
    bl->selectAll();
    key(w, Qt::Key_Delete);
    eq(QStringLiteral("deleting the selection clears the left"), QString(), bl->text());
    eq(QStringLiteral("deleting the selection clears the right"), QString(), br->text());
    eq(QStringLiteral("deleting the selection clears the caption"), QString(), cap(w));

    w.press(QStringLiteral("RESET"));
    focusField(w, 0);
    insertInto(w, 0, QStringLiteral("8:30"));
    eq(QStringLiteral("left source gives the right"), S(QStringLiteral("8,50")), br->text());
    focusField(w, 1);
    insertInto(w, 1, QStringLiteral("9"));
    eq(QStringLiteral("typing over the selected right value"), QStringLiteral("9"), br->text());
    eq(QStringLiteral("the left follows the right"), QStringLiteral("09:00"), bl->text());
    eq(QStringLiteral("caption follows the right"), QStringLiteral("9 h 0 min"), cap(w));

    // editing keys of the field itself
    w.press(QStringLiteral("RESET"));
    focusField(w, 0);
    sendText(w, QStringLiteral("830"));
    key(w, Qt::Key_Home);
    eq(QStringLiteral("Home moves the caret to the start"), QStringLiteral("0"), I(bl->cursorPosition()));
    key(w, Qt::Key_Right);
    key(w, Qt::Key_Delete);
    eq(QStringLiteral("Delete removes the character after the caret"), QStringLiteral("80"), L(w));
    key(w, Qt::Key_End, Qt::ShiftModifier);
    eq(QStringLiteral("Shift+End selects to the end"), QStringLiteral("1"), I(bl->selectedText().size()));
    key(w, Qt::Key_A, Qt::ControlModifier);
    eq(QStringLiteral("Ctrl+A selects all"), QStringLiteral("2"), I(bl->selectedText().size()));
    key(w, Qt::Key_Backspace);
    eq(QStringLiteral("Backspace deletes the selection"), QString(), L(w));
    key(w, Qt::Key_Tab);
    eq(QStringLiteral("Tab moves the focus to the other field"), QStringLiteral("1"), I(c->focusField()));
    key(w, Qt::Key_Tab, Qt::ShiftModifier);
    eq(QStringLiteral("Shift+Tab moves it back"), QStringLiteral("0"), I(c->focusField()));
}

// The active field, the keypad, and what the keypad does to a selection.
void converterState() {
    Win wp = newWin();
    MainWindow& w = *wp;
    ConverterPage* c = w.converter();
    auto expectActive = [&](const char* step, int field) {
        const QString s = QLatin1String(step);
        eq(s + QStringLiteral(": active field"), I(field), I(c->activeField()));
        yes(s + QStringLiteral(": ':' enabled only for the left field"), c->keyEnabled(QStringLiteral(":")) == (field == 0));
        yes(s + QStringLiteral(": decimal key enabled only for the right field"), c->keyEnabled(QStringLiteral(",")) == (field == 1));
        yes(s + QStringLiteral(": underline left"), c->edit(0)->underline() == (field == 0));
        yes(s + QStringLiteral(": underline right"), c->edit(1)->underline() == (field == 1));
    };
    expectActive("start", 0);
    c->setFieldText(1, S(QStringLiteral("8,5")));
    expectActive("after typing in the right field", 1);
    w.press(QStringLiteral("CE"));
    expectActive("after CE", 1);
    w.press(QStringLiteral("5"));
    w.press(QStringLiteral("BACK"));
    expectActive("after BACK", 1);
    w.press(QStringLiteral("CONVERT"));
    expectActive("after Convert", 1);
    w.selectTab(1);
    w.press(QStringLiteral("7"));
    w.selectTab(0);
    expectActive("after a tab round trip", 1);
    eq(QStringLiteral("a tab round trip gives the active field the focus"), QStringLiteral("1"), I(c->focusField()));
    w.press(QStringLiteral("RESET"));
    expectActive("after Reset", 0);
    focusField(w, 1);
    expectActive("after the right field got focus", 1);
    focusField(w, 0);
    expectActive("after the left field got focus", 0);

    w.press(QStringLiteral("RESET"));
    w.press(QStringLiteral(","));
    eq(QStringLiteral("',' ignored while left is active"), QString(), L(w));
    w.press(QStringLiteral("8"));
    w.press(QStringLiteral(":"));
    w.press(QStringLiteral(":"));
    w.press(QStringLiteral("3"));
    eq(QStringLiteral("keypad left"), QStringLiteral("8:3"), L(w));
    w.press(QStringLiteral("RESET"));
    focusField(w, 1);
    w.press(QStringLiteral(":"));
    eq(QStringLiteral("':' ignored while right is active"), QString(), R(w));
    w.press(QStringLiteral(","));
    w.press(QStringLiteral("5"));
    eq(QStringLiteral("keypad right: lone ',' then 5"), S(QStringLiteral(",5")), R(w));
    eq(QStringLiteral("keypad right: left follows"), QStringLiteral("00:30"), L(w));
    w.press(QStringLiteral(","));
    eq(QStringLiteral("second ',' ignored"), S(QStringLiteral(",5")), R(w));
    for (int i = 0; i < 8; ++i) w.press(QStringLiteral("7"));
    eq(QStringLiteral("keypad right stops at 6 decimals"), S(QStringLiteral(",577777")), R(w));
    w.press(QStringLiteral("RESET"));
    for (int i = 0; i < 9; ++i) w.press(QStringLiteral("9"));
    eq(QStringLiteral("keypad left stops at 7 hour digits"), QStringLiteral("9999999"), L(w));

    w.press(QStringLiteral("RESET"));
    c->setFieldText(0, QStringLiteral("8:30"));
    focusField(w, 1);
    eq(QStringLiteral("focus selects all of the right field"), I(R(w).size()), I(c->edit(1)->selectedText().size()));
    w.press(QStringLiteral("9"));
    eq(QStringLiteral("keypad digit replaces the selected value"), QStringLiteral("9"), R(w));
    eq(QStringLiteral("and the left follows"), QStringLiteral("09:00"), L(w));
    focusField(w, 0);
    eq(QStringLiteral("focus selects all of the left field"), I(L(w).size()), I(c->edit(0)->selectedText().size()));
    w.press(QStringLiteral("BACK"));
    eq(QStringLiteral("BACK deletes a selection"), QString(), L(w));
    eq(QStringLiteral("BACK with the left selection clears the right"), QString(), R(w));

    // a calculator error does not touch the converter's keys
    w.selectTab(1);
    keys(w, QStringLiteral("7:30/0="));
    w.selectTab(0);
    w.press(QStringLiteral("RESET"));
    yes(QStringLiteral("converter ':' key unaffected by a calculator error"), c->keyEnabled(QStringLiteral(":")));
}

// Focus and selection by mouse, Tab and window activation (SPEC 4).
void converterFocusRules() {
    Win wp = newWin();
    MainWindow& w = *wp;
    ConverterPage* c = w.converter();
    DisplayEdit* bl = c->edit(0);
    DisplayEdit* br = c->edit(1);

    // The window was deactivated and activated again: the caret stays, nothing is selected.
    focusField(w, 0);
    insertInto(w, 0, QStringLiteral("8:3"));
    bl->setCursorPosition(3);
    {
        QWidget other;
        other.show();
        other.activateWindow();
        yes(QStringLiteral("another window becomes active"), QTest::qWaitForWindowActive(&other));
        yes(QStringLiteral("the main window is not active then"), !w.isActiveWindow());
        w.activateWindow();
        yes(QStringLiteral("the main window becomes active again"), QTest::qWaitForWindowActive(&w));
        settle();
    }
    yes(QStringLiteral("re-activating the window gives the field the focus back"), bl->hasFocus());
    eq(QStringLiteral("re-activating the window keeps the caret (nothing selected)"), QStringLiteral("0"), I(bl->selectedText().size()));
    w.press(QStringLiteral("0"));
    eq(QStringLiteral("... so a keypad digit continues the typed text"), QStringLiteral("8:30"), L(w));
    eq(QStringLiteral("... and the other field follows"), S(QStringLiteral("8,50")), R(w));

    // Tab from the other field selects all.
    key(w, Qt::Key_Tab);
    eq(QStringLiteral("Tab goes to the right field"), QStringLiteral("1"), I(c->focusField()));
    eq(QStringLiteral("Tab selects all of the field"), R(w), br->selectedText());
    w.press(QStringLiteral("9"));
    eq(QStringLiteral("... so the next key replaces the value"), QStringLiteral("9"), R(w));
    eq(QStringLiteral("... and the left follows"), QStringLiteral("09:00"), L(w));

    // A click on the other field selects all of it.
    w.press(QStringLiteral("RESET"));
    c->setFieldText(0, QStringLiteral("8:30"));
    bl->setFocus(Qt::OtherFocusReason);
    bl->setCursorPosition(2);
    QTest::mouseClick(br, Qt::LeftButton, Qt::NoModifier, QPoint(br->width() / 2, br->height() / 2));
    settle();
    eq(QStringLiteral("a click on the other field gives it the focus"), QStringLiteral("1"), I(c->focusField()));
    eq(QStringLiteral("a click on the other field selects all of it"), R(w), br->selectedText());
    w.press(QStringLiteral("7"));
    eq(QStringLiteral("... and the next key replaces it"), QStringLiteral("7"), R(w));
    eq(QStringLiteral("... the left field follows"), QStringLiteral("07:00"), L(w));

    // A click on the field that has the focus places the caret (a click right after the window was activated
    // counts as the click that activated it).
    QTest::qWait(250);
    c->setFieldText(0, QStringLiteral("8:30"));
    bl->setFocus(Qt::OtherFocusReason);
    bl->deselect();
    QTest::mouseClick(bl, Qt::LeftButton, Qt::NoModifier, QPoint(2, bl->height() / 2));
    settle();
    eq(QStringLiteral("a click on the focused field selects nothing"), QStringLiteral("0"), I(bl->selectedText().size()));
    eq(QStringLiteral("... and puts the caret where it was clicked"), QStringLiteral("0"), I(bl->cursorPosition()));

    // A double click and the triple click select everything.
    bl->deselect();
    QTest::mouseDClick(bl, Qt::LeftButton, Qt::NoModifier, QPoint(bl->width() - 4, bl->height() / 2));
    settle();
    eq(QStringLiteral("a double click selects all"), L(w), bl->selectedText());

    // A click in a window that is not active selects all as well.
    c->setFieldText(0, QStringLiteral("8:30"));
    bl->setFocus(Qt::OtherFocusReason);
    bl->setCursorPosition(1);
    {
        QWidget other;
        other.show();
        other.activateWindow();
        yes(QStringLiteral("a second window becomes active"), QTest::qWaitForWindowActive(&other));
        QTest::mouseClick(bl, Qt::LeftButton, Qt::NoModifier, QPoint(bl->width() - 4, bl->height() / 2));
        settle();
        eq(QStringLiteral("a click in an inactive window selects all"), L(w), bl->selectedText());
    }
    w.activateWindow();
    yes(QStringLiteral("the main window becomes active again"), QTest::qWaitForWindowActive(&w));
}

void converterConvertKeepsValue() {
    for (const char* input : {"8,058", "0,0083", "8,0083", "0,996", "8,335", "12,3456", "1,5", "0,125", "9,994"}) {
        Win wp = newWin();
        MainWindow& w = *wp;
        w.converter()->setFieldText(1, S(QLatin1String(input)));
        const QString leftLive = L(w), capLive = cap(w);
        w.press(QStringLiteral("CONVERT"));
        eq(QStringLiteral("Convert on right <%1> keeps the left (%2)").arg(QLatin1String(input), leftLive), leftLive, L(w));
        eq(QStringLiteral("Convert on right <%1> keeps the caption").arg(QLatin1String(input)), capLive, cap(w));
        eq(QStringLiteral("Convert on right <%1> shows two decimals").arg(QLatin1String(input)),
           E(hc::converter::Normalize(hc::converter::Right, S(QLatin1String(input)).toStdWString())),
           R(w));
    }
    for (const char* input : {"8:3", "830", "0:90", "8", "8u30", "899"}) {
        Win wp = newWin();
        MainWindow& w = *wp;
        w.converter()->setFieldText(0, QLatin1String(input));
        const QString rightLive = R(w);
        w.press(QStringLiteral("CONVERT"));
        eq(QStringLiteral("Convert on left <%1> keeps the right").arg(QLatin1String(input)), rightLive, R(w));
    }
    {
        Win wp = newWin();
        MainWindow& w = *wp;
        w.converter()->setFieldText(1, S(QStringLiteral("9999999,999")));
        eq(QStringLiteral("right 9999999,999 gives left"), QStringLiteral("10000000:00"), L(w));
        w.press(QStringLiteral("CONVERT"));
        eq(QStringLiteral("Convert right 9999999,999: right"), S(QStringLiteral("10000000,00")), R(w));
        eq(QStringLiteral("Convert right 9999999,999: left"), QStringLiteral("10000000:00"), L(w));
    }
    {
        Win wp = newWin();
        MainWindow& w = *wp;
        w.converter()->setFieldText(0, QStringLiteral("9999999:60"));
        eq(QStringLiteral("left 9999999:60 gives right"), S(QStringLiteral("10000000,00")), R(w));
        w.press(QStringLiteral("CONVERT"));
        eq(QStringLiteral("Convert left 9999999:60: left"), QStringLiteral("10000000:00"), L(w));
        eq(QStringLiteral("Convert left 9999999:60: right"), S(QStringLiteral("10000000,00")), R(w));
    }
    {
        Win wp = newWin();
        MainWindow& w = *wp;
        w.converter()->setFieldText(0, QStringLiteral("9999999:99"));
        const QString rightShown = R(w);
        focusField(w, 1);
        w.press(QStringLiteral("CONVERT"));
        eq(QStringLiteral("Convert on an untouched 8 digit right value keeps it"), rightShown, R(w));
        eq(QStringLiteral("and the left is that same value (10000000:39)"), QStringLiteral("10000000:39"), L(w));
    }
    {
        Win wp = newWin();
        MainWindow& w = *wp;
        w.converter()->setFieldText(1, S(QStringLiteral("8,5")));
        w.press(QStringLiteral("CE"));
        eq(QStringLiteral("CE clears right"), QString(), R(w));
        eq(QStringLiteral("CE clears left"), QString(), L(w));
        eq(QStringLiteral("CE clears caption"), QString(), cap(w));
        w.press(QStringLiteral("CONVERT"));
        eq(QStringLiteral("Convert on empty fields: left"), QString(), L(w));
        eq(QStringLiteral("Convert on empty fields: right"), QString(), R(w));
        w.converter()->setFieldText(0, QStringLiteral(":"));
        w.press(QStringLiteral("CONVERT"));
        eq(QStringLiteral("Convert on a lone separator clears the field"), QString(), L(w));
    }
}

// The keyboard of the converter: Enter, Esc, text without a focused field, the clipboard.
void converterKeyboard() {
    Win wp = newWin();
    MainWindow& w = *wp;
    ConverterPage* c = w.converter();
    sendText(w, QStringLiteral("8u30"));
    eq(QStringLiteral("converter typed text"), QStringLiteral("8:30"), L(w));
    eq(QStringLiteral("converter typed text, right"), S(QStringLiteral("8,50")), R(w));
    w.press(QStringLiteral("RESET"));
    c->setFieldText(0, QStringLiteral("830"));
    key(w, Qt::Key_Return);
    eq(QStringLiteral("converter Enter converts"), QStringLiteral("08:30"), L(w));
    c->setFieldText(0, QStringLiteral("1230"));
    key(w, Qt::Key_Enter);
    eq(QStringLiteral("converter keypad Enter converts"), QStringLiteral("12:30"), L(w));
    key(w, Qt::Key_Escape);
    eq(QStringLiteral("converter Esc resets left"), QString(), L(w));
    eq(QStringLiteral("converter Esc resets right"), QString(), R(w));
    eq(QStringLiteral("converter Esc activates left"), QStringLiteral("0"), I(c->activeField()));
    focusField(w, 1);
    sendText(w, S(QStringLiteral("8,5")));
    eq(QStringLiteral("converter text goes to the right field when it is active"), S(QStringLiteral("8,5")), R(w));
    eq(QStringLiteral("converter right to left"), QStringLiteral("08:30"), L(w));

    // Text with no field focused (a page that has lost its focus): the active field gets it.
    w.press(QStringLiteral("RESET"));
    c->edit(0)->clearFocus();
    settle();
    eq(QStringLiteral("no field has the focus"), QStringLiteral("-1"), I(c->focusField()));
    QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral("8"));
    QApplication::sendEvent(&w, &press);
    eq(QStringLiteral("text without a focused field goes to the active field"), QStringLiteral("8"), L(w));
    eq(QStringLiteral("... which takes the focus"), QStringLiteral("0"), I(c->focusField()));

    // the clipboard
    w.press(QStringLiteral("RESET"));
    QGuiApplication::clipboard()->setText(QStringLiteral("8,30"));
    key(w, Qt::Key_V, Qt::ControlModifier);
    eq(QStringLiteral("Ctrl+V into the left field is filtered"), QStringLiteral("830"), L(w));
    eq(QStringLiteral("... and converted"), S(QStringLiteral("8,50")), R(w));
    key(w, Qt::Key_A, Qt::ControlModifier);
    key(w, Qt::Key_X, Qt::ControlModifier);
    eq(QStringLiteral("Ctrl+X puts the selection on the clipboard"), QStringLiteral("830"), QGuiApplication::clipboard()->text());
    eq(QStringLiteral("... and takes it from the field"), QString(), L(w));
    eq(QStringLiteral("... and from the other field"), QString(), R(w));
}

// ------------------------------------------------------------------ calculator

void calculatorTable() {
    struct Row {
        const char* keys;
        const char* left;
        const char* right;
    };
    const Row rows[] = {{"7:30+7:20=", "14:50", "14,83"},
                        {"8+0:30=", "08:30", "8,50"},
                        {"7,5+7:20=", "14:50", "14,83"},
                        {"7:30*5=", "37:30", "37,50"},
                        {"7:30/4=", "01:53", "1,88"},
                        {"7:00-8:30=", "-01:30", "-1,50"},
                        {"7:30+1:00==", "09:30", "9,50"},
                        {"0:90=", "01:30", "1,50"},
                        {"1:00+2:00*2=", "06:00", "6,00"},
                        {"7:30/0=", "Cannot divide by zero", ""}};
    for (int mode = 0; mode < 2; ++mode) {
        Win wp = newWin();
        MainWindow& w = *wp;
        w.selectTab(1);
        CalculatorPage* c = w.calculator();
        for (const Row& r : rows) {
            w.press(QStringLiteral("C"));
            const QString input = S(QLatin1String(r.keys));
            if (mode == 0) keys(w, QLatin1String(r.keys));
            else sendText(w, S(QLatin1String(r.keys)));
            const QString how = mode == 0 ? QStringLiteral("keypad") : QStringLiteral("keyboard");
            eq(QStringLiteral("%1 %2 left").arg(how, input), S(QLatin1String(r.left)), c->mainLabel()->text());
            eq(QStringLiteral("%1 %2 right").arg(how, input), S(QLatin1String(r.right)), c->decimalLabel()->text());
        }
    }
}

void calculatorWiring() {
    Win wp = newWin();
    MainWindow& w = *wp;
    w.selectTab(1);
    CalculatorPage* c = w.calculator();
    eq(QStringLiteral("calculator tab"), QStringLiteral("1"), I(w.currentTab()));
    eq(QStringLiteral("idle left display"), QStringLiteral("00:00"), c->mainText());
    eq(QStringLiteral("idle right display"), S(QStringLiteral("0,00")), c->decimalText());
    eq(QStringLiteral("idle caption"), QString(), c->captionText());
    eq(QStringLiteral("idle expression"), QString(), c->expressionShown());
    eq(QStringLiteral("the keyboard focus is not in a hidden converter field"), QStringLiteral("-1"), I(w.converter()->focusField()));

    keys(w, QStringLiteral("7:30+7:20="));
    eq(QStringLiteral("calc left"), QStringLiteral("14:50"), c->mainLabel()->text());
    eq(QStringLiteral("calc right"), S(QStringLiteral("14,83")), c->decimalLabel()->text());
    eq(QStringLiteral("calc caption"), QStringLiteral("14 h 50 min"), c->captionLabel()->text());
    eq(QStringLiteral("calc expression"), QStringLiteral("07:30 + 07:20 ="), c->expressionLabel()->text());
    w.press(QStringLiteral("C"));
    eq(QStringLiteral("calc cleared"), QStringLiteral("00:00"), c->mainLabel()->text());

    keys(w, QStringLiteral("7:30/0="));
    settle();
    eq(QStringLiteral("div0 text"), QStringLiteral("Cannot divide by zero"), c->mainLabel()->text());
    yes(QStringLiteral("the right value is hidden in the error state"), !c->decimalLabel()->isVisible() && !c->rightVisible());
    yes(QStringLiteral("the left value spans the width of both columns"), c->mainLabel()->width() > c->decimalLabel()->x());
    for (const char* k : {"+", "-", "*", "/", "=", "BACK", ":", ","})
        yes(QStringLiteral("calculator %1 disabled in the error state").arg(QLatin1String(k)), !c->keyEnabled(QLatin1String(k)));
    for (const char* k : {"5", "CE", "C", "0"})
        yes(QStringLiteral("calculator %1 enabled in the error state").arg(QLatin1String(k)), c->keyEnabled(QLatin1String(k)));
    w.press(QStringLiteral("C"));
    for (const char* k : {"+", "-", "*", "/", "=", "BACK", ":", ",", "5", "CE", "C"})
        yes(QStringLiteral("calculator %1 enabled after C").arg(QLatin1String(k)), c->keyEnabled(QLatin1String(k)));
    yes(QStringLiteral("the right value is back after C"), c->decimalLabel()->isVisible() && c->rightVisible());

    w.selectTab(0);
    keys(w, QStringLiteral("12"));
    eq(QStringLiteral("converter after calculator use"), QStringLiteral("12"), L(w));
    eq(QStringLiteral("converter value"), S(QStringLiteral("12,00")), R(w));
    w.selectTab(1);
    eq(QStringLiteral("calculator state survives a tab switch"), QStringLiteral("00:00"), c->mainText());
    eq(QStringLiteral("the key labels: ':'"), QStringLiteral(":"), c->key(QStringLiteral(":"))->label());
    eq(QStringLiteral("the key labels: decimal separator"), g_sep, c->key(QStringLiteral(","))->label());
}

// The display mirrors the engine, always (random key sequences).
void calculatorMirror() {
    Win wp = newWin();
    MainWindow& w = *wp;
    w.selectTab(1);
    CalculatorPage* c = w.calculator();
    const char* pool[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", ",", "+", "-", "*", "/", "=", "=", "CE", "C", "BACK"};
    const int poolSize = static_cast<int>(sizeof pool / sizeof pool[0]);
    unsigned seed = 31;
    auto next = [&] {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 16) & 0x7FFF;
    };
    int bad = 0;
    QString first;
    for (int n = 0; n < 500; ++n) {
        w.press(QStringLiteral("C"));
        const int len = 1 + static_cast<int>(next() % 13);
        QString log;
        for (int i = 0; i < len; ++i) {
            const QString k = QLatin1String(pool[next() % static_cast<unsigned>(poolSize)]);
            log += k + QLatin1Char(' ');
            w.press(k);
            hc::CalcEngine& e = c->engine();
            const bool err = e.IsError();
            bool ok = c->mainLabel()->text() == E(e.MainText()) && c->decimalLabel()->text() == E(e.DecimalText()) &&
                      c->captionLabel()->text() == E(e.Caption()) && c->rightVisible() == !err;
            for (const char* key : {"0", "1", "5", "9", ":", ",", "+", "-", "*", "/", "=", "CE", "C", "BACK"})
                ok = ok && (c->keyEnabled(QLatin1String(key)) == e.IsEnabled(QString(QLatin1String(key)).toStdWString()));
            QString full = E(e.Expression());
            full.replace(QStringLiteral(" * "), QStringLiteral(" ") + QChar(0x00D7) + QStringLiteral(" "));
            full.replace(QStringLiteral(" / "), QStringLiteral(" ") + QChar(0x00F7) + QStringLiteral(" "));
            full.replace(QStringLiteral(" - "), QStringLiteral(" ") + QChar(0x2212) + QStringLiteral(" "));
            const QString shown = c->expressionLabel()->text();
            ok = ok && (shown == full || (shown.startsWith(QString(QChar(0x2026)) + QLatin1Char(' ')) && shown.size() > 2 && full.endsWith(shown.mid(2))));
            if (!ok) {
                if (!bad) first = log + QStringLiteral("=> UI <%1|%2|%3|%4>, engine <%5|%6|%7|%8>").arg(c->mainLabel()->text(), c->decimalLabel()->text(), c->captionLabel()->text(), shown, E(e.MainText()), E(e.DecimalText()), E(e.Caption()), full);
                ++bad;
            }
        }
    }
    eq(QStringLiteral("window display always mirrors the engine (bad count; first: %1)").arg(first), QStringLiteral("0"), I(bad));
}

QString specKeyForChar(QChar ch) {
    const ushort c = ch.unicode();
    if (c >= '0' && c <= '9') return QString(ch);
    if (c == ':' || c == 'u' || c == 'U' || c == 'h' || c == 'H') return QStringLiteral(":");
    if (c == ',' || c == '.') return QStringLiteral(",");
    if (c == '+' || c == '-' || c == '*' || c == '/' || c == '=') return QString(ch);
    return QString();
}

void calculatorKeyboard() {
    Win wp = newWin();
    MainWindow& w = *wp;
    w.selectTab(1);
    CalculatorPage* c = w.calculator();

    // Every character a keyboard can type is the key it stands for, or nothing.
    QList<QChar> chars;
    for (ushort u = ' '; u <= '~'; ++u) chars << QChar(u);
    for (ushort u : {0x00E9, 0x00E8, 0x00E7, 0x00E0, 0x00A7, 0x00B0, 0x00B2, 0x00B5, 0x20AC, 0x00D7, 0x00F7, 0x2212, 0x0009, 0x000D, 0x001B}) chars << QChar(u);
    int bad = 0;
    QString first;
    for (QChar ch : chars) {
        w.press(QStringLiteral("C"));
        sendText(w, QStringLiteral("7") + ch + QStringLiteral("3"));
        hc::CalcEngine e;
        e.Press(L"7");
        const QString k = specKeyForChar(ch);
        if (!k.isEmpty()) e.Press(k.toStdWString());
        e.Press(L"3");
        const QString a = state(c->engine()), x = state(e);
        if (a != x) {
            if (!bad) first = QStringLiteral("U+%1 gave %2, expected %3").arg(QString::number(ch.unicode(), 16), a, x);
            ++bad;
        }
    }
    eq(QStringLiteral("calculator text mapping for %1 characters (bad count; first: %2)").arg(chars.size()).arg(first), QStringLiteral("0"), I(bad));

    w.press(QStringLiteral("C"));
    sendText(w, QStringLiteral("7u30+7h20="));
    eq(QStringLiteral("u and h are time separators, in a typed expression"), QStringLiteral("14:50"), c->mainText());
    eq(QStringLiteral("... and its expression line"), QStringLiteral("07:30 + 07:20 ="), c->expressionShown());
    w.press(QStringLiteral("C"));
    sendText(w, S(QStringLiteral("7,5")) + QStringLiteral("+7.5="));
    eq(QStringLiteral("comma and dot are both the decimal separator"), QStringLiteral("15:00"), c->mainText());

    w.press(QStringLiteral("C"));
    sendText(w, QStringLiteral("7:30+1"));
    key(w, Qt::Key_Return);
    eq(QStringLiteral("Enter is equals"), QStringLiteral("08:30"), c->mainText());
    key(w, Qt::Key_Enter);
    eq(QStringLiteral("keypad Enter repeats"), QStringLiteral("09:30"), c->mainText());
    key(w, Qt::Key_Backspace);
    eq(QStringLiteral("Backspace after a result does nothing"), QStringLiteral("09:30"), c->mainText());
    key(w, Qt::Key_Delete);
    eq(QStringLiteral("Delete is CE (a result resets)"), QStringLiteral("00:00"), c->mainText());
    sendText(w, QStringLiteral("1:3"));
    key(w, Qt::Key_Backspace);
    eq(QStringLiteral("Backspace edits the entry"), QStringLiteral("1:"), c->mainText());
    key(w, Qt::Key_Delete);
    eq(QStringLiteral("Delete clears the entry"), QStringLiteral("0"), c->mainText());
    sendText(w, QStringLiteral("5+5"));
    key(w, Qt::Key_Escape);
    eq(QStringLiteral("Escape is C"), QStringLiteral("00:00"), c->mainText());
    eq(QStringLiteral("Escape clears the expression"), QString(), c->expressionShown());
    sendText(w, QStringLiteral("7:30/0"));
    key(w, Qt::Key_Return);
    eq(QStringLiteral("Enter on x / 0 shows the error"), E(hc::CalcEngine::DivideByZeroText), c->mainText());
    key(w, Qt::Key_Return);
    eq(QStringLiteral("Enter in the error state does nothing"), E(hc::CalcEngine::DivideByZeroText), c->mainText());
    key(w, Qt::Key_Backspace);
    eq(QStringLiteral("Backspace in the error state does nothing"), E(hc::CalcEngine::DivideByZeroText), c->mainText());
    sendText(w, QStringLiteral("+"));
    eq(QStringLiteral("'+' in the error state does nothing"), E(hc::CalcEngine::DivideByZeroText), c->mainText());
    sendText(w, QStringLiteral(":"));
    eq(QStringLiteral("':' in the error state does nothing"), E(hc::CalcEngine::DivideByZeroText), c->mainText());
    sendText(w, QStringLiteral("2"));
    eq(QStringLiteral("a digit leaves the error state"), QStringLiteral("2"), c->mainText());

    eq(QStringLiteral("calculator keystrokes did not touch the converter"), QString(), L(w));
    w.selectTab(0);
    w.press(QStringLiteral("C"));
    const QString before = state(c->engine());
    sendText(w, QStringLiteral("7:30+7:20"));
    key(w, Qt::Key_Delete);
    key(w, Qt::Key_Backspace);
    eq(QStringLiteral("converter keystrokes did not touch the calculator"), before, state(c->engine()));
    eq(QStringLiteral("converter typed text (Backspace took the last digit)"), QStringLiteral("7:3"), L(w));
    key(w, Qt::Key_Return);
    eq(QStringLiteral("converter Enter converts"), QStringLiteral("07:03"), L(w));
    key(w, Qt::Key_Escape);
    eq(QStringLiteral("converter Esc resets"), QString(), L(w));
    eq(QStringLiteral("converter Esc keeps the calculator"), before, state(c->engine()));
}

void calculatorExpressionLine() {
    Win wp = newWin();
    MainWindow& w = *wp;
    w.selectTab(1);
    CalculatorPage* c = w.calculator();
    keys(w, QStringLiteral("7:30*2/4-1="));
    eq(QStringLiteral("operators are shown as x / division sign / minus sign"),
       QStringLiteral("07:30 ") + QChar(0x00D7) + QStringLiteral(" 2 ") + QChar(0x00F7) + QStringLiteral(" 4 ") + QChar(0x2212) + QStringLiteral(" 01:00 ="), c->expressionLabel()->text());
    w.press(QStringLiteral("C"));
    eq(QStringLiteral("C clears the expression line"), QString(), c->expressionLabel()->text());
    keys(w, QStringLiteral("7:30*1:30="));
    eq(QStringLiteral("factor shown as a decimal"), S(QStringLiteral("07:30 ") + QChar(0x00D7) + QStringLiteral(" 1,5 =")), c->expressionLabel()->text());
    w.press(QStringLiteral("C"));
    keys(w, QStringLiteral("7:30/0="));
    eq(QStringLiteral("error keeps the failing operation"), QStringLiteral("07:30 ") + QChar(0x00F7) + QStringLiteral(" 0 ="), c->expressionLabel()->text());
    w.press(QStringLiteral("C"));

    const QSize sizes[] = {w.minimumWindowSize(), w.defaultSize(), w.defaultSize() * 2};
    for (const QSize& s : sizes) {
        Win vp = newWin();
        MainWindow& v = *vp;
        v.resize(s);
        v.selectTab(1);
        settle();
        QString typed;
        for (int i = 0; i < 40; ++i) {
            keys(v, QStringLiteral("7:30+"));
            typed += QStringLiteral("07:30 + ");
        }
        settle();
        const QString full = typed.left(typed.size() - 1);
        const QString tag = QStringLiteral("long chain at %1x%2").arg(s.width()).arg(s.height());
        const QString x = v.calculator()->expressionLabel()->text();
        yes(tag + QStringLiteral(": shortened with an ellipsis"), x.startsWith(QString(QChar(0x2026)) + QLatin1Char(' ')));
        yes(tag + QStringLiteral(": keeps the end"), full.endsWith(x.mid(2)));
        yes(tag + QStringLiteral(": keeps at least the pending operator"), !x.isEmpty() && x.endsWith(QLatin1Char('+')));
        const QLabel* label = v.calculator()->expressionLabel();
        const double need = QFontMetricsF(label->font()).horizontalAdvance(x);
        yes(tag + QStringLiteral(": fits the line (%1 of %2)").arg(need).arg(label->width()), need <= label->width() + 0.5);
        v.press(QStringLiteral("="));
        settle();
        const QString y = v.calculator()->expressionLabel()->text();
        yes(tag + QStringLiteral(": still ends with = after the result"), !y.isEmpty() && y.endsWith(QLatin1Char('=')));
        v.press(QStringLiteral("C"));
        eq(tag + QStringLiteral(": cleared"), QString(), v.calculator()->expressionLabel()->text());
    }
}

// Copy, paste, and the Copy menu of the values.
void calculatorClipboard() {
    Win wp = newWin();
    MainWindow& w = *wp;
    w.selectTab(1);
    CalculatorPage* c = w.calculator();
    keys(w, QStringLiteral("7:30+7:20="));
    QGuiApplication::clipboard()->clear();
    key(w, Qt::Key_C, Qt::ControlModifier);
    eq(QStringLiteral("Ctrl+C copies the h:mm value"), QStringLiteral("14:50"), QGuiApplication::clipboard()->text());
    key(w, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
    eq(QStringLiteral("Ctrl+Shift+C copies the decimal value"), S(QStringLiteral("14,83")), QGuiApplication::clipboard()->text());

    w.press(QStringLiteral("C"));
    QGuiApplication::clipboard()->setText(S(QStringLiteral("7,5")));
    key(w, Qt::Key_V, Qt::ControlModifier);
    eq(QStringLiteral("Ctrl+V pastes a decimal entry"), QStringLiteral("7") + g_sep + QStringLiteral("5"), c->mainText());
    eq(QStringLiteral("... and shows its value"), S(QStringLiteral("7,50")), c->decimalText());
    QGuiApplication::clipboard()->setText(QStringLiteral("7u30"));
    key(w, Qt::Key_V, Qt::ControlModifier);
    eq(QStringLiteral("Ctrl+V pastes a time with a u"), QStringLiteral("7:30"), c->mainText());
    QGuiApplication::clipboard()->setText(QStringLiteral("hello"));
    key(w, Qt::Key_V, Qt::ControlModifier);
    eq(QStringLiteral("Ctrl+V with text that is not a time does nothing"), QStringLiteral("7:30"), c->mainText());

    keys(w, QStringLiteral("+1="));
    // Right click on a value offers Copy.
    for (int which = 0; which < 2; ++which) {
        DisplayLabel* label = which == 0 ? c->mainLabel() : c->decimalLabel();
        QGuiApplication::clipboard()->clear();
        QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(5, 5), label->mapToGlobal(QPoint(5, 5)));
        QApplication::sendEvent(label, &ev);
        settle();
        QMenu* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        yes(QStringLiteral("right click on the %1 value opens a menu").arg(which == 0 ? QStringLiteral("left") : QStringLiteral("right")), popup != nullptr);
        if (popup) {
            yes(QStringLiteral("... with one item, Copy"), popup->actions().size() == 1 && popup->actions().first()->text() == QStringLiteral("Copy"));
            popup->actions().first()->trigger();
            popup->close();
            settle();
            eq(QStringLiteral("Copy copies the %1 value").arg(which == 0 ? QStringLiteral("h:mm") : QStringLiteral("decimal")),
               which == 0 ? QStringLiteral("08:30") : S(QStringLiteral("8,50")), QGuiApplication::clipboard()->text());
        }
    }
}

// ------------------------------------------------------------------ keyboard routing, tabs

void tabsAndShortcuts() {
    Win wp = newWin();
    MainWindow& w = *wp;
    key(w, Qt::Key_2, Qt::ControlModifier);
    eq(QStringLiteral("Ctrl+2 selects the calculator"), QStringLiteral("1"), I(w.currentTab()));
    eq(QStringLiteral("... and the tab bar follows"), QStringLiteral("1"), I(w.tabBar()->currentIndex()));
    key(w, Qt::Key_1, Qt::ControlModifier);
    eq(QStringLiteral("Ctrl+1 selects the converter"), QStringLiteral("0"), I(w.currentTab()));
    key(w, Qt::Key_Tab, Qt::ControlModifier);
    eq(QStringLiteral("Ctrl+Tab switches to the other tab"), QStringLiteral("1"), I(w.currentTab()));
    key(w, Qt::Key_Tab, Qt::ControlModifier);
    eq(QStringLiteral("Ctrl+Tab switches back"), QStringLiteral("0"), I(w.currentTab()));
    key(w, Qt::Key_2, Qt::ControlModifier | Qt::KeypadModifier);
    eq(QStringLiteral("Ctrl+Numpad2 selects the calculator"), QStringLiteral("1"), I(w.currentTab()));
    key(w, Qt::Key_1, Qt::ControlModifier | Qt::KeypadModifier);
    eq(QStringLiteral("Ctrl+Numpad1 selects the converter"), QStringLiteral("0"), I(w.currentTab()));
    eq(QStringLiteral("Ctrl+1 types nothing into the field"), QString(), L(w));

    // The tab bar: a click selects, and the keypad never takes the focus.
    QTest::mouseClick(w.tabBar(), Qt::LeftButton, Qt::NoModifier, QPoint(w.tabBar()->width() - 8, w.tabBar()->height() / 2));
    settle();
    eq(QStringLiteral("clicking the Calculator tab selects it"), QStringLiteral("1"), I(w.currentTab()));
    eq(QStringLiteral("tab names"), QStringLiteral("Converter|Calculator"), w.tabBar()->tabText(0) + QStringLiteral("|") + w.tabBar()->tabText(1));
    w.selectTab(0);
    eq(QStringLiteral("back on the converter the left field has the focus"), QStringLiteral("0"), I(w.converter()->focusField()));
    for (KeyButton* b : w.converter()->keys()) yes(QStringLiteral("key %1 does not take the focus").arg(b->command()), b->focusPolicy() == Qt::NoFocus);
    for (KeyButton* b : w.calculator()->keys()) yes(QStringLiteral("calculator key %1 does not take the focus").arg(b->command()), b->focusPolicy() == Qt::NoFocus);
    yes(QStringLiteral("the tab bar does not take the focus"), w.tabBar()->focusPolicy() == Qt::NoFocus);
    yes(QStringLiteral("the menu button does not take the focus"), w.menuButton()->focusPolicy() == Qt::NoFocus);

    // The menu opens from the keyboard (F10) and from the button, below the button.
    yes(QStringLiteral("the menu is closed at first"), !w.menu()->isVisible());
    key(w, Qt::Key_F10);
    settle();
    yes(QStringLiteral("F10 opens the menu"), w.menu()->isVisible());
    const QPoint below = w.menuButton()->mapToGlobal(QPoint(0, w.menuButton()->height()));
    yes(QStringLiteral("... below the menu button (menu top %1, button bottom %2)").arg(w.menu()->y()).arg(below.y()), qAbs(w.menu()->y() - below.y()) <= 2);
    yes(QStringLiteral("... its right edge on the right edge of the button"),
        qAbs(w.menu()->geometry().right() - w.menuButton()->mapToGlobal(QPoint(w.menuButton()->width() - 1, 0)).x()) <= 2);
    w.menu()->hide();
    settle();
    yes(QStringLiteral("the menu closes"), !w.menu()->isVisible());
    QTest::qWait(300);                       // past the guard against a click that only closed the menu
    yes(QStringLiteral("after the guard time a click on the button is not blocked"), !w.menuReopenBlocked());
    QTest::mouseClick(w.menuButton(), Qt::LeftButton);
    settle();
    yes(QStringLiteral("a click on the menu button opens the menu"), w.menu()->isVisible());
    w.menu()->hide();
    settle();
    yes(QStringLiteral("a click right after the menu closed does not open it again"), w.menuReopenBlocked());
    QTest::mouseClick(w.menuButton(), Qt::LeftButton);
    settle();
    yes(QStringLiteral("... (the click that closed it)"), !w.menu()->isVisible());
    QTest::qWait(300);

    // Clicking keys of the keypad with the mouse.
    w.press(QStringLiteral("RESET"));
    for (const char* k : {"8", ":", "3", "0"}) {
        KeyButton* b = w.converter()->key(QLatin1String(k));
        QTest::mouseClick(b, Qt::LeftButton);
    }
    settle();
    eq(QStringLiteral("keys clicked with the mouse type into the field"), QStringLiteral("8:30"), L(w));
    QTest::mouseClick(w.converter()->key(QStringLiteral("CONVERT")), Qt::LeftButton);
    eq(QStringLiteral("clicking Convert converts"), QStringLiteral("08:30"), L(w));
    yes(QStringLiteral("a disabled key does nothing when clicked"), !w.converter()->keyEnabled(QStringLiteral(",")));
    QTest::mouseClick(w.converter()->key(QStringLiteral(",")), Qt::LeftButton);
    eq(QStringLiteral("... the left field is as it was"), QStringLiteral("08:30"), L(w));
}

// Ctrl+1 and Ctrl+2 on a layout whose digits are on the shifted level of the number row (Belgian and French AZERTY: the
// keys are & and e-acute). The event is the one the platform delivers when the key is pressed alone with Ctrl: the
// character of the unshifted level, the xkb key code of the key (10 and 11). Sent to the widget that has the focus.
void numberRowKeys() {
    struct Row {
        int scanCode;
        Qt::Key key;
        ushort ch;             // what the key types with Ctrl on an AZERTY layout
        int tab;
    };
    for (const Row& r : {Row{10, Qt::Key_Ampersand, 0x26, 0}, Row{11, Qt::Key_Eacute, 0xE9, 1}}) {
        for (int start = 0; start < 2; ++start) {          // from the converter (a field has the focus) / the calculator (nothing has)
            Win wp = newWin();
            MainWindow& w = *wp;
            w.selectTab(start);
            const QString from = QStringLiteral("(from the %1)").arg(start == 0 ? "converter" : "calculator");
            QKeyEvent press(QEvent::KeyPress, r.key, Qt::ControlModifier, static_cast<quint32>(r.scanCode), 0x26, 4, QString(QChar(r.ch)));
            QApplication::sendEvent(target(w), &press);
            settle();
            eq(QStringLiteral("Ctrl + number row key %1 selects tab %2 %3").arg(r.scanCode).arg(r.tab).arg(from), I(r.tab), I(w.currentTab()));
            eq(QStringLiteral("... and the field takes no character %1").arg(from), QString(), L(w));
            // with Shift or Alt it is another key: no tab switch
            w.selectTab(1 - r.tab);
            QKeyEvent shifted(QEvent::KeyPress, r.key, Qt::ControlModifier | Qt::ShiftModifier, static_cast<quint32>(r.scanCode), 0x26, 5, QString());
            QApplication::sendEvent(target(w), &shifted);
            QKeyEvent alt(QEvent::KeyPress, r.key, Qt::ControlModifier | Qt::AltModifier, static_cast<quint32>(r.scanCode), 0x26, 12, QString());
            QApplication::sendEvent(target(w), &alt);
            settle();
            eq(QStringLiteral("Ctrl+Shift and Ctrl+Alt + number row key %1 are not this shortcut").arg(r.scanCode), I(1 - r.tab), I(w.currentTab()));
            // the key without Ctrl does not switch
            QKeyEvent plain(QEvent::KeyPress, r.key, Qt::NoModifier, static_cast<quint32>(r.scanCode), 0x26, 0, QString(QChar(r.ch)));
            QApplication::sendEvent(target(w), &plain);
            settle();
            eq(QStringLiteral("number row key %1 without Ctrl does not switch the tab").arg(r.scanCode), I(1 - r.tab), I(w.currentTab()));
        }
    }
}

// Ctrl+Q and Ctrl+W close the window (the standard keys of a KDE application); the program ends with its only window.
void quitAndClose() {
    const bool quitOnClose = QGuiApplication::quitOnLastWindowClosed();
    QGuiApplication::setQuitOnLastWindowClosed(false);       // the test goes on after the window is gone
    for (Qt::Key k : {Qt::Key_Q, Qt::Key_W}) {
        Win wp = newWin();
        MainWindow& w = *wp;
        yes(QStringLiteral("the window is open"), w.isVisible());
        QTest::keyClick(target(w), k, Qt::ControlModifier);
        settle();
        yes(QStringLiteral("Ctrl+%1 closes the window").arg(QKeySequence(k).toString()), !w.isVisible());
    }
    QGuiApplication::setQuitOnLastWindowClosed(quitOnClose);
}

// ------------------------------------------------------------------ menu, settings, theme

QAction* findAction(QMenu* menu, const QString& text) {
    for (QAction* a : menu->actions())
        if (a->text() == text) return a;
    return nullptr;
}

QString checkedItems(QActionGroup* group) {
    QStringList out;
    for (QAction* a : group->actions())
        if (a->isChecked()) out << a->text();
    return out.join(QLatin1Char('|'));
}

QString fileText(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QStringLiteral("<no file>");
    return QString::fromUtf8(f.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("<CRLF>"));
}

void menuAndPlaces() {
    Win wp = newWin();
    MainWindow& w = *wp;
    QMenu* menu = w.menu();
    // the menu: Theme, Decimal places, About (and Keep on top only where the platform has it)
    QStringList items;
    for (QAction* a : menu->actions()) items << (a->isSeparator() ? (a->text().isEmpty() ? QStringLiteral("---") : QStringLiteral("[%1]").arg(a->text())) : a->text());
    eq(QStringLiteral("menu items"),
       QStringLiteral("[Theme] Auto (follow system) Light Dark [Decimal places] 2 (default) 3 4 5 6 --- About HourCalc"), items.join(QLatin1Char(' ')));
    yes(QStringLiteral("no Keep on top on the offscreen platform (not X11)"), !w.keepOnTopOffered() && w.keepOnTopAction() == nullptr);
    yes(QStringLiteral("... by default too"), !MainWindow(QString()).keepOnTopOffered());

    // decimal places
    eq(QStringLiteral("2 places by default"), QStringLiteral("2"), I(w.decimalPlaces()));
    for (QAction* a : menu->actions())
        if (a->isCheckable()) {
            yes(QStringLiteral("menu item %1 is a radio item").arg(a->text()), a->actionGroup() != nullptr && a->actionGroup()->isExclusive());
        }
    w.converter()->setFieldText(0, QStringLiteral("08:20"));
    w.selectTab(1);
    keys(w, QStringLiteral("7:30/4="));
    eq(QStringLiteral("2 places: converter"), S(QStringLiteral("8,33")), R(w));
    eq(QStringLiteral("2 places: calculator"), S(QStringLiteral("1,88")), w.calculator()->decimalText());
    const QString placesNames[] = {QStringLiteral("2 (default)"), QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6")};
    const char* conv[] = {"8,33", "8,333", "8,3333", "8,33333", "8,333333"};
    const char* calc[] = {"1,88", "1,875", "1,8750", "1,87500", "1,875000"};
    for (int i = 0; i < 5; ++i) {
        QAction* a = findAction(menu, placesNames[i]);
        yes(QStringLiteral("menu has %1").arg(placesNames[i]), a != nullptr);
        if (!a) continue;
        a->trigger();
        eq(QStringLiteral("%1: setting").arg(placesNames[i]), I(i + 2), I(w.decimalPlaces()));
        eq(QStringLiteral("%1: the checked item").arg(placesNames[i]), placesNames[i], checkedItems(w.menu()->actions().at(5)->actionGroup()));
        eq(QStringLiteral("%1: the converter value is re-formatted at once").arg(placesNames[i]), S(QLatin1String(conv[i])), R(w));
        eq(QStringLiteral("%1: the calculator value is re-formatted at once").arg(placesNames[i]), S(QLatin1String(calc[i])), w.calculator()->decimalText());
        eq(QStringLiteral("%1: the left value stays whole minutes").arg(placesNames[i]), QStringLiteral("08:20"), L(w));
        eq(QStringLiteral("%1: the calculator's h:mm stays").arg(placesNames[i]), QStringLiteral("01:53"), w.calculator()->mainText());
    }
    // the table of SPEC 6: 0:01 -> 0,02 (2), 0,0167 (4)
    w.setDecimalPlaces(2);
    w.selectTab(0);
    w.converter()->setFieldText(0, QStringLiteral("0:01"));
    eq(QStringLiteral("0:01 at 2 places"), S(QStringLiteral("0,02")), R(w));
    w.setDecimalPlaces(4);
    eq(QStringLiteral("0:01 at 4 places"), S(QStringLiteral("0,0167")), R(w));
    w.setDecimalPlaces(9);
    eq(QStringLiteral("a number of places outside 2..6 is the default"), QStringLiteral("2"), I(w.decimalPlaces()));
    // converting an entry that is typed with 6 decimals still works at 2 places
    w.press(QStringLiteral("RESET"));
    w.converter()->setFieldText(1, S(QStringLiteral("0,123456")));
    eq(QStringLiteral("typed input keeps 6 decimals at 2 places"), S(QStringLiteral("0,123456")), R(w));
    w.press(QStringLiteral("CONVERT"));
    eq(QStringLiteral("Convert shows the setting"), S(QStringLiteral("0,12")), R(w));

    // About is an item of the menu
    AboutDialog* d = w.createAbout();
    yes(QStringLiteral("About can be created"), d != nullptr);
    delete d;
}

void keepOnTop() {
    Win wp = newWin(QString(), 1);
    MainWindow& w = *wp;
    yes(QStringLiteral("Keep on top is offered when forced"), w.keepOnTopOffered());
    QAction* a = w.keepOnTopAction();
    yes(QStringLiteral("it is a checkable item"), a && a->isCheckable() && !a->isChecked() && a->text() == QStringLiteral("Keep on top"));
    yes(QStringLiteral("it sits before About"), w.menu()->actions().at(w.menu()->actions().size() - 2) == a);
    a->trigger();
    yes(QStringLiteral("Keep on top on"), w.keepOnTop() && a->isChecked());
    yes(QStringLiteral("... the window carries the hint"), w.windowHandle() && (w.windowHandle()->flags() & Qt::WindowStaysOnTopHint));
    a->trigger();
    yes(QStringLiteral("Keep on top off"), !w.keepOnTop() && !a->isChecked());
    yes(QStringLiteral("... the hint is gone"), w.windowHandle() && !(w.windowHandle()->flags() & Qt::WindowStaysOnTopHint));
    yes(QStringLiteral("the offscreen platform is not X11, so the platform default is not to offer it"), QGuiApplication::platformName() != QLatin1String("xcb"));
}

void settingsFile() {
    QTemporaryDir dir;
    yes(QStringLiteral("temporary folder"), dir.isValid());
    const QString path = dir.filePath(QStringLiteral("cfg/hourcalc.ini"));      // the folder does not exist yet
    {
        Win wp = newWin(path);
        MainWindow& w = *wp;
        eq(QStringLiteral("no file without a change"), QStringLiteral("<no file>"), fileText(path));
        eq(QStringLiteral("defaults: places"), QStringLiteral("2"), I(w.decimalPlaces()));
        yes(QStringLiteral("defaults: theme Auto"), w.themeMode() == MainWindow::ThemeMode::Auto);
        w.setDecimalPlaces(2);
        eq(QStringLiteral("setting the same value writes nothing"), QStringLiteral("<no file>"), fileText(path));
        w.setThemeMode(MainWindow::ThemeMode::Auto);
        eq(QStringLiteral("choosing the same theme writes nothing"), QStringLiteral("<no file>"), fileText(path));
        w.setDecimalPlaces(3);
        eq(QStringLiteral("the first change creates the folder and the file"), QStringLiteral("DecimalPlaces=3<CRLF>Theme=auto<CRLF>"), fileText(path));
        w.setThemeMode(MainWindow::ThemeMode::Dark);
        eq(QStringLiteral("a theme change keeps the places"), QStringLiteral("DecimalPlaces=3<CRLF>Theme=dark<CRLF>"), fileText(path));
        // another HourCalc changed a setting meanwhile: it is not lost
        QFile f(path);
        yes(QStringLiteral("rewrite the file by hand"), f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("DecimalPlaces=5\r\nTheme=light\r\n");
        f.close();
        w.setThemeMode(MainWindow::ThemeMode::Auto);
        eq(QStringLiteral("changing the theme keeps the places that are on disk"), QStringLiteral("DecimalPlaces=5<CRLF>Theme=auto<CRLF>"), fileText(path));
        w.setDecimalPlaces(4);
        eq(QStringLiteral("changing the places keeps the theme that is on disk"), QStringLiteral("DecimalPlaces=4<CRLF>Theme=auto<CRLF>"), fileText(path));
        w.setThemeMode(MainWindow::ThemeMode::Light);
    }
    {
        Win wp = newWin(path);
        eq(QStringLiteral("a new window reads the places"), QStringLiteral("4"), I(wp->decimalPlaces()));
        yes(QStringLiteral("a new window reads the theme"), wp->themeMode() == MainWindow::ThemeMode::Light);
        eq(QStringLiteral("the converter uses the stored places"), QStringLiteral("4"), I(wp->converter()->decimalPlaces()));
        eq(QStringLiteral("the calculator uses the stored places"), QStringLiteral("4"), I(wp->calculator()->decimalPlaces()));
        eq(QStringLiteral("the menu shows the stored places"), QStringLiteral("4"), checkedItems(wp->menu()->actions().at(5)->actionGroup()));
        wp->setThemeMode(MainWindow::ThemeMode::Dark);
        eq(QStringLiteral("the menu shows the theme"), QStringLiteral("Dark"), checkedItems(wp->menu()->actions().at(1)->actionGroup()));
    }
    {
        Win wp = newWin(path);
        yes(QStringLiteral("a new window starts dark when the file says dark"), wp->themeMode() == MainWindow::ThemeMode::Dark && wp->isDark());
        eq(QStringLiteral("... and keeps the places"), QStringLiteral("4"), I(wp->decimalPlaces()));
    }
    // the file is read case-insensitively; anything invalid is the default
    const struct {
        const char* text;
        int places;
        MainWindow::ThemeMode theme;
    } cases[] = {{"THEME=DARK\r\ndecimalplaces=6\r\n", 6, MainWindow::ThemeMode::Dark},
                 {"DecimalPlaces=9\r\nTheme=blue\r\n", 2, MainWindow::ThemeMode::Auto},
                 {"DecimalPlaces=abc\r\n", 2, MainWindow::ThemeMode::Auto},
                 {"", 2, MainWindow::ThemeMode::Auto},
                 {"DecimalPlaces=3\nTheme=light\n", 3, MainWindow::ThemeMode::Light}};
    for (const auto& k : cases) {
        QFile f(path);
        yes(QStringLiteral("write a settings file"), f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(k.text);
        f.close();
        MainWindow w(path, 0);
        eq(QStringLiteral("settings file <%1>: places").arg(QString::fromLatin1(k.text).replace(QLatin1Char('\r'), QString()).replace(QLatin1Char('\n'), QLatin1Char('/'))), I(k.places), I(w.decimalPlaces()));
        yes(QStringLiteral("settings file <%1>: theme").arg(QString::fromLatin1(k.text).replace(QLatin1Char('\r'), QString()).replace(QLatin1Char('\n'), QLatin1Char('/'))), w.themeMode() == k.theme);
    }
    // a settings file that cannot be written: the setting still applies to the session
    {
        MainWindow w(QStringLiteral("/proc/hourcalc-no-such-folder/hourcalc.ini"), 0);
        w.setDecimalPlaces(5);
        eq(QStringLiteral("a file that cannot be written: the setting applies to the session"), QStringLiteral("5"), I(w.decimalPlaces()));
        w.setThemeMode(MainWindow::ThemeMode::Dark);
        yes(QStringLiteral("... and so does the theme"), w.isDark());
    }
    // no path: nothing is read or written
    {
        MainWindow w(QString(), 0);
        w.setDecimalPlaces(5);
        eq(QStringLiteral("no path: the setting applies"), QStringLiteral("5"), I(w.decimalPlaces()));
    }
    // the default path of the core: $XDG_CONFIG_HOME/hourcalc/hourcalc.ini
    {
        const QByteArray saved = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", QByteArray("/tmp/xdg-test"));
        eq(QStringLiteral("default path from XDG_CONFIG_HOME"), QStringLiteral("/tmp/xdg-test/hourcalc/hourcalc.ini"), q(hc::settings::DefaultPath()));
        qunsetenv("XDG_CONFIG_HOME");
        const QString home = QString::fromLocal8Bit(qgetenv("HOME"));
        eq(QStringLiteral("default path without XDG_CONFIG_HOME is under ~/.config"), home + QStringLiteral("/.config/hourcalc/hourcalc.ini"), q(hc::settings::DefaultPath()));
        qputenv("XDG_CONFIG_HOME", saved);
        eq(QStringLiteral("default path is restored"), QString::fromLocal8Bit(saved) + QStringLiteral("/hourcalc/hourcalc.ini"), q(hc::settings::DefaultPath()));
    }
    // persistence the way the program does it: through the default path, with a temporary XDG_CONFIG_HOME
    {
        QTemporaryDir xdg;
        yes(QStringLiteral("temporary XDG_CONFIG_HOME"), xdg.isValid());
        const QByteArray saved = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", xdg.path().toLocal8Bit());
        const QString def = q(hc::settings::DefaultPath());
        eq(QStringLiteral("the program's settings file"), xdg.path() + QStringLiteral("/hourcalc/hourcalc.ini"), def);
        {
            MainWindow w(def, 0);
            eq(QStringLiteral("XDG: nothing is written at start-up"), QStringLiteral("<no file>"), fileText(def));
            w.setDecimalPlaces(5);
            w.setThemeMode(MainWindow::ThemeMode::Dark);
        }
        eq(QStringLiteral("XDG: the settings file"), QStringLiteral("DecimalPlaces=5<CRLF>Theme=dark<CRLF>"), fileText(def));
        {
            MainWindow w(q(hc::settings::DefaultPath()), 0);
            eq(QStringLiteral("XDG: the next start reads the places"), QStringLiteral("5"), I(w.decimalPlaces()));
            yes(QStringLiteral("XDG: the next start reads the theme, and is dark"), w.themeMode() == MainWindow::ThemeMode::Dark && w.isDark());
            w.converter()->setFieldText(0, QStringLiteral("8:20"));
            eq(QStringLiteral("XDG: the converter uses them"), S(QStringLiteral("8,33333")), w.converter()->fieldText(1));
        }
        qputenv("XDG_CONFIG_HOME", saved);
    }
}

// The window colour of a palette, as a text (for the messages).
QString windowName(const QPalette& p) { return p.color(QPalette::Active, QPalette::Window).name(); }

// A forced theme that was saved: everything starts in it, not only after the first change of the theme in the session
// (SPEC 6b: the choice applies to the menu, the dialogs and the tool tips too), on a desktop of the other scheme and of the same.
void forcedThemeAtStart(const QPalette& lightDesktop, const QPalette& darkDesktop) {
    const QPalette toolTipBefore = QToolTip::palette();
    struct Case {
        const char* name;
        bool desktopDark;
        MainWindow::ThemeMode mode;
        bool wantDark;
    };
    for (const Case& c : {Case{"Dark on a light desktop", false, MainWindow::ThemeMode::Dark, true},
                          Case{"Light on a dark desktop", true, MainWindow::ThemeMode::Light, false},
                          Case{"Dark on a dark desktop", true, MainWindow::ThemeMode::Dark, true},
                          Case{"Light on a light desktop", false, MainWindow::ThemeMode::Light, false}}) {
        QApplication::setPalette(c.desktopDark ? darkDesktop : lightDesktop);
        settle();
        QTemporaryDir dir;
        yes(QStringLiteral("temporary folder"), dir.isValid());
        const QString path = dir.filePath(QStringLiteral("hourcalc.ini"));
        hc::settings::SaveTheme(w(path), c.mode, 2);
        const QString what = QStringLiteral("saved %1, at start").arg(QString::fromLatin1(c.name));
        {
            Win wp = newWin(path);
            MainWindow& win = *wp;
            yes(what + QStringLiteral(": the window is %1").arg(c.wantDark ? "dark" : "light"), win.isDark() == c.wantDark);
            yes(what + QStringLiteral(": the menu has the palette of the window (%1, window %2)").arg(windowName(win.menu()->palette()), windowName(win.palette())),
                win.menu()->palette().color(QPalette::Active, QPalette::Window) == win.palette().color(QPalette::Active, QPalette::Window));
            yes(what + QStringLiteral(": the menu is %1").arg(c.wantDark ? "dark" : "light"), isDarkPalette(win.menu()->palette()) == c.wantDark);
            yes(what + QStringLiteral(": the tool tips are %1 (%2)").arg(c.wantDark ? "dark" : "light", QToolTip::palette().color(QPalette::ToolTipBase).name()),
                (QToolTip::palette().color(QPalette::ToolTipBase).lightness() < 128) == c.wantDark);
            AboutDialog* about = win.createAbout();
            yes(what + QStringLiteral(": the About dialog is %1").arg(c.wantDark ? "dark" : "light"), isDarkPalette(about->palette()) == c.wantDark);
            delete about;
            // back to Auto: the menu and the tool tips follow the desktop again
            win.setThemeMode(MainWindow::ThemeMode::Auto);
            yes(what + QStringLiteral(": Auto, the menu follows the desktop"), isDarkPalette(win.menu()->palette()) == c.desktopDark);
            yes(what + QStringLiteral(": Auto, the tool tips follow the desktop"), (QToolTip::palette().color(QPalette::ToolTipBase).lightness() < 128) == c.desktopDark);
            win.setThemeMode(c.mode);
            yes(what + QStringLiteral(": and forced again"), isDarkPalette(win.menu()->palette()) == c.wantDark);
        }
        eq(what + QStringLiteral(": the tool tips are as before once the window is gone"), toolTipBefore.color(QPalette::ToolTipBase).name(),
           QToolTip::palette().color(QPalette::ToolTipBase).name());
    }
    QApplication::setPalette(lightDesktop);
    settle();
}

// The relative luminance of a colour, WCAG 2: for the contrast of the label on the accent key.
double luminance(const QColor& c) {
    auto lin = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

double contrast(const QColor& a, const QColor& b) {
    const double x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

// The label of Convert and = reads on the accent: the strongest ink pixel of the key against its fill, at least 4.5 : 1
// (WCAG AA for text), for the blues of KDE and of Windows and for a dark and a light scheme. Measured on the painted key.
void accentLabel() {
    const QPalette original = QApplication::palette();
    struct Accent {
        const char* name;
        QColor color;
        bool dark;
    };
    for (const Accent& a : {Accent{"Breeze blue", QColor(0x3d, 0xae, 0xe9), false}, Accent{"Breeze blue, dark scheme", QColor(0x3d, 0xae, 0xe9), true},
                            Accent{"Windows blue", QColor(0x00, 0x67, 0xc0), false}, Accent{"Windows light blue, dark scheme", QColor(0x4c, 0xc2, 0xff), true},
                            Accent{"orange", QColor(0xf6, 0x74, 0x00), false}, Accent{"green", QColor(0x27, 0xae, 0x60), true}}) {
        QPalette pal = schemePalette(a.dark, original);
        for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) pal.setColor(g, QPalette::Highlight, a.color);
        // The palette of the desktop: the label colour is not taken from it (HighlightedText of a desktop is its own business).
        pal.setColor(QPalette::HighlightedText, Qt::white);
        QApplication::setPalette(pal);
        settle();
        Win wp = newWin();
        MainWindow& w = *wp;
        for (const char* command : {"CONVERT"}) {
            KeyButton* key = w.converter()->key(QLatin1String(command));
            const QImage img = key->grab().toImage();
            // the fill: the middle of the left part of the key, away from the text
            const QColor fill = img.pixelColor(img.width() / 10 + 3, img.height() / 2);
            QColor ink = fill;
            double best = 0;
            for (int y = img.height() / 4; y < img.height() * 3 / 4; ++y)
                for (int x = img.width() / 4; x < img.width() * 3 / 4; ++x) {
                    const double c = contrast(img.pixelColor(x, y), fill);
                    if (c > best) {
                        best = c;
                        ink = img.pixelColor(x, y);
                    }
                }
            yes(QStringLiteral("%1: the label of %2 reads on the key (ink %3 on %4, contrast %5 : 1)").arg(QString::fromLatin1(a.name), QString::fromLatin1(command), ink.name(), fill.name()).arg(best, 0, 'f', 1),
                best >= 4.5);
        }
    }
    QApplication::setPalette(original);
    settle();
}

// The theme modes of the front end on a light and on a dark desktop: Auto, and Light and Dark forced, each on both desktops.
struct ThemeCase {
    const char* name;
    bool desktopDark;
    MainWindow::ThemeMode mode;
    bool wantDark;      // the scheme the window ends up in
};

const ThemeCase* themeCases(int* count) {
    static const ThemeCase cases[] = {{"Auto on a light desktop", false, MainWindow::ThemeMode::Auto, false},
                                      {"Auto on a dark desktop", true, MainWindow::ThemeMode::Auto, true},
                                      {"Light on a light desktop", false, MainWindow::ThemeMode::Light, false},
                                      {"Light on a dark desktop", true, MainWindow::ThemeMode::Light, false},
                                      {"Dark on a light desktop", false, MainWindow::ThemeMode::Dark, true},
                                      {"Dark on a dark desktop", true, MainWindow::ThemeMode::Dark, true}};
    *count = int(sizeof(cases) / sizeof(cases[0]));
    return cases;
}

// A line of measured figures for a human; only with HCQT_TEST_VERBOSE set (the pass/fail counts do not depend on it).
void info(const QString& text) {
    if (qEnvironmentVariableIsSet("HCQT_TEST_VERBOSE")) std::printf("  [%s] %s\n", g_tag.toLocal8Bit().constData(), text.toLocal8Bit().constData());
}

// The address of the source in the About dialog reads in every theme mode: its colour comes from the palette of the dialog (the
// scheme that is in force, forced or followed), not from the palette of the desktop, which is what Qt uses for the link colour of
// rich text. Measured three ways: the contrast of the colour the dialog says against the window colour of the dialog, the same
// against the background pixel of the painted dialog, and the pixels of the painted label, among which the colour must be
// found (the underline). At least 4.5 : 1 (WCAG AA for text), in all six modes.
void aboutLinkContrast(const QPalette& lightDesktop, const QPalette& darkDesktop) {
    int count = 0;
    const ThemeCase* cases = themeCases(&count);
    for (int i = 0; i < count; ++i) {
        const ThemeCase& c = cases[i];
        QApplication::setPalette(c.desktopDark ? darkDesktop : lightDesktop);
        settle();
        Win wp = newWin();
        MainWindow& w = *wp;
        w.setThemeMode(c.mode);
        AboutDialog* d = w.createAbout();
        d->show();
        settle();
        settle();
        const QString what = QStringLiteral("About link, %1").arg(QString::fromLatin1(c.name));
        yes(what + QStringLiteral(": the dialog is %1").arg(c.wantDark ? "dark" : "light"), isDarkPalette(d->palette()) == c.wantDark);
        const QColor window = d->palette().color(QPalette::Window);
        const QColor link = d->linkColor();
        const double byPalette = contrastRatio(link, window);
        yes(what + QStringLiteral(": the link colour %1 on the dialog colour %2 is %3 : 1 (at least 4.5)").arg(link.name(), window.name()).arg(byPalette, 0, 'f', 2), byPalette >= 4.5);
        // the same through the document of the label: the colour that is in the markup is the one the dialog says
        QTextDocument doc;
        doc.setHtml(d->urlLabel()->text());
        QColor inMarkup;
        for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
            for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it)
                if (it.fragment().charFormat().isAnchor()) inMarkup = it.fragment().charFormat().foreground().color();
        eq(what + QStringLiteral(": the markup of the link carries the colour"), link.name(), inMarkup.name());
        // the painted dialog
        const QImage img = d->grab().toImage();
        const QRect label = d->urlLabel()->geometry();
        const QColor bg = img.pixelColor(label.right() - 2, label.center().y());
        eq(what + QStringLiteral(": the painted background is the window colour"), window.name(), bg.name());
        QColor ink = bg;
        double best = 0;
        int nearest = 1000;
        for (int y = label.top(); y <= label.bottom(); ++y)
            for (int x = label.left(); x <= label.right(); ++x) {
                const QColor px = img.pixelColor(x, y);
                const double cr = contrast(px, bg);
                if (cr > best) {
                    best = cr;
                    ink = px;
                }
                nearest = qMin(nearest, qAbs(px.red() - link.red()) + qAbs(px.green() - link.green()) + qAbs(px.blue() - link.blue()));
            }
        // (the strongest ink pixel is only reported: anti-aliasing may take a few levels off the colour of the underline)
        yes(what + QStringLiteral(": the link is painted in its colour (strongest ink %1, nearest pixel %2 off %3)").arg(ink.name()).arg(nearest).arg(link.name()), nearest <= 24);
        const double onPainted = contrast(link, bg);
        yes(what + QStringLiteral(": the link colour %1 on the painted background %2 is %3 : 1 (at least 4.5)").arg(link.name(), bg.name()).arg(onPainted, 0, 'f', 2), onPainted >= 4.5);
        info(QStringLiteral("About link, %1: %2 on %3 = %4 : 1 (painted: %5 on %6 = %7 : 1; the desktop's own link colour %8 would be %9 : 1)")
                 .arg(QString::fromLatin1(c.name), link.name(), window.name()).arg(byPalette, 0, 'f', 2).arg(ink.name(), bg.name()).arg(best, 0, 'f', 2)
                 .arg(QApplication::palette().color(QPalette::Link).name())
                 .arg(contrast(QApplication::palette().color(QPalette::Link), window), 0, 'f', 2));
        delete d;
    }
    QApplication::setPalette(lightDesktop);
    settle();
}

// The check box of Keep on top, in the open menu, in every theme mode: the item is painted off and on and the two pictures are
// compared. The box has to show when it is empty (its frame against the menu: at least 3 : 1, WCAG 1.4.11 for the parts of a
// control), and the tick has to show when it is on (at least 4.5 : 1 against the menu, and enough pixels that differ from the
// empty box). Measured on the painted menu, so it holds for whatever the style draws; the sizes come from the picture.
void keepOnTopIndicator(const QPalette& lightDesktop, const QPalette& darkDesktop) {
    int count = 0;
    const ThemeCase* cases = themeCases(&count);
    for (int i = 0; i < count; ++i) {
        const ThemeCase& c = cases[i];
        QApplication::setPalette(c.desktopDark ? darkDesktop : lightDesktop);
        settle();
        QImage shot[2];
        QRect row;
        for (int on = 0; on < 2; ++on) {
            Win wp = newWin(QString(), 1);
            MainWindow& w = *wp;
            w.setThemeMode(c.mode);
            if (on) w.setKeepOnTop(true);
            w.openMenu();
            settle();
            settle();
            row = w.menu()->actionGeometry(w.keepOnTopAction());
            shot[on] = w.menu()->grab().toImage();
            w.menu()->hide();
            settle();
        }
        const QString what = QStringLiteral("Keep on top box, %1").arg(QString::fromLatin1(c.name));
        const QImage& off = shot[0];
        const QImage& on = shot[1];
        yes(what + QStringLiteral(": both pictures of the menu have the same size"), off.size() == on.size() && !off.isNull());
        if (off.size() != on.size() || off.isNull()) continue;
        const QRect r = row.intersected(QRect(QPoint(0, 0), off.size()));
        yes(what + QStringLiteral(": the row of the item is in the picture"), r.width() > 20 && r.height() > 8);
        const QColor bg = off.pixelColor(r.right() - 4, r.center().y());
        QRect changed;
        int strong = 0;
        for (int y = r.top(); y <= r.bottom(); ++y)
            for (int x = r.left(); x <= r.right(); ++x) {
                const double d = contrast(on.pixelColor(x, y), off.pixelColor(x, y));
                if (d >= 1.15) changed = changed.united(QRect(x, y, 1, 1));
                if (d >= 3.0) ++strong;
            }
        yes(what + QStringLiteral(": the checked box differs from the empty one (%1 pixels that differ clearly)").arg(strong), !changed.isNull() && strong >= 8);
        const QRect around = changed.adjusted(-5, -5, 5, 5).intersected(r);
        double tick = 0, frame = 0;
        for (int y = around.top(); y <= around.bottom(); ++y)
            for (int x = around.left(); x <= around.right(); ++x) {
                tick = qMax(tick, contrast(on.pixelColor(x, y), bg));
                frame = qMax(frame, contrast(off.pixelColor(x, y), bg));
            }
        yes(what + QStringLiteral(": the tick reads on the menu (%1 : 1, at least 4.5)").arg(tick, 0, 'f', 2), tick >= 4.5);
        yes(what + QStringLiteral(": the empty box shows on the menu (%1 : 1, at least 3)").arg(frame, 0, 'f', 2), frame >= 3.0);
        info(QStringLiteral("Keep on top box, %1: menu %2, tick %3 : 1, empty box %4 : 1, %5 pixels differ").arg(QString::fromLatin1(c.name), bg.name()).arg(tick, 0, 'f', 2).arg(frame, 0, 'f', 2).arg(strong));
    }
    QApplication::setPalette(lightDesktop);
    settle();
}

// An icon theme with the icons the front end asks for (a 16 px square each): the menu icons are shown while the window follows
// the desktop and left out under a forced theme, and the menu button never uses an icon of the theme (it paints its bars).
void themeIcons() {
    QTemporaryDir dir;
    yes(QStringLiteral("temporary folder for the icon theme"), dir.isValid());
    QDir().mkpath(dir.filePath(QStringLiteral("hctest/16x16/actions")));
    QFile index(dir.filePath(QStringLiteral("hctest/index.theme")));
    yes(QStringLiteral("the icon theme can be written"), index.open(QIODevice::WriteOnly));
    index.write("[Icon Theme]\nName=hctest\nDirectories=16x16/actions\n\n[16x16/actions]\nSize=16\nType=Fixed\n");
    index.close();
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(QColor(0x40, 0x40, 0x40));
    for (const char* name : {"help-about", "open-menu-symbolic", "application-menu", "edit-copy"})
        yes(QStringLiteral("icon %1 written").arg(QString::fromLatin1(name)), img.save(dir.filePath(QStringLiteral("hctest/16x16/actions/%1.png").arg(QString::fromLatin1(name)))));
    const QStringList oldPaths = QIcon::themeSearchPaths();
    const QString oldName = QIcon::themeName();
    QIcon::setThemeSearchPaths(QStringList() << dir.path());
    QIcon::setThemeName(QStringLiteral("hctest"));
    yes(QStringLiteral("the test icon theme works"), !QIcon::fromTheme(QStringLiteral("help-about")).isNull());
    {
        Win wp = newWin();
        MainWindow& w = *wp;
        QAction* about = findAction(w.menu(), QStringLiteral("About HourCalc"));
        yes(QStringLiteral("About is in the menu"), about != nullptr);
        if (about) yes(QStringLiteral("Auto: the menu item has the icon of the theme"), !about->icon().isNull());
        yes(QStringLiteral("the menu button does not use an icon of the theme"), w.menuButton()->icon().isNull());
        w.setThemeMode(MainWindow::ThemeMode::Dark);
        if (about) yes(QStringLiteral("a forced theme: no icon in the menu (it would be coloured for the desktop)"), about->icon().isNull());
        yes(QStringLiteral("... the menu button still has none"), w.menuButton()->icon().isNull());
        w.setThemeMode(MainWindow::ThemeMode::Auto);
        if (about) yes(QStringLiteral("Auto again: the icon is back"), !about->icon().isNull());
        // the Copy item of a calculator value
        w.selectTab(1);
        keys(w, QStringLiteral("7:30+1="));
        for (bool forced : {false, true}) {
            w.setThemeMode(forced ? MainWindow::ThemeMode::Dark : MainWindow::ThemeMode::Auto);
            DisplayLabel* label = w.calculator()->mainLabel();
            QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(5, 5), label->mapToGlobal(QPoint(5, 5)));
            QApplication::sendEvent(label, &ev);
            settle();
            QMenu* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            yes(QStringLiteral("the Copy menu opens (%1)").arg(forced ? "forced theme" : "Auto"), popup != nullptr);
            if (popup) {
                yes(QStringLiteral("... its item has %1 icon of the theme").arg(forced ? "no" : "the"), popup->actions().first()->icon().isNull() == forced);
                popup->close();
                settle();
            }
        }
    }
    QIcon::setThemeSearchPaths(oldPaths);
    QIcon::setThemeName(oldName);
}

// The theme: Auto follows the desktop, Light and Dark force one (SPEC 6b).
void themes() {
    const QPalette original = QApplication::palette();
    const QPalette darkDesktop = schemePalette(true, original);
    yes(QStringLiteral("the offscreen desktop is light"), !isDarkPalette(original));
    yes(QStringLiteral("the dark palette is dark"), isDarkPalette(darkDesktop));

    {
        Win wp = newWin();
        MainWindow& w = *wp;
        yes(QStringLiteral("Auto on a light desktop is light"), !w.isDark());
        w.setThemeMode(MainWindow::ThemeMode::Dark);
        yes(QStringLiteral("Dark makes the window dark"), w.isDark());
        yes(QStringLiteral("... the theme choice is Dark"), w.themeMode() == MainWindow::ThemeMode::Dark);
        eq(QStringLiteral("... the menu says Dark"), QStringLiteral("Dark"), checkedItems(w.menu()->actions().at(1)->actionGroup()));
        yes(QStringLiteral("... the menu is dark too"), isDarkPalette(w.menu()->palette()));
        AboutDialog* about = w.createAbout();
        yes(QStringLiteral("... and so is the About dialog"), isDarkPalette(about->palette()));
        delete about;
        w.setThemeMode(MainWindow::ThemeMode::Light);
        yes(QStringLiteral("Light makes it light"), !w.isDark());
        w.setThemeMode(MainWindow::ThemeMode::Dark);
        w.setThemeMode(MainWindow::ThemeMode::Auto);
        yes(QStringLiteral("Auto after Dark is the desktop again"), !w.isDark());
        eq(QStringLiteral("... the menu says Auto"), QStringLiteral("Auto (follow system)"), checkedItems(w.menu()->actions().at(1)->actionGroup()));

        // the accent: Convert and = use the highlight of the palette
        w.setThemeMode(MainWindow::ThemeMode::Dark);
        w.show();
        settle();
        const QImage img = w.grab().toImage();
        KeyButton* convert = w.converter()->key(QStringLiteral("CONVERT"));
        const QColor at = img.pixelColor(convert->mapTo(&w, convert->rect().center()));
        const QColor accent = w.palette().color(QPalette::Highlight);
        auto dist = [](const QColor& a, const QColor& b) { return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue()); };
        const QColor plain = img.pixelColor(w.converter()->key(QStringLiteral("5"))->mapTo(&w, QPoint(8, w.converter()->key(QStringLiteral("5"))->height() / 2)));
        yes(QStringLiteral("the Convert key is the accent colour (%1 vs the highlight %2, a plain key %3)").arg(at.name(), accent.name(), plain.name()), dist(at, accent) < dist(at, plain) && dist(at, accent) < 160);
    }

    // a light desktop, a dark one, and the desktop changing while the windows are open
    {
        Win a = newWin();                                        // Auto
        Win l = newWin();
        l->setThemeMode(MainWindow::ThemeMode::Light);           // forced Light on a light desktop
        Win d = newWin();
        d->setThemeMode(MainWindow::ThemeMode::Dark);            // forced Dark on a light desktop
        QApplication::setPalette(darkDesktop);
        settle();
        yes(QStringLiteral("Auto follows the desktop to dark, live"), a->isDark());
        yes(QStringLiteral("forced Light ignores the desktop going dark"), !l->isDark());
        yes(QStringLiteral("forced Dark stays dark"), d->isDark());
        Win a2 = newWin();
        yes(QStringLiteral("a new Auto window on a dark desktop is dark"), a2->isDark());
        Win l2 = newWin();
        l2->setThemeMode(MainWindow::ThemeMode::Light);
        yes(QStringLiteral("Light on a dark desktop is light"), !l2->isDark());
        Win d2 = newWin();
        d2->setThemeMode(MainWindow::ThemeMode::Dark);
        yes(QStringLiteral("Dark on a dark desktop is dark"), d2->isDark());
        QApplication::setPalette(original);
        settle();
        yes(QStringLiteral("Auto follows the desktop back to light, live"), !a->isDark());
        yes(QStringLiteral("forced Light stays light"), !l->isDark());
        yes(QStringLiteral("forced Dark ignores the desktop going light"), d->isDark());
        yes(QStringLiteral("forced Light on a dark desktop stays light when the desktop goes light"), !l2->isDark());
        yes(QStringLiteral("forced Dark on a dark desktop ignores the desktop going light"), d2->isDark());
        yes(QStringLiteral("Auto (a2) is light again"), !a2->isDark());
        // the accent of a forced theme follows the desktop
        QPalette accented = original;
        accented.setColor(QPalette::Active, QPalette::Highlight, QColor(0xe0, 0x4a, 0x2f));
        QApplication::setPalette(accented);
        settle();
        yes(QStringLiteral("a forced theme takes the accent of the desktop (%1)").arg(d->palette().color(QPalette::Highlight).name()),
            d->palette().color(QPalette::Active, QPalette::Highlight).red() > 200 && d->isDark());
        QApplication::setPalette(original);
        settle();
    }
    forcedThemeAtStart(original, darkDesktop);
    accentLabel();
    aboutLinkContrast(original, darkDesktop);
    keepOnTopIndicator(original, darkDesktop);
    themeIcons();

    // the style hint is asked for, and put back by Auto
    {
        Win wp = newWin();
        wp->setThemeMode(MainWindow::ThemeMode::Dark);
        wp->setThemeMode(MainWindow::ThemeMode::Auto);
        yes(QStringLiteral("after Auto the colour scheme of the application is not forced"),
            QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Unknown || QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light);
    }
}

// ------------------------------------------------------------------ About

void aboutDialog() {
    Win wp = newWin();
    AboutDialog* d = wp->createAbout();
    eq(QStringLiteral("About title"), QStringLiteral("About HourCalc"), d->windowTitle());
    eq(QStringLiteral("About version"), QStringLiteral("1.0.0"), AboutDialog::version());
    eq(QStringLiteral("About copyright"), QStringLiteral("Copyright (c) 2026 Optimey CommV"), AboutDialog::copyright());
    QStringList buttons = d->buttonTexts();
    buttons.sort();
    eq(QStringLiteral("About buttons"), QStringLiteral("Close|View license"), buttons.join(QLatin1Char('|')));
    const QString notice = d->noticeText();
    yes(QStringLiteral("About shows the GPL notice (free software, version 3 or later, no warranty)"),
        notice.contains(QStringLiteral("free software")) && notice.contains(QStringLiteral("version 3")) && notice.contains(QStringLiteral("any later version")) &&
            notice.contains(QStringLiteral("ABSOLUTELY NO WARRANTY")));
    eq(QStringLiteral("the address of the source code is the placeholder"), QStringLiteral("https://github.com/Optimey-CommV/hourcalc"), d->urlText());
    yes(QStringLiteral("About is modal"), d->isModal());
    d->show();
    settle();
    yes(QStringLiteral("About can be shown"), d->isVisible());
    yes(QStringLiteral("About is not clipped (its size hint fits)"), d->width() >= d->sizeHint().width() && d->height() >= d->sizeHint().height());
    LicenseDialog* l = d->createLicenseDialog();
    const QString text = l->view()->toPlainText();
    yes(QStringLiteral("the license text is the GPL"), text.contains(QStringLiteral("GNU GENERAL PUBLIC LICENSE")) && text.contains(QStringLiteral("Version 3, 29 June 2007")));
    yes(QStringLiteral("the license text is the whole license (%1 characters)").arg(text.size()), text.size() > 30000 && text.contains(QStringLiteral("END OF TERMS AND CONDITIONS")));
    yes(QStringLiteral("the license view is read-only"), l->view()->isReadOnly());
    eq(QStringLiteral("the license text is the file of the distribution"), licenseText().left(60), text.left(60));
    delete l;
    delete d;
}

// ------------------------------------------------------------------ layout: nothing clipped

// Every visible child lies within its parent, every text fits the widget that shows it, at every size.
void checkLayout(MainWindow& w, const QString& tag) {
    settle();
    const QSize layoutMin = w.layout()->minimumSize();
    yes(tag + QStringLiteral(": the layout fits the minimum window (%1x%2 in %3x%4)").arg(layoutMin.width()).arg(layoutMin.height()).arg(w.minimumWindowSize().width()).arg(w.minimumWindowSize().height()),
        layoutMin.width() <= w.minimumWindowSize().width() && layoutMin.height() <= w.minimumWindowSize().height());
    int outside = 0;
    QString which;
    const auto widgets = w.findChildren<QWidget*>();
    for (QWidget* child : widgets) {
        if (!child->isVisible() || child->isWindow()) continue;
        QWidget* parent = child->parentWidget();
        if (!parent) continue;
        // internal parts of QLineEdit / QTabBar / QStackedWidget are not ours to check
        if (qobject_cast<QLineEdit*>(parent) || qobject_cast<QTabBar*>(parent) || qobject_cast<QPlainTextEdit*>(parent)) continue;
        if (!QRect(QPoint(0, 0), parent->size()).contains(child->geometry())) {
            ++outside;
            which += child->metaObject()->className() + QStringLiteral(" ");
        }
    }
    eq(tag + QStringLiteral(": all widgets lie inside their parents (%1)").arg(which), QStringLiteral("0"), I(outside));

    // the header: tabs and the menu button side by side without overlap
    yes(tag + QStringLiteral(": the tab bar and the menu button do not overlap"), w.tabBar()->geometry().right() < w.menuButton()->geometry().left());
    yes(tag + QStringLiteral(": the tab bar shows both tabs"), w.tabBar()->sizeHint().width() <= w.tabBar()->width() + 1);

    // keys: every label fits its key
    auto keysFit = [&](const QList<KeyButton*>& list, const QString& page) {
        int bad = 0;
        QString names;
        for (KeyButton* b : list) {
            if (b->label().isEmpty()) continue;
            const QFontMetricsF fm(b->font());
            if (fm.horizontalAdvance(b->label()) + 8 > b->width() || fm.height() > b->height()) {
                ++bad;
                names += b->command() + QLatin1Char(' ');
            }
        }
        eq(tag + QStringLiteral(": every %1 key label fits its key (%2)").arg(page, names), QStringLiteral("0"), I(bad));
        int sizes = 0;
        for (KeyButton* b : list) {
            if (b->width() < 8 || b->height() < 8) ++sizes;
        }
        eq(tag + QStringLiteral(": every %1 key has a usable size").arg(page), QStringLiteral("0"), I(sizes));
    };
    // (the page that is not shown has not been laid out)
    if (w.currentTab() == 0) keysFit(w.converter()->keys(), QStringLiteral("converter"));
    else keysFit(w.calculator()->keys(), QStringLiteral("calculator"));

    // the keys of a page are one size (same column width, same row height)
    auto sameSize = [&](const QList<KeyButton*>& list, const QString& page) {
        int minW = 100000, maxW = 0, minH = 100000, maxH = 0;
        for (KeyButton* b : list) {
            if (b->command() == QLatin1String("CONVERT")) continue;
            minW = qMin(minW, b->width());
            maxW = qMax(maxW, b->width());
            minH = qMin(minH, b->height());
            maxH = qMax(maxH, b->height());
        }
        yes(tag + QStringLiteral(": %1 keys are one width (%2..%3) and one height (%4..%5)").arg(page).arg(minW).arg(maxW).arg(minH).arg(maxH), maxW - minW <= 2 && maxH - minH <= 2);
    };
    if (w.currentTab() == 0) sameSize(w.converter()->keys(), QStringLiteral("converter"));
    else sameSize(w.calculator()->keys(), QStringLiteral("calculator"));

    // labels
    for (int f = 0; f < 2; ++f) {
        const QLabel* lc = w.converter()->fieldLabel(f);
        yes(tag + QStringLiteral(": converter label %1 fits").arg(f), QFontMetricsF(lc->font()).horizontalAdvance(lc->text()) <= lc->width());
        const QLabel* lk = w.calculator()->fieldLabel(f);
        if (lk->isVisible()) yes(tag + QStringLiteral(": calculator label %1 fits").arg(f), QFontMetricsF(lk->font()).horizontalAdvance(lk->text()) <= lk->width());
    }
}

void fits(const QString& tag, const QFont& font, const QString& text, double width) {
    const double need = QFontMetricsF(font).horizontalAdvance(text);
    yes(QStringLiteral("%1: <%2> fits (%3 of %4)").arg(tag, text).arg(need, 0, 'f', 1).arg(width, 0, 'f', 1), need <= width + 0.5);
}

void layoutAtSizes() {
    struct Sz {
        const char* name;
        double wFactor, hFactor;       // of the default size
        bool minimum;
    };
    const Sz sizes[] = {{"minimum", 0, 0, true}, {"default", 1, 1, false}, {"1.5x", 1.5, 1.5, false}, {"2x", 2, 2, false}, {"wide", 2, 1, false}, {"tall", 1, 1.6, false}};
    double defaultPt = 0, bigPt = 0;
    for (const Sz& s : sizes) {
        Win wp = newWin();
        MainWindow& w = *wp;
        const QSize size = s.minimum ? w.minimumWindowSize() : QSize(qRound(w.defaultSize().width() * s.wFactor), qRound(w.defaultSize().height() * s.hFactor));
        w.resize(size);
        settle();
        const QString tag = QStringLiteral("%1 (%2x%3)").arg(QLatin1String(s.name)).arg(size.width()).arg(size.height());
        eq(tag + QStringLiteral(": the window has the size asked for"), QStringLiteral("%1x%2").arg(size.width()).arg(size.height()), QStringLiteral("%1x%2").arg(w.width()).arg(w.height()));

        // converter: idle, a long value, the longest value
        for (const char* text : {"", "08:30", "9999999:99", "123456:59"}) {
            w.selectTab(0);
            w.press(QStringLiteral("RESET"));
            w.converter()->setFieldText(0, QLatin1String(text));
            settle();
            checkLayout(w, tag + QStringLiteral(" converter <%1>").arg(QLatin1String(text)));
            for (int f = 0; f < 2; ++f) {
                DisplayEdit* e = w.converter()->edit(f);
                fits(tag + QStringLiteral(" converter field %1").arg(f), e->font(), e->text(), e->textAreaWidth());
                yes(tag + QStringLiteral(" converter field %1 is tall enough for its font").arg(f), QFontMetricsF(e->font()).height() <= e->height());
            }
            fits(tag + QStringLiteral(" converter caption"), w.converter()->captionLabel()->font(), w.converter()->caption(), w.converter()->captionLabel()->width());
        }
        // calculator: a result, the error text, a long entry, a negative result
        w.selectTab(1);
        const char* sequences[] = {"7:30+7:20=", "7:30/0=", "1234567,123456", "9999999:59+1:00=", "7:00-8:30="};
        for (const char* seq : sequences) {
            w.press(QStringLiteral("C"));
            keys(w, QLatin1String(seq));
            settle();
            const QString t = tag + QStringLiteral(" calculator <%1>").arg(QLatin1String(seq));
            checkLayout(w, t);
            CalculatorPage* c = w.calculator();
            fits(t + QStringLiteral(" left"), c->mainLabel()->font(), c->mainText(), c->mainLabel()->width());
            if (c->rightVisible()) fits(t + QStringLiteral(" right"), c->decimalLabel()->font(), c->decimalText(), c->decimalLabel()->width());
            fits(t + QStringLiteral(" caption"), c->captionLabel()->font(), c->captionText(), c->captionLabel()->width());
            yes(t + QStringLiteral(": both values use one font size"), qFuzzyCompare(c->mainLabel()->font().pointSizeF(), c->decimalLabel()->font().pointSizeF()));
        }
        w.press(QStringLiteral("C"));
        settle();
        if (QLatin1String(s.name) == QLatin1String("default")) defaultPt = w.calculator()->valuePointSize();
        if (QLatin1String(s.name) == QLatin1String("2x")) bigPt = w.calculator()->valuePointSize();
    }
    yes(QStringLiteral("the values grow with the window (%1 pt at the default size, %2 pt at twice that)").arg(defaultPt).arg(bigPt), bigPt > defaultPt * 1.5);

    // the default window: 20 x 34 units, minimum 17 x 28
    Win wp = newWin();
    const double u = unitPx();
    eq(QStringLiteral("default size in units"), QStringLiteral("%1x%2").arg(qRound(kDefaultWidth * u)).arg(qRound(kDefaultHeight * u)),
       QStringLiteral("%1x%2").arg(wp->defaultSize().width()).arg(wp->defaultSize().height()));
    eq(QStringLiteral("the window opens at the default size"), QStringLiteral("%1x%2").arg(wp->defaultSize().width()).arg(wp->defaultSize().height()),
       QStringLiteral("%1x%2").arg(wp->width()).arg(wp->height()));
    yes(QStringLiteral("the minimum size is set on the window"), wp->minimumSize() == wp->minimumWindowSize());
}

// The sizes follow the font of the desktop: a larger interface font makes a larger window and larger keys.
void followsTheFont() {
    const QFont original = QApplication::font();
    Win a = newWin();
    const QSize s1 = a->defaultSize();
    const QSize k1 = a->converter()->key(QStringLiteral("5"))->size();
    const double f1 = a->converter()->key(QStringLiteral("5"))->font().pointSizeF();
    QFont big = original;
    big.setPointSizeF(original.pointSizeF() * 1.5);
    QApplication::setFont(big);
    settle();
    QTest::qWait(50);
    settle();
    const double f2 = a->converter()->key(QStringLiteral("5"))->font().pointSizeF();
    yes(QStringLiteral("an open window follows a change of the interface font live (key font %1 pt -> %2 pt)").arg(f1).arg(f2), f2 > f1 * 1.3);
    Win b = newWin();
    const QSize s2 = b->defaultSize();
    yes(QStringLiteral("a 1.5x interface font gives a larger default window (%1x%2 -> %3x%4)").arg(s1.width()).arg(s1.height()).arg(s2.width()).arg(s2.height()),
        s2.width() > s1.width() * 1.3 && s2.height() > s1.height() * 1.3);
    const QSize k2 = b->converter()->key(QStringLiteral("5"))->size();
    yes(QStringLiteral("... and larger keys (%1x%2 -> %3x%4)").arg(k1.width()).arg(k1.height()).arg(k2.width()).arg(k2.height()), k2.width() > k1.width() * 1.3);
    checkLayout(*b, QStringLiteral("1.5x font"));
    b->resize(b->minimumWindowSize());
    checkLayout(*b, QStringLiteral("1.5x font, minimum"));
    b.reset();
    QApplication::setFont(original);
    settle();
}

// Another style (the metrics of Breeze are not there to test; the classic Windows style is as different from
// Fusion as a style gets): nothing is clipped in it either, also when the style is changed while the window is open.
void followsTheStyle() {
    const QString original = QApplication::style()->objectName();
    Win a = newWin();
    a->selectTab(1);
    keys(*a, QStringLiteral("7:30+7:20="));
    QApplication::setStyle(QStringLiteral("windows"));
    settle();
    QTest::qWait(50);
    settle();
    eq(QStringLiteral("the style was changed"), QStringLiteral("windows"), QApplication::style()->objectName());
    a->resize(a->defaultSize());
    settle();
    checkLayout(*a, QStringLiteral("Windows style, changed while open"));
    a->selectTab(0);
    a->converter()->setFieldText(0, QStringLiteral("9999999:99"));
    settle();
    checkLayout(*a, QStringLiteral("Windows style, converter, long value"));
    a->resize(a->minimumWindowSize());
    settle();
    checkLayout(*a, QStringLiteral("Windows style, converter, minimum"));
    a.reset();
    Win b = newWin();
    b->resize(b->minimumWindowSize());
    b->selectTab(1);
    keys(*b, QStringLiteral("7:30/0="));
    settle();
    checkLayout(*b, QStringLiteral("Windows style, calculator, error, minimum"));
    b.reset();
    QApplication::setStyle(original);
    settle();
    eq(QStringLiteral("the style is back"), original, QApplication::style()->objectName());
}

void runGroups() {
    converterWiring();
    converterEditing();
    converterTyping();
    converterState();
    converterFocusRules();
    converterConvertKeepsValue();
    converterKeyboard();
    calculatorTable();
    calculatorWiring();
    calculatorMirror();
    calculatorKeyboard();
    calculatorExpressionLine();
    calculatorClipboard();
    tabsAndShortcuts();
    numberRowKeys();
    quitAndClose();
    menuAndPlaces();
    keepOnTop();
    settingsFile();
    themes();
    aboutDialog();
    layoutAtSizes();
    followsTheFont();
    followsTheStyle();
}

}  // namespace

int runSelfTest() {
    int perRun[2][2] = {};
    const wchar_t* seps[2] = {L",", L"."};
    for (int i = 0; i < 2; ++i) {
        hc::timemath::SetDecimalSeparatorOverride(seps[i]);
        g_sep = q(seps[i]);
        g_tag = QStringLiteral("decimal separator '%1'").arg(g_sep);
        const int checks0 = g_checks, fails0 = g_failures;
        runGroups();
        perRun[i][0] = g_checks - checks0;
        perRun[i][1] = g_failures - fails0;
        std::printf("%s: %d checks, %d failures\n", g_tag.toLocal8Bit().constData(), perRun[i][0], perRun[i][1]);
    }
    hc::timemath::SetDecimalSeparatorOverride(L"");
    std::printf("selftest: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

}  // namespace testing
}  // namespace hcqt
