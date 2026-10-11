// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// Tab 2: the calculator (SPEC 5). The display shows what the core's state machine reports (expression
// line, the h:mm value, the decimal value, the value in words); the keypad and the keyboard feed keys to it.

#pragma once

#include "hc_core.h"

#include <QHash>
#include <QList>
#include <QWidget>

class QGridLayout;

namespace hcqt {

class DisplayLabel;
class KeyButton;

class CalculatorPage : public QWidget {
    Q_OBJECT
public:
    explicit CalculatorPage(QWidget* parent = nullptr);

    hc::CalcEngine& engine() { return engine_; }
    int decimalPlaces() const { return engine_.DecimalPlaces(); }
    void setDecimalPlaces(int places);

    // ---- state mirrored from the engine
    QString mainText() const { return main_; }
    QString decimalText() const { return decimal_; }
    QString captionText() const { return caption_; }
    QString expressionShown() const { return expressionShown_; }
    bool isError() const { return error_; }
    bool rightVisible() const { return !error_; }
    bool keyEnabled(const QString& command) const;
    DisplayLabel* mainLabel() const { return value_[0]; }
    DisplayLabel* decimalLabel() const { return value_[1]; }
    DisplayLabel* captionLabel() const { return captionLabel_; }
    DisplayLabel* expressionLabel() const { return expression_; }
    DisplayLabel* fieldLabel(int i) const { return labels_[i]; }
    KeyButton* key(const QString& command) const { return keys_.value(command); }
    QList<KeyButton*> keys() const { return keyList_; }
    double valuePointSize() const { return valuePt_; }
    double scale() const { return scale_; }

    // ---- input
    // A key of the keypad or of the engine: "0".."9", ":", ",", "+", "-", "*", "/", "=", "CE", "C", "BACK".
    void press(const QString& command);
    // Text from the keyboard, one character at a time (layout independent): digits, ":" "u" "h", "," ".",
    // "+ - * /", "=".
    void typeText(const QString& text);
    void copyMain();
    void copyDecimal();
    void pasteFromClipboard();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildKeypad();
    void refresh();
    void arrangeValues();
    void refreshMetrics();
    void applyScale(double scale);
    void fitAll();

    hc::CalcEngine engine_;
    DisplayLabel* expression_ = nullptr;
    DisplayLabel* labels_[2] = {nullptr, nullptr};
    DisplayLabel* value_[2] = {nullptr, nullptr};
    DisplayLabel* captionLabel_ = nullptr;
    QGridLayout* display_ = nullptr;
    QGridLayout* keypad_ = nullptr;
    QHash<QString, KeyButton*> keys_;
    QList<KeyButton*> keyList_;

    QString main_, decimal_, caption_, expressionFull_, expressionShown_;
    bool error_ = false;
    bool spanned_ = false;       // the left value spans both columns (the error text)
    double scale_ = 1.0;
    double valuePt_ = 0.0;
};

}  // namespace hcqt
