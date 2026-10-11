// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

#include "calculatorpage.h"

#include "displaywidgets.h"
#include "keybutton.h"
#include "layoutconst.h"
#include "util.h"

#include <QClipboard>
#include <QEvent>
#include <QGridLayout>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <cmath>

namespace hcqt {

namespace {

// The operators of the expression line are drawn as x, the division sign and the minus sign.
QString expressionSymbols(QString e) {
    e.replace(QStringLiteral(" * "), QStringLiteral(" ") + QChar(0x00D7) + QStringLiteral(" "));
    e.replace(QStringLiteral(" / "), QStringLiteral(" ") + QChar(0x00F7) + QStringLiteral(" "));
    e.replace(QStringLiteral(" - "), QStringLiteral(" ") + QChar(0x2212) + QStringLiteral(" "));
    return e;
}

// The expression line shows the end of a long chain, with a leading ellipsis.
QString elideExpression(const QString& full, const QFont& font, double avail) {
    const QFontMetricsF fm(font);
    if (full.isEmpty() || avail <= 8.0 || fm.horizontalAdvance(full) <= avail) return full;
    const QStringList tokens = full.split(QLatin1Char(' '));
    const int n = static_cast<int>(tokens.size());
    for (int i = 1; i < n; ++i) {
        const QString cand = QString(QChar(0x2026)) + QLatin1Char(' ') + QStringList(tokens.mid(i)).join(QLatin1Char(' '));
        if (fm.horizontalAdvance(cand) <= avail || i == n - 1) return cand;
    }
    return full;
}

// The key a typed character stands for (layout independent: the character, not the key code).
QString keyForChar(QChar c) {
    const ushort u = c.unicode();
    if (u >= '0' && u <= '9') return QString(c);
    switch (u) {
    case ':': case 'u': case 'U': case 'h': case 'H':
        return QStringLiteral(":");
    case ',': case '.':
        return QStringLiteral(",");
    case '+': case '-': case '*': case '/':
        return QString(c);
    case '=':
        return QStringLiteral("=");
    default:
        return QString();
    }
}

}  // namespace

CalculatorPage::CalculatorPage(QWidget* parent) : QWidget(parent), engine_(hc::timemath::DefaultDecimals) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    display_ = new QGridLayout();
    display_->setVerticalSpacing(0);
    display_->setColumnStretch(0, 1);
    display_->setColumnStretch(1, 1);
    display_->setColumnMinimumWidth(0, 1);
    display_->setColumnMinimumWidth(1, 1);

    expression_ = new DisplayLabel(QString(), this);
    expression_->setSecondary(true);
    expression_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    expression_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    expression_->setAccessibleDescription(QStringLiteral("Expression"));
    display_->addWidget(expression_, 0, 0, 1, 2);

    const QString names[2] = {QStringLiteral("Hours : minutes"), QStringLiteral("Decimal hours")};
    for (int i = 0; i < 2; ++i) {
        labels_[i] = new DisplayLabel(names[i], this);
        labels_[i]->setSecondary(true);
        labels_[i]->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        labels_[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        display_->addWidget(labels_[i], 1, i);

        value_[i] = new DisplayLabel(QString(), this);
        value_[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value_[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        value_[i]->setCopyable(true);
        value_[i]->setAccessibleDescription(names[i]);
        value_[i]->installEventFilter(this);
        display_->addWidget(value_[i], 2, i);
    }
    connect(value_[0], &DisplayLabel::copyRequested, this, &CalculatorPage::copyMain);
    connect(value_[1], &DisplayLabel::copyRequested, this, &CalculatorPage::copyDecimal);

    captionLabel_ = new DisplayLabel(QString(), this);
    captionLabel_->setSecondary(true);
    captionLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    captionLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    captionLabel_->setAccessibleDescription(QStringLiteral("Value in words"));
    display_->addWidget(captionLabel_, 3, 0);
    root->addLayout(display_);

    keypad_ = new QGridLayout();
    root->addLayout(keypad_, 1);
    buildKeypad();

    refresh();
    refreshMetrics();
}

// ------------------------------------------------------------------ keypad

void CalculatorPage::buildKeypad() {
    const QString sep = q(hc::timemath::DecimalSeparator());
    auto add = [&](const QString& command, const QString& label, KeyButton::Kind kind, KeyButton::Glyph glyph, int row, int col,
                   const QString& name) {
        auto* b = new KeyButton(command, label, kind, glyph, this);
        if (!name.isEmpty()) b->setAccessibleName(name);
        keypad_->addWidget(b, row, col);
        keys_.insert(command, b);
        keyList_.append(b);
        connect(b, &QPushButton::clicked, this, [this, command] { press(command); });
        return b;
    };
    add(QStringLiteral("CE"), QStringLiteral("CE"), KeyButton::Function, KeyButton::NoGlyph, 0, 0, QStringLiteral("Clear entry"));
    add(QStringLiteral("C"), QStringLiteral("C"), KeyButton::Function, KeyButton::NoGlyph, 0, 1, QStringLiteral("Clear"));
    add(QStringLiteral("BACK"), QString(), KeyButton::Function, KeyButton::Backspace, 0, 2, QStringLiteral("Backspace"));
    add(QStringLiteral("/"), QString(), KeyButton::Function, KeyButton::Divide, 0, 3, QStringLiteral("Divide"));
    const char* digits[9] = {"7", "8", "9", "4", "5", "6", "1", "2", "3"};
    for (int i = 0; i < 9; ++i)
        add(QLatin1String(digits[i]), QLatin1String(digits[i]), KeyButton::Digit, KeyButton::NoGlyph, 1 + i / 3, i % 3, QString());
    add(QStringLiteral("*"), QString(), KeyButton::Function, KeyButton::Times, 1, 3, QStringLiteral("Multiply"));
    add(QStringLiteral("-"), QString(), KeyButton::Function, KeyButton::Minus, 2, 3, QStringLiteral("Subtract"));
    add(QStringLiteral("+"), QString(), KeyButton::Function, KeyButton::Plus, 3, 3, QStringLiteral("Add"));
    add(QStringLiteral(":"), QStringLiteral(":"), KeyButton::Digit, KeyButton::NoGlyph, 4, 0, QStringLiteral("Time separator"));
    add(QStringLiteral("0"), QStringLiteral("0"), KeyButton::Digit, KeyButton::NoGlyph, 4, 1, QString());
    add(QStringLiteral(","), sep, KeyButton::Digit, KeyButton::NoGlyph, 4, 2, QStringLiteral("Decimal separator"));
    add(QStringLiteral("="), QString(), KeyButton::Accent, KeyButton::Equals, 4, 3, QStringLiteral("Equals"));
    for (int r = 0; r < 5; ++r) keypad_->setRowStretch(r, 1);
    for (int c = 0; c < 4; ++c) keypad_->setColumnStretch(c, 1);
}

bool CalculatorPage::keyEnabled(const QString& command) const {
    KeyButton* b = keys_.value(command);
    return b ? b->isEnabled() : false;
}

// ------------------------------------------------------------------ input

void CalculatorPage::press(const QString& command) {
    guarded([&] { engine_.Press(w(command)); });
    refresh();
}

void CalculatorPage::typeText(const QString& text) {
    for (QChar c : text) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7F) continue;
        const QString key = keyForChar(c);
        if (!key.isEmpty()) press(key);
    }
}

void CalculatorPage::copyMain() { QGuiApplication::clipboard()->setText(q(engine_.CopyMainText())); }
void CalculatorPage::copyDecimal() { QGuiApplication::clipboard()->setText(q(engine_.CopyDecimalText())); }

void CalculatorPage::pasteFromClipboard() {
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) return;
    bool pasted = false;
    guarded([&] { pasted = engine_.Paste(w(text)); });
    if (pasted) refresh();
}

void CalculatorPage::setDecimalPlaces(int places) {
    engine_.SetDecimalPlaces(places);
    refresh();
}

// ------------------------------------------------------------------ display

void CalculatorPage::refresh() {
    // (the core does not throw here for any state it can reach; the guard keeps an exception out of Qt all the same)
    guarded([&] {
        error_ = engine_.IsError();
        expressionFull_ = expressionSymbols(q(engine_.Expression()));
        main_ = q(engine_.MainText());
        decimal_ = q(engine_.DecimalText());
        caption_ = q(engine_.Caption());
        for (KeyButton* b : keyList_) b->setEnabled(engine_.IsEnabled(w(b->command())));
    });
    value_[0]->setText(main_);
    value_[1]->setText(decimal_);
    captionLabel_->setText(caption_);
    arrangeValues();
    fitAll();
}

// The error text may use the width of both columns: the left value spans them and the right one is hidden.
void CalculatorPage::arrangeValues() {
    if (spanned_ == error_) return;
    spanned_ = error_;
    display_->removeWidget(value_[0]);
    display_->addWidget(value_[0], 2, 0, 1, error_ ? 2 : 1);
    value_[1]->setVisible(!error_);
    labels_[1]->setVisible(!error_);
}

void CalculatorPage::refreshMetrics() { applyScale(scale_); }

void CalculatorPage::applyScale(double scale) {
    scale_ = scale;
    const QFont small = scaledFont(kLabelFactor);
    const QFont caption = scaledFont(kCaptionFactor);
    const QFont value = scaledFont(kValueFactor * scale_, QFont::Light);
    const int valueHeight = static_cast<int>(std::ceil(QFontMetricsF(value).height())) + 2;
    for (int i = 0; i < 2; ++i) {
        labels_[i]->setFont(small);
        labels_[i]->setFixedHeight(static_cast<int>(std::ceil(QFontMetricsF(small).height())) + 2);
        value_[i]->setFont(value);
        value_[i]->setFixedHeight(valueHeight);
    }
    const int captionHeight = static_cast<int>(std::ceil(QFontMetricsF(caption).height())) + 2;
    expression_->setFont(caption);
    expression_->setFixedHeight(captionHeight);
    captionLabel_->setFont(caption);
    captionLabel_->setFixedHeight(captionHeight);
    for (KeyButton* b : keyList_) b->setScale(scale_);
    fitAll();
}

// Both values use one font size: the smaller of the two that fit. The left value may use the width of both
// columns and a smaller font for an error text.
void CalculatorPage::fitAll() {
    const double leftMax = basePoints() * kValueFactor * (error_ ? kErrorFactor : 1.0) * scale_;
    const double leftMin = basePoints() * kCalcLeftMinFactor * scale_;
    const double rightMax = basePoints() * kValueFactor * scale_;
    const double rightMin = basePoints() * kValueMinFactor * scale_;
    const double pad = 2.0;
    double a = fitPointSize(value_[0]->font(), main_, value_[0]->width() - pad, leftMax, leftMin);
    double b = fitPointSize(value_[1]->font(), decimal_, value_[1]->width() - pad, rightMax, rightMin);
    valuePt_ = qMin(a, b);
    for (int i = 0; i < 2; ++i) {
        QFont f = value_[i]->font();
        if (!qFuzzyCompare(f.pointSizeF(), valuePt_)) {
            f.setPointSizeF(valuePt_);
            value_[i]->setFont(f);
        }
    }
    expressionShown_ = elideExpression(expressionFull_, expression_->font(), expression_->width() - pad);
    expression_->setText(expressionShown_);
}

void CalculatorPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const double s = windowScale(size(), unitPx(), kPageRefWidth, kPageRefHeight);
    if (std::fabs(s - scale_) >= 0.004) applyScale(s);
    else fitAll();
}

void CalculatorPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::StyleChange)
        QMetaObject::invokeMethod(this, [this] { refreshMetrics(); }, Qt::QueuedConnection);
    QWidget::changeEvent(event);
}

bool CalculatorPage::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize && (watched == value_[0] || watched == value_[1])) fitAll();
    return QWidget::eventFilter(watched, event);
}

}  // namespace hcqt
