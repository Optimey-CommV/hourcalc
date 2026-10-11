// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// About HourCalc (SPEC 8, docs/LINUX.md section 3): name, version, copyright, the GPL notice, the address
// of the source code, and the full license text (the copy the distribution keeps under
// /usr/share/common-licenses/GPL-3) in a window of its own.

#pragma once

#include <QColor>
#include <QDialog>
#include <QStringList>

class QDialogButtonBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace hcqt {

// The text of the license: the file of the distribution, or a note where to read it when there is none.
QString licenseText();

class LicenseDialog : public QDialog {
    Q_OBJECT
public:
    explicit LicenseDialog(QWidget* parent = nullptr);
    QPlainTextEdit* view() const { return view_; }

private:
    QPlainTextEdit* view_ = nullptr;
};

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);

    static QString productName();
    static QString version();
    static QString copyright();
    static QString notice();
    static QString sourceUrl();

    // What the dialog shows, for the tests.
    QStringList buttonTexts() const;
    QString noticeText() const;
    QString urlText() const;
    QPushButton* licenseButton() const { return licenseButton_; }
    LicenseDialog* createLicenseDialog();
    // The colour the address of the source is shown in, and the label that shows it (for the tests).
    QColor linkColor() const { return linkColor_; }
    QLabel* urlLabel() const { return url_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    // Sets the colour of the link from the palette of this dialog: Qt takes the link colour of rich text from the palette of the
    // application, and under a forced Light or Dark theme that is the palette of the desktop, the wrong scheme.
    void refreshLink();

    QColor linkColor_;
    QPushButton* licenseButton_ = nullptr;
    QLabel* notice_ = nullptr;
    QLabel* url_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

}  // namespace hcqt
