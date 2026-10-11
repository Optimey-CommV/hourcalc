// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// Tab 1: the converter (SPEC 4). Two editable fields side by side (hours : minutes and decimal hours),
// the caption under the left one and the keypad. Every edit of a field recomputes the other one through
// the core; the keypad types into the active field, which is the last one that had the keyboard focus.

#pragma once

#include <QHash>
#include <QList>
#include <QWidget>

class QGridLayout;

namespace hcqt {

class DisplayEdit;
class DisplayLabel;
class KeyButton;

class ConverterPage : public QWidget {
    Q_OBJECT
public:
    static constexpr int Left = 0;
    static constexpr int Right = 1;

    explicit ConverterPage(QWidget* parent = nullptr);

    int decimalPlaces() const { return places_; }
    // Re-formats the computed field and the caption at once, from the field that was edited last.
    void setDecimalPlaces(int places);

    // ---- state
    QString fieldText(int field) const;
    QString caption() const;
    int activeField() const { return active_; }     // the field the keypad types into
    int focusField() const;                         // the field that has the keyboard focus (-1: none)
    bool keyEnabled(const QString& command) const;
    DisplayEdit* edit(int field) const { return edits_[field]; }
    DisplayLabel* fieldLabel(int field) const { return labels_[field]; }
    DisplayLabel* captionLabel() const { return caption_; }
    KeyButton* key(const QString& command) const { return keys_.value(command); }
    QList<KeyButton*> keys() const { return keyList_; }
    double valuePointSize() const { return valuePt_; }
    double scale() const { return scale_; }

    // ---- input
    // A key of the keypad: "0".."9", ":", ",", "CE", "BACK", "RESET", "CONVERT".
    void press(const QString& command);
    // Text from the keyboard while no field has the focus: typed into the active field.
    void typeText(const QString& text);
    // Gives the active field the keyboard focus without selecting its text (the caret goes to the end),
    // unless it has the focus already.
    void focusActive();
    // Test and render aid: the text of a field as if it had been typed (filtered, the other field computed).
    void setFieldText(int field, const QString& text);
    void activate(int field);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildKeypad();
    void refreshMetrics();
    void applyScale(double scale);
    void fitAll();
    void applyEdit(int field, const QString& raw, int caret, bool widgetHoldsRaw);
    void recompute(int field, const QString& text);
    void typeInto(int field, const QString& text);
    void updateKeyStates();

    DisplayLabel* labels_[2] = {nullptr, nullptr};
    DisplayEdit* edits_[2] = {nullptr, nullptr};
    DisplayLabel* caption_ = nullptr;
    QHash<QString, KeyButton*> keys_;
    QList<KeyButton*> keyList_;
    QGridLayout* keypad_ = nullptr;
    int places_ = 2;
    int active_ = Left;       // the field the keypad types into
    int source_ = Left;       // the field that was edited last: the other one is computed from it
    double scale_ = 1.0;
    double valuePt_ = 0.0;
};

}  // namespace hcqt
