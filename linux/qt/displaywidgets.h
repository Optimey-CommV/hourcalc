// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// The two display widgets of the converter and the calculator: a label that can be drawn in the
// secondary text colour and offers Copy on the context menu, and the editable value of the converter,
// a QLineEdit with the behaviour of the Windows version (select all when it gets the focus by a click or
// Tab, a 2 px accent underline on the active field, Tab moves between the two fields, Enter converts,
// Esc resets).

#pragma once

#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPoint>

namespace hcqt {

class DisplayLabel : public QLabel {
    Q_OBJECT
public:
    explicit DisplayLabel(const QString& text = QString(), QWidget* parent = nullptr);

    // Secondary text colour (field labels, caption, expression line).
    void setSecondary(bool on);
    bool isSecondary() const { return secondary_; }
    // Right click offers Copy (the values of the calculator).
    void setCopyable(bool on) { copyable_ = on; }

signals:
    void copyRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    bool secondary_ = false;
    bool copyable_ = false;
};

class DisplayEdit : public QLineEdit {
    Q_OBJECT
public:
    // field: 0 = hours : minutes, 1 = decimal hours
    explicit DisplayEdit(int field, QWidget* parent = nullptr);

    int field() const { return field_; }
    // The accent underline of the active field.
    void setUnderline(bool on);
    bool underline() const { return underline_; }
    // The width in which the text can be set without being clipped (device independent pixels).
    double textAreaWidth() const;

signals:
    void focused(int field);          // the field got the keyboard focus (any reason)
    void enterPressed();              // Convert
    void escapePressed();             // Reset
    void switchField(int from);       // Tab / Shift+Tab: the other field gets the focus, with its text selected

protected:
    void focusInEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool focusNextPrevChild(bool next) override;
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    int field_;
    bool underline_ = false;
    bool clickFocus_ = false;      // the focus came with the mouse press that is being handled
    bool swallowDrag_ = false;     // the press selected everything: no caret placing, no drag selection
    bool tripleArmed_ = false;     // the last click was a double click
    QElapsedTimer tripleTimer_;
    QElapsedTimer activated_;      // when the window was activated last
    QPoint triplePos_;
};

}  // namespace hcqt
