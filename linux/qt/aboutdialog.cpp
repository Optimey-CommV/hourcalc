// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

#include "aboutdialog.h"

#include "appicon.h"
#include "hcqt_config.h"
#include "util.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

namespace hcqt {

static const char kLicensePath[] = "/usr/share/common-licenses/GPL-3";

QString licenseText() {
    QFile file(QString::fromLatin1(kLicensePath));
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = file.readAll();
        if (!bytes.isEmpty()) return QString::fromUtf8(bytes);
    }
    return QStringLiteral("The text of the license was not found on this system (%1).\n\n"
                          "The GNU General Public License, version 3, can be read at\n"
                          "https://www.gnu.org/licenses/gpl-3.0.html")
        .arg(QString::fromLatin1(kLicensePath));
}

// ------------------------------------------------------------------ LicenseDialog

LicenseDialog::LicenseDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("GNU General Public License"));
    setModal(true);
    auto* root = new QVBoxLayout(this);
    view_ = new QPlainTextEdit(this);
    view_->setReadOnly(true);
    view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view_->setPlainText(licenseText());
    view_->moveCursor(QTextCursor::Start);
    // Wide enough for the 80 column text of the license, tall enough to read a page of it.
    const QFontMetricsF fm(view_->font());
    const int frame = 2 * view_->frameWidth() + view_->verticalScrollBar()->sizeHint().width();
    view_->setMinimumSize(static_cast<int>(fm.horizontalAdvance(QLatin1Char('0')) * 82) + frame, static_cast<int>(unitPx() * 24));
    root->addWidget(view_, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    root->addWidget(buttons);
}

// ------------------------------------------------------------------ AboutDialog

QString AboutDialog::productName() { return QStringLiteral("HourCalc"); }
QString AboutDialog::version() { return QStringLiteral(HC_VERSION); }
QString AboutDialog::copyright() { return QStringLiteral("Copyright (c) 2026 Optimey CommV"); }
QString AboutDialog::sourceUrl() { return QStringLiteral(HC_SOURCE_URL); }
QString AboutDialog::notice() {
    return QStringLiteral(
        "This program is free software: you can redistribute it and/or modify it under the terms of "
        "the GNU General Public License as published by the Free Software Foundation, either version 3 "
        "of the License, or (at your option) any later version. This program comes with ABSOLUTELY NO WARRANTY.");
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("About HourCalc"));
    setModal(true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    const double u = unitPx();

    auto* root = new QVBoxLayout(this);
    root->setSizeConstraint(QLayout::SetFixedSize);

    auto* head = new QHBoxLayout();
    head->setSpacing(static_cast<int>(u));
    auto* icon = new QLabel(this);
    const int iconSide = static_cast<int>(u * 4.0);
    icon->setPixmap(appIcon().pixmap(QSize(iconSide, iconSide), devicePixelRatioF()));
    icon->setFixedSize(iconSide, iconSide);
    icon->setAccessibleName(QStringLiteral("HourCalc icon"));
    head->addWidget(icon, 0, Qt::AlignTop);

    auto* titles = new QVBoxLayout();
    titles->setSpacing(0);
    auto* name = new QLabel(productName(), this);
    QFont big = scaledFont(1.7, QFont::DemiBold);
    name->setFont(big);
    auto* ver = new QLabel(QStringLiteral("Version %1").arg(version()), this);
    auto* copy = new QLabel(copyright(), this);
    titles->addWidget(name);
    titles->addSpacing(static_cast<int>(u * 0.3));
    titles->addWidget(ver);
    titles->addWidget(copy);
    titles->addStretch(1);
    head->addLayout(titles, 1);
    root->addLayout(head);

    notice_ = new QLabel(notice(), this);
    notice_->setWordWrap(true);
    notice_->setMinimumWidth(static_cast<int>(u * 25));
    root->addWidget(notice_);

    url_ = new QLabel(this);
    url_->setTextFormat(Qt::RichText);
    refreshLink();
    url_->setOpenExternalLinks(true);
    url_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    root->addWidget(url_);

    buttons_ = new QDialogButtonBox(this);
    licenseButton_ = buttons_->addButton(QStringLiteral("View license"), QDialogButtonBox::ActionRole);
    buttons_->addButton(QDialogButtonBox::Close);
    connect(licenseButton_, &QPushButton::clicked, this, [this] {
        LicenseDialog* d = createLicenseDialog();
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->exec();
    });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::close);
    root->addSpacing(static_cast<int>(u * 0.4));
    root->addWidget(buttons_);
}

// The link is written with an explicit colour: the palette of this dialog (the Link role, which is the desktop's under Auto and
// the Breeze one of the scheme under a forced theme), moved to at least 4.5 : 1 against the background of the dialog when it is
// too weak there (Breeze's own blues are 3.8 : 1 on its light window and 4.5 : 1 on its dark one, a desktop's may be worse).
void AboutDialog::refreshLink() {
    if (!url_) return;
    linkColor_ = readableLink(palette().color(QPalette::Link), palette().color(QPalette::Window));
    url_->setText(QStringLiteral("<a href=\"%1\" style=\"color:%2\">%1</a>").arg(sourceUrl().toHtmlEscaped(), linkColor_.name(QColor::HexRgb)));
}

void AboutDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) refreshLink();
}

LicenseDialog* AboutDialog::createLicenseDialog() {
    auto* d = new LicenseDialog(this);
    d->setPalette(palette());
    return d;
}

QStringList AboutDialog::buttonTexts() const {
    QStringList out;
    for (QAbstractButton* b : buttons_->buttons()) {
        QString t = b->text();
        t.remove(QLatin1Char('&'));
        out << t;
    }
    return out;
}

QString AboutDialog::noticeText() const { return notice_->text(); }
QString AboutDialog::urlText() const { return sourceUrl(); }

}  // namespace hcqt
