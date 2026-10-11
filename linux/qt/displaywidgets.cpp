// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displaywidgets.h"

#include "util.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionFrame>

namespace hcqt {

// ------------------------------------------------------------------ DisplayLabel

DisplayLabel::DisplayLabel(const QString& text, QWidget* parent) : QLabel(text, parent) {
    setTextFormat(Qt::PlainText);
    setIndent(0);
    setMargin(0);
}

void DisplayLabel::setSecondary(bool on) {
    if (secondary_ == on) return;
    secondary_ = on;
    update();
}

void DisplayLabel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    QPalette pal = palette();
    if (secondary_) pal.setColor(QPalette::WindowText, secondaryText(palette()));
    style()->drawItemText(&painter, contentsRect(), static_cast<int>(alignment()) | Qt::TextSingleLine, pal, isEnabled(), text(),
                          QPalette::WindowText);
}

void DisplayLabel::contextMenuEvent(QContextMenuEvent* event) {
    if (!copyable_) {
        event->ignore();
        return;
    }
    QMenu* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setPalette(window()->palette());
    // The icon of the icon theme is coloured for the palette of the application: not under a palette of the window's own.
    const bool forced = window()->testAttribute(Qt::WA_SetPalette);
    QAction* copy = menu->addAction(forced ? QIcon() : QIcon::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Copy"));
    connect(copy, &QAction::triggered, this, &DisplayLabel::copyRequested);
    menu->popup(event->globalPos());
    event->accept();
}

// ------------------------------------------------------------------ DisplayEdit

DisplayEdit::DisplayEdit(int field, QWidget* parent) : QLineEdit(parent), field_(field) {
    setFrame(false);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setInputMethodHints(Qt::ImhPreferNumbers | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase);
    setClearButtonEnabled(false);
    setDragEnabled(false);
    // The style paints a frameless line edit on the colour of the text field: let the window show through,
    // so the value sits on the page like the calculator display does.
    QPalette pal = palette();
    pal.setColor(QPalette::Base, Qt::transparent);
    setPalette(pal);
    setTextMargins(0, 0, 3, 0);
}

void DisplayEdit::setUnderline(bool on) {
    if (underline_ == on) return;
    underline_ = on;
    update();
}

double DisplayEdit::textAreaWidth() const {
    QStyleOptionFrame option;
    initStyleOption(&option);
    const QRect r = style()->subElementRect(QStyle::SE_LineEditContents, &option, this);
    const QMargins m = textMargins();
    // QLineEdit keeps 2 px free on both sides of its text; one more for the caret at the end.
    return qMax(0, r.width() - m.left() - m.right() - 2 * 2 - 3);
}

void DisplayEdit::paintEvent(QPaintEvent* event) {
    QLineEdit::paintEvent(event);
    if (underline_) {
        QPainter painter(this);
        const int t = 2;
        painter.fillRect(QRect(0, height() - t, width(), t), palette().color(QPalette::Active, QPalette::Highlight));
    }
}

bool DisplayEdit::event(QEvent* event) {
    // A click on a window that was not active activates it, and the focus comes back to the field with it:
    // that click selects all, like a click on a field that did not have the focus. The activation is noted.
    if (event->type() == QEvent::WindowActivate) activated_.start();
    return QLineEdit::event(event);
}

void DisplayEdit::focusInEvent(QFocusEvent* event) {
    // The base class selects everything for Tab, Shift+Tab and shortcut focus, and keeps the caret and the
    // text for every other reason: window activation (Alt+Tab back), setFocus() from the keypad.
    clickFocus_ = event->reason() == Qt::MouseFocusReason;
    QLineEdit::focusInEvent(event);
    emit focused(field_);
}

// A left click on a field that did not have the focus (or in a window that was not active) selects all of
// its text instead of placing the caret, so typing replaces a computed value. A click on a field that has
// the focus places the caret as usual; a double click and the triple click select everything.
void DisplayEdit::mousePressEvent(QMouseEvent* event) {
    const bool justActivated = activated_.isValid() && activated_.elapsed() < 150;
    const bool within = isActiveWindow() && hasFocus() && !clickFocus_ && !justActivated;
    clickFocus_ = false;
    if (event->button() == Qt::LeftButton) {
        const bool triple = tripleArmed_ && tripleTimer_.isValid() && tripleTimer_.elapsed() <= QApplication::doubleClickInterval() &&
                            (event->position().toPoint() - triplePos_).manhattanLength() <= QApplication::startDragDistance();
        tripleArmed_ = false;
        if (!within || triple) {
            if (!hasFocus()) setFocus(Qt::MouseFocusReason);
            selectAll();
            swallowDrag_ = true;
            event->accept();
            return;
        }
    } else if (event->button() == Qt::RightButton && !within) {
        // Like a text box: the right click gives the field the focus (a field that did not have it selects all).
        if (!hasFocus()) setFocus(Qt::MouseFocusReason);
        selectAll();
    }
    QLineEdit::mousePressEvent(event);
}

void DisplayEdit::mouseMoveEvent(QMouseEvent* event) {
    if (swallowDrag_) {
        event->accept();
        return;
    }
    QLineEdit::mouseMoveEvent(event);
}

void DisplayEdit::mouseReleaseEvent(QMouseEvent* event) {
    clickFocus_ = false;
    if (swallowDrag_) {
        swallowDrag_ = false;
        event->accept();
        return;
    }
    QLineEdit::mouseReleaseEvent(event);
}

void DisplayEdit::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QLineEdit::mouseDoubleClickEvent(event);
        return;
    }
    selectAll();
    swallowDrag_ = true;
    tripleArmed_ = true;
    tripleTimer_.start();
    triplePos_ = event->position().toPoint();
    event->accept();
}

void DisplayEdit::keyPressEvent(QKeyEvent* event) {
    // Ctrl + a key of the number row is the window's (tab switch), not a character for the field.
    if (digitRowTab(event) >= 0) {
        event->ignore();
        return;
    }
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit enterPressed();
        event->accept();
        return;
    case Qt::Key_Escape:
        emit escapePressed();
        event->accept();
        return;
    default:
        break;
    }
    QLineEdit::keyPressEvent(event);
}

// Tab and Shift+Tab switch between the two fields only (the keypad and the header never take the focus).
bool DisplayEdit::focusNextPrevChild(bool) {
    emit switchField(field_);
    return true;
}

// The standard menu (Undo, Cut, Copy, Paste, Delete, Select All), in the palette of the window: a
// popup is a window of its own and does not inherit a palette that was set on the main window.
void DisplayEdit::contextMenuEvent(QContextMenuEvent* event) {
    QMenu* menu = createStandardContextMenu();
    if (!menu) return;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setPalette(window()->palette());
    menu->popup(event->globalPos());
    event->accept();
}

}  // namespace hcqt
