#pragma once

#include "Gamepad.h"
#include "Library.h"
#include "Settings.h"

#include <QMainWindow>
#include <QSettings>

#include <memory>
#include <sys/types.h>

class QLabel;
class QListWidget;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QPushButton;
class QVBoxLayout;

class LauncherWindow final : public QMainWindow
{
public:
    explicit LauncherWindow(QWidget *parent = nullptr);

private:
    void buildUi();
    QWidget *buildLibraryPage();
    QWidget *buildGraphicsPage();
    QWidget *buildSystemPage();
    QWidget *buildControllerPage();
    QWidget *buildDebugPage();
    QWidget *buildExpertPage();

    void reloadGames();
    QWidget *createGameCard(const GameEntry &game);
    void addGame();
    void launchGame(const GameEntry &game);
    void startTrackedProcess(const QString &program, const QStringList &args, const QString &kind);
    void pollChild();

    void handleGamepad(PadAction action);
    void switchSection(int delta);
    void focusFirstControl();
    void moveFocus(bool forward);
    void cycleFocusedValue();
    void adjustFocusedValue(int delta);

    void addHeaderHint(const QString &text);
    QWidget *settingsPage(const QString &title, const QString &description, QVBoxLayout **content);

    QSettings m_settings;
    EmulatorSettings m_emu;

    QListWidget *m_sections = nullptr;
    QStackedWidget *m_stack = nullptr;
    QScrollArea *m_libraryScroll = nullptr;
    QLabel *m_controllerLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QPushButton *m_firstPlay = nullptr;

    std::unique_ptr<EvdevGamepad> m_gamepad;
    QTimer *m_processTimer = nullptr;
    pid_t m_childPid = -1;
    QString m_childLog;
    QString m_childKind;
    int m_childTicks = 0;
};
