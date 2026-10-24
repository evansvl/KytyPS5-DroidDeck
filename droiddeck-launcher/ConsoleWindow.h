#pragma once

#include "Gamepad.h"
#include "Library.h"
#include "Settings.h"

#include <QList>
#include <QMainWindow>
#include <QSettings>

#include <memory>
#include <sys/types.h>

class QHBoxLayout;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

class ConsoleWindow final : public QMainWindow
{
public:
    explicit ConsoleWindow(QWidget *parent = nullptr);

private:
    enum class ViewMode { Shell, Browser, Error };
    enum class BrowserEntryKind { Parent = 1, AddCurrent, Directory, Zip, DirectGame };

    void buildUi();
    QWidget *buildShellPage();
    QWidget *buildLibraryPage();
    QWidget *buildGraphicsPage();
    QWidget *buildSystemPage();
    QWidget *buildControllerPage();
    QWidget *buildDebugPage();
    QWidget *buildExpertPage();
    QWidget *buildBrowserPage();
    QWidget *buildErrorPage();
    QWidget *settingsPage(const QString &title, const QString &description, QVBoxLayout **content);

    void reloadGames();
    QPushButton *createGameCard(const GameEntry &game);
    QPushButton *createAddGameCard();
    void selectGame(const GameEntry &game);
    void focusLibraryButton(int index);
    int focusedLibraryIndex() const;

    void openBrowser();
    void closeBrowser();
    void populateBrowser(const QString &directory);
    void activateBrowserEntry();
    bool currentBrowserFolderIsGame() const;
    void addCurrentBrowserFolder();
    void showBrowserStatus(const QString &text, bool error = false);

    void showErrorPage(const QString &title, const QString &summary, const QString &details);
    void closeErrorPage();

    void launchGame(const GameEntry &game);
    void startTrackedProcess(const QString &program, const QStringList &args, const QString &kind);
    void pollChild();

    void handleGamepad(PadAction action);
    void switchSection(int delta);
    void focusFirstControl();
    void moveFocus(bool forward);
    void cycleFocusedValue();
    void adjustFocusedValue(int delta);
    void ensureFocusedVisible(QWidget *widget);
    void updateHints();

    QSettings m_settings;
    EmulatorSettings m_emu;

    QStackedWidget *m_rootStack = nullptr;
    QWidget *m_shellPage = nullptr;
    QWidget *m_browserPage = nullptr;
    QWidget *m_errorPage = nullptr;

    QListWidget *m_sections = nullptr;
    QStackedWidget *m_sectionStack = nullptr;
    QScrollArea *m_libraryScroll = nullptr;
    QWidget *m_libraryContainer = nullptr;
    QHBoxLayout *m_libraryStrip = nullptr;
    QList<QPushButton *> m_libraryButtons;
    QList<GameEntry> m_games;

    QLabel *m_heroArt = nullptr;
    QLabel *m_heroTitle = nullptr;
    QLabel *m_heroPath = nullptr;
    QLabel *m_heroStatus = nullptr;
    QPushButton *m_heroLaunch = nullptr;
    GameEntry m_selectedGame;
    bool m_hasSelectedGame = false;

    QLabel *m_controllerLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_hintLabel = nullptr;

    QLabel *m_browserPath = nullptr;
    QListWidget *m_browserList = nullptr;
    QLabel *m_browserStatus = nullptr;
    QString m_browserDir;

    QLabel *m_errorTitle = nullptr;
    QLabel *m_errorSummary = nullptr;
    QPlainTextEdit *m_errorDetails = nullptr;
    QPushButton *m_errorBack = nullptr;

    ViewMode m_viewMode = ViewMode::Shell;
    int m_lastSection = 0;

    std::unique_ptr<EvdevGamepad> m_gamepad;
    QTimer *m_processTimer = nullptr;
    pid_t m_childPid = -1;
    QString m_childLog;
    QString m_childKind;
    int m_childTicks = 0;
};
