// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"

#include "aboutdialog.h"
#include "appicon.h"
#include "calculatorpage.h"
#include "converterpage.h"
#include "layoutconst.h"
#include "popupstyle.h"
#include "scheme.h"
#include "util.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QPaintEvent>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyleHints>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QTabBar>
#include <QToolTip>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

namespace hcqt {

namespace {

std::function<void()>& firstPaintHook() {
    static std::function<void()> hook;
    return hook;
}

// The menu button: three bars painted in the button text colour of the window's palette. Never an icon of the icon
// theme: an icon engine colours a symbolic icon from the palette of the application, not from the palette of this
// window, and the window palette is what a forced Light or Dark theme changes - the glyph could end up dark on dark.
// No arrow: the menu is opened by hand, below the button.
class HamburgerButton : public QToolButton {
public:
    explicit HamburgerButton(QWidget* parent) : QToolButton(parent) {
        setFocusPolicy(Qt::NoFocus);
        setAutoRaise(true);
        setToolTip(QStringLiteral("Menu"));
        setAccessibleName(QStringLiteral("Main menu"));
    }
    QSize sizeHint() const override {
        const int s = qRound(unitPx() * 1.9);
        return QSize(s, s);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QStylePainter painter(this);
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.features &= ~QStyleOptionToolButton::HasMenu;
        option.icon = QIcon();
        option.text.clear();
        painter.drawComplexControl(QStyle::CC_ToolButton, option);
        const QRectF r = QRectF(rect());
        const double side = unitPx() * 0.95;
        const double bar = qMax(1.5, side * 0.11);
        const double gap = side * 0.30;
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(option.palette.color(QPalette::ButtonText), bar, Qt::SolidLine, Qt::RoundCap));
        for (int i = -1; i <= 1; ++i) {
            const double y = r.center().y() + i * gap;
            painter.drawLine(QPointF(r.center().x() - side / 2, y), QPointF(r.center().x() + side / 2, y));
        }
    }
};

// A window that is never shown and has no palette of its own: it gets every change of the desktop palette. A
// window with an explicit palette (a forced Light or Dark) does not hear of it, and the accent of a forced theme
// has to follow the desktop.
class PaletteWatcher : public QWidget {
public:
    explicit PaletteWatcher(std::function<void()> changed) : changed_(std::move(changed)) {}

protected:
    void changeEvent(QEvent* event) override {
        if (event->type() == QEvent::PaletteChange && changed_) changed_();
        QWidget::changeEvent(event);
    }

private:
    std::function<void()> changed_;
};

}  // namespace

void MainWindow::setFirstPaintHook(std::function<void()> hook) { firstPaintHook() = std::move(hook); }

// ------------------------------------------------------------------ construction

MainWindow::MainWindow(const QString& settingsPath, int keepOnTop, QWidget* parent) : QWidget(parent), settingsPath_(settingsPath) {
    setWindowTitle(QStringLiteral("HourCalc"));
    setWindowIcon(appIcon());
    if (!settingsPath_.isEmpty()) {
        const hc::settings::Values v = hc::settings::Load(w(settingsPath_));
        places_ = v.decimalPlaces;
        theme_ = v.theme;
    }
    // The scheme first: the widgets that are made next start with the right palette.
    applyTheme();
    paletteWatcher_ = new PaletteWatcher([this] {
        // The desktop palette changed: a forced theme keeps its scheme and takes the new accent of the desktop.
        if (theme_ != ThemeMode::Auto && !applyingTheme_) applyTheme();
    });

    auto* root = new QVBoxLayout(this);
    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);

    tabs_ = new QTabBar(this);
    tabs_->addTab(QStringLiteral("Converter"));
    tabs_->addTab(QStringLiteral("Calculator"));
    tabs_->setFocusPolicy(Qt::NoFocus);
    tabs_->setDocumentMode(true);
    tabs_->setExpanding(false);
    tabs_->setDrawBase(false);
    tabs_->setUsesScrollButtons(false);
    tabs_->setAccessibleName(QStringLiteral("Tabs"));
    header->addWidget(tabs_, 0, Qt::AlignVCenter);
    header->addStretch(1);

    menuButton_ = new HamburgerButton(this);
    header->addWidget(menuButton_, 0, Qt::AlignVCenter);
    root->addLayout(header);

    stack_ = new QStackedWidget(this);
    converter_ = new ConverterPage(stack_);
    calculator_ = new CalculatorPage(stack_);
    stack_->addWidget(converter_);
    stack_->addWidget(calculator_);
    root->addWidget(stack_, 1);

    converter_->setDecimalPlaces(places_);
    calculator_->setDecimalPlaces(places_);

    const bool offerKeepOnTop = keepOnTop < 0 ? QGuiApplication::platformName() == QLatin1String("xcb") : keepOnTop != 0;
    buildMenu();
    if (offerKeepOnTop) {
        keepOnTopAction_ = new QAction(QStringLiteral("Keep on top"), this);
        keepOnTopAction_->setCheckable(true);
        menu_->insertAction(aboutAction_, keepOnTopAction_);
        connect(keepOnTopAction_, &QAction::triggered, this, [this](bool on) { setKeepOnTop(on); });
    }
    checkMenuItems();
    syncPopups();

    connect(tabs_, &QTabBar::currentChanged, this, &MainWindow::selectTab);
    connect(menuButton_, &QToolButton::clicked, this, &MainWindow::openMenu);

    // Ctrl+1 / Ctrl+2 / Ctrl+Tab switch tabs.
    auto shortcut = [this](const QKeySequence& keys, std::function<void()> fn) {
        auto* s = new QShortcut(keys, this);
        s->setContext(Qt::WindowShortcut);
        connect(s, &QShortcut::activated, this, [fn] { fn(); });
    };
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_1), [this] { selectTab(0); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_2), [this] { selectTab(1); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), [this] { selectTab(1 - currentTab()); });
    // The menu button never takes the focus: F10 opens the menu from the keyboard.
    shortcut(QKeySequence(Qt::Key_F10), [this] { openMenu(); });
    // Quit (Ctrl+Q) and Close (Ctrl+W, Ctrl+F4): the keys of a KDE application. One window: both end the program. The
    // keys of the platform's standard bindings (they are empty where the platform names no desktop) and the usual ones.
    auto closeKeys = [this](QKeySequence::StandardKey standard, const QKeySequence& usual) {
        QList<QKeySequence> keys = QKeySequence::keyBindings(standard);
        if (!keys.contains(usual)) keys << usual;
        auto* s = new QShortcut(this);
        s->setKeys(keys);
        s->setContext(Qt::WindowShortcut);
        connect(s, &QShortcut::activated, this, [this] { close(); });
    };
    closeKeys(QKeySequence::Quit, QKeySequence(Qt::CTRL | Qt::Key_Q));
    closeKeys(QKeySequence::Close, QKeySequence(Qt::CTRL | Qt::Key_W));

    applyMetrics();
    resize(defaultSize());
}

MainWindow::~MainWindow() {
    if (tooltipPinned_) QToolTip::setPalette(tooltipOriginal());
    delete paletteWatcher_;
}

QSize MainWindow::defaultSize() const {
    const double u = unitPx();
    return QSize(qRound(kDefaultWidth * u), qRound(kDefaultHeight * u));
}

QSize MainWindow::minimumWindowSize() const {
    const double u = unitPx();
    return QSize(qRound(kMinimumWidth * u), qRound(kMinimumHeight * u));
}

void MainWindow::applyMetrics() { setMinimumSize(minimumWindowSize()); }

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::StyleChange)
        applyMetrics();
    if (event->type() == QEvent::StyleChange && menu_) PopupStyle::sync(menu_);      // the application style was replaced
    if (event->type() == QEvent::PaletteChange && !applyingTheme_) syncPopups();
    QWidget::changeEvent(event);
}

// ------------------------------------------------------------------ menu

void MainWindow::buildMenu() {
    menu_ = new QMenu(this);
    menu_->setAttribute(Qt::WA_NoMouseReplay);
    PopupStyle::sync(menu_);               // the check box of Keep on top, under Fusion
    connect(menu_, &QMenu::aboutToHide, this, [this] { menuClosed_.start(); });

    menu_->addSection(QStringLiteral("Theme"));
    themeGroup_ = new QActionGroup(this);
    themeGroup_->setExclusive(true);
    struct ThemeItem {
        const char* text;
        ThemeMode mode;
    };
    for (const ThemeItem& item : {ThemeItem{"Auto (follow system)", ThemeMode::Auto}, ThemeItem{"Light", ThemeMode::Light},
                                  ThemeItem{"Dark", ThemeMode::Dark}}) {
        QAction* a = menu_->addAction(QString::fromLatin1(item.text));
        a->setCheckable(true);
        a->setData(static_cast<int>(item.mode));
        themeGroup_->addAction(a);
        const ThemeMode mode = item.mode;
        connect(a, &QAction::triggered, this, [this, mode] { setThemeMode(mode); });
    }

    menu_->addSection(QStringLiteral("Decimal places"));
    placesGroup_ = new QActionGroup(this);
    placesGroup_->setExclusive(true);
    for (int n = hc::timemath::MinDisplayDecimals; n <= hc::timemath::MaxDisplayDecimals; ++n) {
        QAction* a = menu_->addAction(n == hc::timemath::DefaultDecimals ? QStringLiteral("2 (default)") : QString::number(n));
        a->setCheckable(true);
        a->setData(n);
        placesGroup_->addAction(a);
        connect(a, &QAction::triggered, this, [this, n] { setDecimalPlaces(n); });
    }

    menu_->addSeparator();
    aboutAction_ = menu_->addAction(QStringLiteral("About HourCalc"));      // its icon: see syncPopups()
    connect(aboutAction_, &QAction::triggered, this, [this] { showAbout(); });
}

void MainWindow::checkMenuItems() {
    for (QAction* a : themeGroup_->actions()) a->setChecked(a->data().toInt() == static_cast<int>(theme_));
    for (QAction* a : placesGroup_->actions()) a->setChecked(a->data().toInt() == places_);
    if (keepOnTopAction_) keepOnTopAction_->setChecked(keepOnTop_);
}

void MainWindow::openMenu() {
    if (menu_->isVisible()) {
        menu_->hide();
        return;
    }
    // The click that closed the menu (it closes on any click outside it) also reaches the button: that must
    // not open it again.
    if (menuReopenBlocked()) return;
    checkMenuItems();
    syncPopups();
    const QSize hint = menu_->sizeHint();
    // Below the button, its right edge on the button's right edge (the button sits at the end of the header).
    QPoint pos = menuButton_->mapToGlobal(QPoint(menuButton_->width() - hint.width(), menuButton_->height()));
    menu_->popup(pos);
}

bool MainWindow::menuReopenBlocked() const { return menuClosed_.isValid() && menuClosed_.elapsed() < 250; }

AboutDialog* MainWindow::createAbout() {
    auto* dialog = new AboutDialog(this);
    if (testAttribute(Qt::WA_SetPalette)) dialog->setPalette(palette());
    return dialog;
}

void MainWindow::showAbout() {
    AboutDialog* dialog = createAbout();
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->exec();
}

// ------------------------------------------------------------------ tabs

int MainWindow::currentTab() const { return stack_->currentIndex(); }

void MainWindow::selectTab(int index) {
    index = index == 1 ? 1 : 0;
    if (tabs_->currentIndex() != index) tabs_->setCurrentIndex(index);   // the signal comes back here with the same index
    stack_->setCurrentIndex(index);
    if (index == 0) converter_->focusActive();
}

void MainWindow::press(const QString& command) {
    if (currentTab() == 0) converter_->press(command);
    else calculator_->press(command);
}

// ------------------------------------------------------------------ settings

void MainWindow::savePlaces() {
    if (!settingsPath_.isEmpty()) hc::settings::SavePlaces(w(settingsPath_), places_, theme_);
}

void MainWindow::saveTheme() {
    if (!settingsPath_.isEmpty()) hc::settings::SaveTheme(w(settingsPath_), theme_, places_);
}

void MainWindow::setDecimalPlaces(int places) {
    const int p = hc::timemath::ClampDecimals(places);
    if (placesGroup_) {
        for (QAction* a : placesGroup_->actions()) a->setChecked(a->data().toInt() == p);
    }
    if (p == places_) return;
    places_ = p;
    converter_->setDecimalPlaces(p);
    calculator_->setDecimalPlaces(p);
    repaint();             // on the screen before the file write, which can take a while
    savePlaces();
}

void MainWindow::setThemeMode(ThemeMode mode) {
    if (themeGroup_) {
        for (QAction* a : themeGroup_->actions()) a->setChecked(a->data().toInt() == static_cast<int>(mode));
    }
    if (mode == theme_) return;
    theme_ = mode;
    applyTheme();
    repaint();
    saveTheme();
}

bool MainWindow::isDark() const { return isDarkPalette(palette()); }

// Auto follows the desktop (live: Qt updates the palette when the system theme changes); Light and Dark force
// a scheme. The scheme is requested from the platform (QStyleHints::setColorScheme, Qt 6.8); a platform that
// does not carry it out (it leaves colorScheme() as it was) gets a palette of its own for this window. That
// palette is explicit in every role, also when the desktop is in the chosen scheme already, so a later change
// of the desktop theme does not touch a forced choice; the accent still follows the desktop (changeEvent).
void MainWindow::applyTheme() {
    if (applyingTheme_) return;
    applyingTheme_ = true;
    Qt::ColorScheme want = Qt::ColorScheme::Unknown;
    if (theme_ == ThemeMode::Light) want = Qt::ColorScheme::Light;
    else if (theme_ == ThemeMode::Dark) want = Qt::ColorScheme::Dark;
    const bool wantDark = want == Qt::ColorScheme::Dark;
    QGuiApplication::styleHints()->setColorScheme(want);
    // Carried out when the platform reports the scheme and the palette of the desktop is of that lightness. (A
    // platform may need a moment to change its palette: the watcher then calls this again.)
    const bool honoured = want == Qt::ColorScheme::Unknown ||
                          (QGuiApplication::styleHints()->colorScheme() == want && isDarkPalette(QApplication::palette()) == wantDark);
    if (honoured) {
        setPalette(QPalette());
    } else {
        const QPalette system = QApplication::palette();
        setPalette(isDarkPalette(system) == wantDark ? pinnedPalette(system) : schemePalette(wantDark, system));
    }
    syncPopups();
    update();
    applyingTheme_ = false;
}

// The tool tip palette as it was before this window ever changed it (taken once, at the first window).
QPalette MainWindow::tooltipOriginal() {
    static const QPalette original = QToolTip::palette();
    return original;
}

// Everything that is a window of its own and so does not inherit the palette of this window: the menu and the tool tips
// (the About dialog and the context menus get the palette when they are made). With a forced Light or Dark theme the window
// has a palette of its own (WA_SetPalette) and they get it; with Auto they follow the desktop. The icons of the menu come
// from the icon theme, whose engine colours them for the palette of the application: they are only shown while the window
// follows it (a forced theme can differ from the desktop, and a dark glyph on a dark menu is invisible).
// Safe to call at any time and as often as needed; the menu may not exist yet.
void MainWindow::syncPopups() {
    const bool forced = testAttribute(Qt::WA_SetPalette);
    if (menu_) menu_->setPalette(forced ? palette() : QPalette());
    if (aboutAction_) aboutAction_->setIcon(forced ? QIcon() : QIcon::fromTheme(QStringLiteral("help-about")));
    // The tool tip palette belongs to the whole application (with several windows the last one to set it wins), and a
    // palette of its own is never taken away again: it has to be kept up to date by hand. So the tool tips are left to the
    // platform until a theme is forced; from then on they get the palette of the window, or that of the desktop under
    // Auto (kept current by the palette change event).
    if (forced || tooltipPinned_) {
        tooltipOriginal();
        QToolTip::setPalette(forced ? palette() : QApplication::palette());
        tooltipPinned_ = true;
    }
}

void MainWindow::setKeepOnTop(bool on) {
    keepOnTop_ = on;
    if (keepOnTopAction_) keepOnTopAction_->setChecked(on);
    if (QWindow* handle = windowHandle()) handle->setFlag(Qt::WindowStaysOnTopHint, on);
    else setWindowFlag(Qt::WindowStaysOnTopHint, on);
}

// ------------------------------------------------------------------ keyboard

// Keys that no field took. Characters arrive as text (not key codes), so every keyboard layout works.
void MainWindow::keyPressEvent(QKeyEvent* event) {
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const int key = event->key();
    const bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
    // Ctrl+1 / Ctrl+2 by the key, not by the character: where the digits are on the shifted level of the number row
    // (Belgian and French AZERTY) Qt only knows Ctrl+Shift+& as Ctrl+1, while Windows takes the key itself.
    const int digitTab = digitRowTab(event);
    if (digitTab >= 0) {
        selectTab(digitTab);
        event->accept();
        return;
    }
    if (currentTab() == 0) {
        if (enter) {
            converter_->press(QStringLiteral("CONVERT"));
            event->accept();
            return;
        }
        if (key == Qt::Key_Escape) {
            converter_->press(QStringLiteral("RESET"));
            event->accept();
            return;
        }
        if (!ctrl) {
            const QString text = event->text();
            if (!text.isEmpty() && text.at(0).unicode() >= 0x20 && text.at(0).unicode() != 0x7F) {
                converter_->typeText(text);        // nothing has the focus: type into the active field
                event->accept();
                return;
            }
        }
    } else {
        if (ctrl) {
            if (key == Qt::Key_C) {
                if (shift) calculator_->copyDecimal();
                else calculator_->copyMain();
                event->accept();
                return;
            }
            if (key == Qt::Key_V) {
                calculator_->pasteFromClipboard();
                event->accept();
                return;
            }
        } else {
            if (enter) {
                calculator_->press(QStringLiteral("="));
                event->accept();
                return;
            }
            if (key == Qt::Key_Backspace) {
                calculator_->press(QStringLiteral("BACK"));
                event->accept();
                return;
            }
            if (key == Qt::Key_Delete) {
                calculator_->press(QStringLiteral("CE"));
                event->accept();
                return;
            }
            if (key == Qt::Key_Escape) {
                calculator_->press(QStringLiteral("C"));
                event->accept();
                return;
            }
            const QString text = event->text();
            if (!text.isEmpty()) {
                calculator_->typeText(text);
                event->accept();
                return;
            }
        }
    }
    QWidget::keyPressEvent(event);
}

void MainWindow::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    if (!painted_) {
        painted_ = true;
        if (firstPaintHook()) {
            // After the whole frame (the children paint after this event), at the next turn of the event loop.
            QMetaObject::invokeMethod(this, [] {
                if (firstPaintHook()) firstPaintHook()();
            }, Qt::QueuedConnection);
        }
    }
}

}  // namespace hcqt
