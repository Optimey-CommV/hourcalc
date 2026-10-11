// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// The main window: a header with the tab bar (Converter / Calculator) and the menu button, and the two
// pages. Plain Qt Widgets, drawn by the style of the desktop (Breeze on Plasma). The menu holds the theme
// (Auto / Light / Dark), the decimal places 2-6, Keep on top (X11 only: there is no such thing on Wayland)
// and About. The settings live in the file of the core (hc::settings), like on Windows.

#pragma once

#include "hc_core.h"

#include <QElapsedTimer>
#include <QPalette>
#include <QString>
#include <QWidget>

#include <functional>

class QAction;
class QActionGroup;
class QMenu;
class QShortcut;
class QStackedWidget;
class QTabBar;
class QToolButton;

namespace hcqt {

class AboutDialog;
class CalculatorPage;
class ConverterPage;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    using ThemeMode = hc::settings::ThemeMode;

    // 'settingsPath': the settings file; empty = the settings are neither read nor written.
    // 'keepOnTop': -1 = offered where the platform has it (X11), 0 = never, 1 = always (tests, renders).
    explicit MainWindow(const QString& settingsPath, int keepOnTop = -1, QWidget* parent = nullptr);
    ~MainWindow() override;

    // ---- parts (the test and render surface)
    ConverterPage* converter() const { return converter_; }
    CalculatorPage* calculator() const { return calculator_; }
    QTabBar* tabBar() const { return tabs_; }
    QToolButton* menuButton() const { return menuButton_; }
    QMenu* menu() const { return menu_; }
    QAction* keepOnTopAction() const { return keepOnTopAction_; }
    QString settingsPath() const { return settingsPath_; }

    // ---- tabs and keypad
    int currentTab() const;
    void selectTab(int index);
    // The keypad of the current tab.
    void press(const QString& command);

    // ---- settings
    int decimalPlaces() const { return places_; }
    void setDecimalPlaces(int places);
    ThemeMode themeMode() const { return theme_; }
    // Applies at once to everything that is open and is saved when it changes.
    void setThemeMode(ThemeMode mode);
    // The palette the window uses is dark.
    bool isDark() const;
    bool keepOnTopOffered() const { return keepOnTopAction_ != nullptr; }
    bool keepOnTop() const { return keepOnTop_; }
    void setKeepOnTop(bool on);

    // ---- dialogs and menu
    AboutDialog* createAbout();
    void showAbout();
    // Opens the menu below its button (a popup on the screen). The click that closes it must not reopen it.
    void openMenu();
    // Whether a click on the menu button right now would only be the one that closed the menu.
    bool menuReopenBlocked() const;

    // ---- sizes
    QSize defaultSize() const;
    QSize minimumWindowSize() const;
    QSize sizeHint() const override { return defaultSize(); }

    // Called once, after the first paint of the window (the start-up measurement of the test build).
    static void setFirstPaintHook(std::function<void()> hook);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void buildMenu();
    void applyTheme();
    void applyMetrics();
    void syncPopups();
    static QPalette tooltipOriginal();
    void checkMenuItems();
    void savePlaces();
    void saveTheme();

    QString settingsPath_;
    QTabBar* tabs_ = nullptr;
    QToolButton* menuButton_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    ConverterPage* converter_ = nullptr;
    CalculatorPage* calculator_ = nullptr;
    QMenu* menu_ = nullptr;
    QActionGroup* themeGroup_ = nullptr;
    QActionGroup* placesGroup_ = nullptr;
    QAction* keepOnTopAction_ = nullptr;
    QAction* aboutAction_ = nullptr;
    int places_ = 2;
    ThemeMode theme_ = ThemeMode::Auto;
    bool keepOnTop_ = false;
    bool painted_ = false;
    bool applyingTheme_ = false;
    bool tooltipPinned_ = false;          // this window put its palette on the tool tips
    QWidget* paletteWatcher_ = nullptr;   // a hidden window with the palette of the desktop: tells when the desktop theme changes
    QElapsedTimer menuClosed_;
};

}  // namespace hcqt
