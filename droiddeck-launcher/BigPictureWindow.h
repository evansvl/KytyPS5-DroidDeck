#pragma once

#include "Gamepad.h"
#include "Library.h"
#include "Settings.h"

#include <QList>
#include <QMainWindow>
#include <QSettings>
#include <QStringList>

#include <initializer_list>
#include <memory>
#include <sys/types.h>

template <typename T>
class IntList final : public QList<T>
{
public:
    using QList<T>::QList;

    IntList &operator=(std::initializer_list<T> values)
    {
        this->clear();
        for (const T &value : values) this->append(value);
        return *this;
    }

    int size() const noexcept
    {
        return static_cast<int>(QList<T>::size());
    }
};

class QBoxLayout;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class QWidget;

class BigPictureWindow final : public QMainWindow
{
public:
    explicit BigPictureWindow(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    enum class View { Library, Settings, Browser, Error, Editor };
    enum class SettingKind { Toggle, Choice, Integer, Text, TextList };
    enum class BrowserKind { Parent, Directory, AddCurrent, Zip, DirectGame };

    struct SettingSpec {
        QString label;
        QString description;
        QString key;
        SettingKind kind = SettingKind::Toggle;
        QStringList choices;
        int minimum = 0;
        int maximum = 0;
        int step = 1;
    };

    QSettings m_store;
    EmulatorSettings m_emu;
    std::unique_ptr<EvdevGamepad> m_gamepad;
    QTimer *m_processTimer = nullptr;

    QStackedWidget *m_pages = nullptr;
    QWidget *m_libraryPage = nullptr;
    QWidget *m_settingsPage = nullptr;
    QWidget *m_browserPage = nullptr;
    QWidget *m_errorPage = nullptr;
    QWidget *m_editorPage = nullptr;
    View m_view = View::Library;

    QList<QVBoxLayout *> m_safeLayouts;

    QLabel *m_padStatus = nullptr;
    QLabel *m_heroTitle = nullptr;
    QLabel *m_heroMeta = nullptr;
    QLabel *m_heroPath = nullptr;
    QLabel *m_heroArt = nullptr;
    QLabel *m_libraryCounter = nullptr;
    QLabel *m_libraryHint = nullptr;
    QScrollArea *m_libraryScroll = nullptr;
    QWidget *m_libraryContainer = nullptr;
    QHBoxLayout *m_libraryStrip = nullptr;
    IntList<QPushButton *> m_libraryTiles;
    QList<GameEntry> m_games;
    int m_libraryIndex = 0;

    QLabel *m_settingsSectionTitle = nullptr;
    QLabel *m_settingsSectionDesc = nullptr;
    QLabel *m_settingsTabs = nullptr;
    QScrollArea *m_settingsScroll = nullptr;
    QWidget *m_settingsRowsWidget = nullptr;
    QVBoxLayout *m_settingsRows = nullptr;
    IntList<QPushButton *> m_settingButtons;
    IntList<QList<SettingSpec>> m_settingSections;
    int m_settingSection = 0;
    int m_settingIndex = 0;

    QLabel *m_browserPath = nullptr;
    QLabel *m_browserStatus = nullptr;
    QListWidget *m_browserList = nullptr;
    QString m_browserDir;

    QLabel *m_errorTitle = nullptr;
    QLabel *m_errorSummary = nullptr;
    QLabel *m_errorHint = nullptr;
    QPlainTextEdit *m_errorDetails = nullptr;
    bool m_errorDetailsVisible = false;

    QLabel *m_editorTitle = nullptr;
    QLabel *m_editorDescription = nullptr;
    QLineEdit *m_editorLine = nullptr;
    QString m_editorKey;
    SettingKind m_editorKind = SettingKind::Text;

    pid_t m_childPid = -1;
    int m_childTicks = 0;
    QString m_childLog;

    QWidget *buildLibraryPage();
    QWidget *buildSettingsPage();
    QWidget *buildBrowserPage();
    QWidget *buildErrorPage();
    QWidget *buildEditorPage();

    QVBoxLayout *makeSafeLayout(QWidget *page);
    QWidget *makeBrandHeader(const QString &active);
    void buildSettingSpecs();
    void rebuildSettingsRows();
    QString settingValueText(const SettingSpec &spec) const;
    void adjustSetting(int delta);
    void activateSetting();
    void openTextEditor(const SettingSpec &spec);
    void closeTextEditor(bool save);

    void reloadGames();
    void selectLibraryIndex(int index);
    void activateLibrarySelection();
    QPushButton *createGameTile(const GameEntry &game, int index);
    QPushButton *createAddTile(int index);
    void updateHero();

    void openBrowser();
    void closeBrowser();
    void populateBrowser(const QString &directory);
    void activateBrowserEntry();
    void addCurrentBrowserFolder();
    bool currentBrowserFolderIsGame() const;
    void setBrowserStatus(const QString &text, bool error = false);

    void openSettings(int section = 0);
    void closeSettings();
    void switchSettingsSection(int delta);
    void selectSettingIndex(int index);

    void showError(const QString &title, const QString &summary, const QString &details);
    void closeError();
    void toggleErrorDetails();

    void launchGame(const GameEntry &game);
    void startTrackedProcess(const QString &program, const QStringList &args);
    void pollChild();

    void handleGamepad(PadAction action);
    void switchView(View view, QWidget *page);
    void applyResponsiveMetrics();
    void applyStyle(double scale);
    void forceFullscreen();
};
