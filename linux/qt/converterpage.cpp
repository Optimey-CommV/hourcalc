// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

#include "converterpage.h"

#include "displaywidgets.h"
#include "keybutton.h"
#include "layoutconst.h"
#include "util.h"

#include "hc_core.h"

#include <QEvent>
#include <QGridLayout>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <cmath>

namespace hcqt {

namespace conv = hc::converter;

namespace {

// conv::Convert without an exception ever leaving it: text the core cannot take converts to nothing.
void convertSafe(int field, const QString& text, int places, std::wstring& normal, std::wstring& other, std::wstring& cap) {
    normal.clear();
    other.clear();
    cap.clear();
    guarded([&] { conv::Convert(field, w(text), places, normal, other, cap); });
}

}  // namespace

ConverterPage::ConverterPage(QWidget* parent) : QWidget(parent), places_(hc::timemath::DefaultDecimals) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    auto* display = new QGridLayout();
    display->setVerticalSpacing(0);
    display->setColumnStretch(0, 1);
    display->setColumnStretch(1, 1);
    display->setColumnMinimumWidth(0, 1);
    display->setColumnMinimumWidth(1, 1);

    const QString captions[2] = {QStringLiteral("Hours : minutes"), QStringLiteral("Decimal hours")};
    for (int f = 0; f < 2; ++f) {
        labels_[f] = new DisplayLabel(captions[f], this);
        labels_[f]->setSecondary(true);
        labels_[f]->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        labels_[f]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        edits_[f] = new DisplayEdit(f, this);
        edits_[f]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        edits_[f]->setAccessibleName(captions[f]);
        edits_[f]->installEventFilter(this);
        display->addWidget(labels_[f], 0, f);
        display->addWidget(edits_[f], 1, f);

        connect(edits_[f], &QLineEdit::textEdited, this, [this, f](const QString& text) { applyEdit(f, text, edits_[f]->cursorPosition(), true); });
        connect(edits_[f], &DisplayEdit::focused, this, [this](int field) { activate(field); });
        connect(edits_[f], &DisplayEdit::enterPressed, this, [this] { press(QStringLiteral("CONVERT")); });
        connect(edits_[f], &DisplayEdit::escapePressed, this, [this] { press(QStringLiteral("RESET")); });
        connect(edits_[f], &DisplayEdit::switchField, this, [this](int from) { edits_[1 - from]->setFocus(Qt::TabFocusReason); });
    }
    caption_ = new DisplayLabel(QString(), this);
    caption_->setSecondary(true);
    caption_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    caption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    caption_->setAccessibleDescription(QStringLiteral("Value in words"));
    display->addWidget(caption_, 2, 0);
    root->addLayout(display);

    keypad_ = new QGridLayout();
    root->addLayout(keypad_, 1);
    buildKeypad();

    edits_[Left]->setUnderline(true);
    updateKeyStates();
    refreshMetrics();
}

// ------------------------------------------------------------------ keypad

void ConverterPage::buildKeypad() {
    const QString sep = q(hc::timemath::DecimalSeparator());
    auto add = [&](const QString& command, const QString& label, KeyButton::Kind kind, KeyButton::Glyph glyph, int row, int col, int span,
                   const QString& name) {
        auto* b = new KeyButton(command, label, kind, glyph, this);
        if (!name.isEmpty()) {
            b->setAccessibleName(name);
            b->setToolTip(QString());
        }
        keypad_->addWidget(b, row, col, 1, span);
        keys_.insert(command, b);
        keyList_.append(b);
        connect(b, &QPushButton::clicked, this, [this, command] { press(command); });
        return b;
    };
    add(QStringLiteral("RESET"), QStringLiteral("Reset"), KeyButton::Function, KeyButton::NoGlyph, 0, 0, 1, QStringLiteral("Reset"));
    add(QStringLiteral("CE"), QStringLiteral("CE"), KeyButton::Function, KeyButton::NoGlyph, 0, 1, 1, QStringLiteral("Clear entry"));
    add(QStringLiteral("BACK"), QString(), KeyButton::Function, KeyButton::Backspace, 0, 2, 1, QStringLiteral("Backspace"));
    const char* digits[9] = {"7", "8", "9", "4", "5", "6", "1", "2", "3"};
    for (int i = 0; i < 9; ++i)
        add(QLatin1String(digits[i]), QLatin1String(digits[i]), KeyButton::Digit, KeyButton::NoGlyph, 1 + i / 3, i % 3, 1, QString());
    add(QStringLiteral(":"), QStringLiteral(":"), KeyButton::Digit, KeyButton::NoGlyph, 4, 0, 1, QStringLiteral("Time separator"));
    add(QStringLiteral("0"), QStringLiteral("0"), KeyButton::Digit, KeyButton::NoGlyph, 4, 1, 1, QString());
    add(QStringLiteral(","), sep, KeyButton::Digit, KeyButton::NoGlyph, 4, 2, 1, QStringLiteral("Decimal separator"));
    add(QStringLiteral("CONVERT"), QStringLiteral("Convert"), KeyButton::Accent, KeyButton::NoGlyph, 5, 0, 3, QStringLiteral("Convert"));
    for (int r = 0; r < 6; ++r) keypad_->setRowStretch(r, 1);
    for (int c = 0; c < 3; ++c) keypad_->setColumnStretch(c, 1);
}

// ------------------------------------------------------------------ state

QString ConverterPage::fieldText(int field) const { return edits_[field]->text(); }
QString ConverterPage::caption() const { return caption_->text(); }

int ConverterPage::focusField() const {
    for (int f = 0; f < 2; ++f)
        if (edits_[f]->hasFocus()) return f;
    return -1;
}

bool ConverterPage::keyEnabled(const QString& command) const {
    KeyButton* b = keys_.value(command);
    return b ? b->isEnabled() : false;
}

void ConverterPage::activate(int field) {
    active_ = field;
    updateKeyStates();
}

// ":" is for the left field, the decimal separator for the right one; a key that does not apply is disabled.
void ConverterPage::updateKeyStates() {
    for (int f = 0; f < 2; ++f) edits_[f]->setUnderline(f == active_);
    if (KeyButton* b = keys_.value(QStringLiteral(":"))) b->setEnabled(active_ == Left);
    if (KeyButton* b = keys_.value(QStringLiteral(","))) b->setEnabled(active_ == Right);
}

void ConverterPage::focusActive() {
    DisplayEdit* e = edits_[active_];
    if (e->hasFocus() && e->isActiveWindow()) return;
    e->setFocus(Qt::OtherFocusReason);
    e->deselect();
    e->setCursorPosition(e->text().size());
}

// ------------------------------------------------------------------ editing

// The single place where typed field text is sanitized and the other field recomputed. 'raw' is the text
// as it was typed, 'caret' the caret in it. 'widgetHoldsRaw': the field shows 'raw' already (an edit made in
// the field itself); the field is only touched when the filter changes the text. A change that came from the
// keypad is not in the field yet: the field is set to the filtered text.
void ConverterPage::applyEdit(int field, const QString& raw, int caret, bool widgetHoldsRaw) {
    DisplayEdit* e = edits_[field];
    const QString filtered = q(conv::FilterField(field, w(raw)));
    if (!widgetHoldsRaw || e->text() != filtered) {
        const int safe = qBound(0, caret, static_cast<int>(raw.size()));
        const int newCaret = static_cast<int>(q(conv::FilterField(field, w(raw.left(safe)))).size());
        e->setText(filtered);
        e->setCursorPosition(qMin(newCaret, static_cast<int>(filtered.size())));
    }
    recompute(field, filtered);
}

void ConverterPage::recompute(int field, const QString& text) {
    std::wstring normal, other, cap;
    convertSafe(field, text, places_, normal, other, cap);
    DisplayEdit* o = edits_[1 - field];
    const QString otherText = q(other);
    if (o->text() != otherText) o->setText(otherText);
    caption_->setText(q(cap));
    source_ = field;
    fitAll();
}

void ConverterPage::setDecimalPlaces(int places) {
    places_ = hc::timemath::ClampDecimals(places);
    std::wstring normal, other, cap;
    convertSafe(source_, edits_[source_]->text(), places_, normal, other, cap);
    DisplayEdit* o = edits_[1 - source_];
    const QString otherText = q(other);
    if (o->text() != otherText) o->setText(otherText);
    caption_->setText(q(cap));
    fitAll();
}

// Replaces the selection of a field (or inserts at the caret) and runs the usual edit.
void ConverterPage::typeInto(int field, const QString& text) {
    DisplayEdit* e = edits_[field];
    QString next = e->text();
    int start = static_cast<int>(next.size());
    int length = 0;
    if (e->hasSelectedText()) {
        start = e->selectionStart();
        length = static_cast<int>(e->selectedText().size());
    } else {
        start = e->cursorPosition();
    }
    start = qBound(0, start, static_cast<int>(next.size()));
    length = qMin(length, static_cast<int>(next.size()) - start);
    next.remove(start, length);
    next.insert(start, text);
    applyEdit(field, next, start + static_cast<int>(text.size()), false);
}

void ConverterPage::typeText(const QString& text) {
    focusActive();
    typeInto(active_, text);
}

void ConverterPage::setFieldText(int field, const QString& text) {
    activate(field);
    applyEdit(field, text, static_cast<int>(text.size()), false);
}

void ConverterPage::press(const QString& command) {
    if (command == QLatin1String("RESET")) {
        for (int f = 0; f < 2; ++f) edits_[f]->setText(QString());
        caption_->setText(QString());
        source_ = Left;
        activate(Left);
        focusActive();
        fitAll();
        return;
    }
    if (command == QLatin1String("CONVERT")) {
        // Tidies the active field's text, and recomputes the other field from what is in the field
        // (not from the tidied text: 8,058 reads 8,06 afterwards, but the other field keeps the 08:03 it had).
        const int f = active_;
        std::wstring normal, other, cap;
        convertSafe(f, edits_[f]->text(), places_, normal, other, cap);
        edits_[f]->setText(q(normal));
        edits_[f]->setCursorPosition(static_cast<int>(edits_[f]->text().size()));
        edits_[1 - f]->setText(q(other));
        caption_->setText(q(cap));
        source_ = f;
        fitAll();
        return;
    }
    if (command == QLatin1String("CE")) {
        focusActive();
        if (!edits_[active_]->text().isEmpty()) applyEdit(active_, QString(), 0, false);
        return;
    }
    if (command == QLatin1String("BACK")) {
        const int f = active_;
        focusActive();
        DisplayEdit* e = edits_[f];
        QString next = e->text();
        if (e->hasSelectedText()) {
            const int start = e->selectionStart();
            next.remove(start, static_cast<int>(e->selectedText().size()));
            applyEdit(f, next, start, false);
        } else if (e->cursorPosition() > 0) {
            const int i = e->cursorPosition();
            next.remove(i - 1, 1);
            applyEdit(f, next, i - 1, false);
        }
        return;
    }
    if (command.size() != 1) return;
    const QChar c = command.at(0);
    if (c.unicode() >= '0' && c.unicode() <= '9') {
        typeText(command);
    } else if (c == QLatin1Char(':') && active_ == Left) {
        typeText(QStringLiteral(":"));
    } else if ((c == QLatin1Char(',') || c == QLatin1Char('.')) && active_ == Right) {
        typeText(q(hc::timemath::DecimalSeparator()));
    }
}

// ------------------------------------------------------------------ sizes and fonts

// The value row keeps the height of the largest font, so a long value that shrinks its font never moves
// the keypad. The fonts follow the size of the window (1.0 is the default window).
void ConverterPage::refreshMetrics() {
    applyScale(scale_);
}

void ConverterPage::applyScale(double scale) {
    scale_ = scale;
    const QFont small = scaledFont(kLabelFactor);
    QFont value = scaledFont(kValueFactor * scale_, QFont::Light);
    const int valueHeight = static_cast<int>(std::ceil(QFontMetricsF(value).height())) + 2;
    for (int f = 0; f < 2; ++f) {
        labels_[f]->setFont(small);
        labels_[f]->setFixedHeight(static_cast<int>(std::ceil(QFontMetricsF(small).height())) + 2);
        edits_[f]->setFont(value);
        edits_[f]->setFixedHeight(valueHeight);
    }
    caption_->setFont(scaledFont(kCaptionFactor));
    caption_->setFixedHeight(static_cast<int>(std::ceil(QFontMetricsF(caption_->font()).height())) + 2);
    for (KeyButton* b : keyList_) b->setScale(scale_);
    fitAll();
}

// Both values use one font size: the smaller of the two that fit.
void ConverterPage::fitAll() {
    const double maxPt = basePoints() * kValueFactor * scale_;
    const double minPt = basePoints() * kValueMinFactor * scale_;
    double pt = maxPt;
    for (int f = 0; f < 2; ++f) pt = qMin(pt, fitPointSize(edits_[f]->font(), edits_[f]->text(), edits_[f]->textAreaWidth(), maxPt, minPt));
    valuePt_ = pt;
    for (int f = 0; f < 2; ++f) {
        QFont font = edits_[f]->font();
        if (!qFuzzyCompare(font.pointSizeF(), pt)) {
            font.setPointSizeF(pt);
            edits_[f]->setFont(font);
        }
    }
}

void ConverterPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const double s = windowScale(size(), unitPx(), kPageRefWidth, kPageRefHeight);
    // The size follows the window only when the scale changes by 0.004 or more: a fraction of a pixel must not
    // make the fonts shrink.
    if (std::fabs(s - scale_) >= 0.004) applyScale(s);
    else fitAll();
}

void ConverterPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::StyleChange)
        QMetaObject::invokeMethod(this, [this] { refreshMetrics(); }, Qt::QueuedConnection);
    QWidget::changeEvent(event);
}

bool ConverterPage::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize && (watched == edits_[0] || watched == edits_[1])) fitAll();
    return QWidget::eventFilter(watched, event);
}

}  // namespace hcqt
