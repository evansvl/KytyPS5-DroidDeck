#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QVBoxLayout>
#include <QWidget>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

static QString gamesRoot()
{
    const QByteArray env = qgetenv("KYTY_GAMES_DIR");
    if (!env.isEmpty()) {
        return QString::fromUtf8(env);
    }
    return QStringLiteral("/storage/emulated/0/KytyPS5");
}

static QString kytyDir()
{
    const QByteArray env = qgetenv("KYTY_DIR");
    if (!env.isEmpty()) {
        return QString::fromUtf8(env);
    }
    return QCoreApplication::applicationDirPath();
}

static bool spawnDetached(const QString &program, const QStringList &arguments, QString *error)
{
    const QByteArray programUtf8 = QFileInfo(program).absoluteFilePath().toUtf8();
    std::vector<QByteArray> encoded;
    encoded.reserve(static_cast<size_t>(arguments.size()) + 1);
    encoded.push_back(programUtf8);
    for (const QString &argument : arguments) {
        encoded.push_back(argument.toUtf8());
    }

    std::vector<char *> argv;
    argv.reserve(encoded.size() + 1);
    for (QByteArray &value : encoded) {
        argv.push_back(value.data());
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        if (error) {
            *error = QStringLiteral("fork() failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    if (pid == 0) {
        setsid();
        execv(programUtf8.constData(), argv.data());
        _exit(127);
    }

    return true;
}

struct GameEntry {
    QString directory;
    QString title;
    QString icon;
};

static QList<GameEntry> scanGames(const QString &root)
{
    QList<GameEntry> result;
    const fs::path base = fs::u8path(root.toStdString());
    std::error_code ec;

    if (!fs::exists(base, ec)) {
        return result;
    }

    fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;

    while (it != end) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }

        const fs::directory_entry entry = *it;
        if (entry.is_regular_file(ec) && entry.path().filename() == "eboot.bin") {
            const fs::path dir = entry.path().parent_path();
            GameEntry game;
            game.directory = QString::fromStdString(dir.string());
            game.title = QString::fromStdString(dir.filename().string());

            const fs::path icon = dir / "sce_sys" / "icon0.png";
            if (fs::exists(icon, ec)) {
                game.icon = QString::fromStdString(icon.string());
            }

            result.push_back(game);
            it.disable_recursion_pending();
        }

        it.increment(ec);
    }

    std::sort(result.begin(), result.end(), [](const GameEntry &a, const GameEntry &b) {
        return a.title.toLower() < b.title.toLower();
    });

    return result;
}

class LauncherWindow final : public QMainWindow
{
public:
    LauncherWindow()
        : m_settings(QStringLiteral("evansvl"), QStringLiteral("KytyPS5-DroidDeck"))
    {
        setWindowTitle(QStringLiteral("KytyPS5 DroidDeck"));
        resize(1180, 760);

        auto *central = new QWidget(this);
        auto *rootLayout = new QVBoxLayout(central);
        rootLayout->setContentsMargins(24, 20, 24, 20);
        rootLayout->setSpacing(16);

        auto *header = new QHBoxLayout();
        auto *titles = new QVBoxLayout();
        auto *title = new QLabel(QStringLiteral("KytyPS5"));
        title->setObjectName(QStringLiteral("mainTitle"));
        auto *subtitle = new QLabel(QStringLiteral("DroidDeck launcher"));
        subtitle->setObjectName(QStringLiteral("subTitle"));
        titles->addWidget(title);
        titles->addWidget(subtitle);
        header->addLayout(titles);
        header->addStretch(1);

        auto *refresh = new QPushButton(QStringLiteral("Refresh"));
        refresh->setMinimumHeight(52);
        auto *advanced = new QPushButton(QStringLiteral("Advanced Kyty"));
        advanced->setMinimumHeight(52);
        header->addWidget(refresh);
        header->addWidget(advanced);
        rootLayout->addLayout(header);

        auto *pathLabel = new QLabel(QStringLiteral("Games folder: %1").arg(gamesRoot()));
        pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        pathLabel->setObjectName(QStringLiteral("pathLabel"));
        rootLayout->addWidget(pathLabel);

        auto *settingsBox = new QGroupBox(QStringLiteral("Launch settings"));
        auto *settingsLayout = new QHBoxLayout(settingsBox);
        settingsLayout->setContentsMargins(16, 14, 16, 14);
        settingsLayout->setSpacing(14);

        m_resolution = new QComboBox();
        m_resolution->addItems({QStringLiteral("1280x720"), QStringLiteral("1920x1080"), QStringLiteral("2560x1440")});
        m_resolution->setCurrentText(m_settings.value(QStringLiteral("resolution"), QStringLiteral("1280x720")).toString());

        m_presentMode = new QComboBox();
        m_presentMode->addItems({QStringLiteral("Mailbox"), QStringLiteral("Fifo"), QStringLiteral("Immediate")});
        m_presentMode->setCurrentText(m_settings.value(QStringLiteral("presentMode"), QStringLiteral("Mailbox")).toString());

        m_fullscreen = new QCheckBox(QStringLiteral("Fullscreen"));
        m_fullscreen->setChecked(m_settings.value(QStringLiteral("fullscreen"), true).toBool());
        m_perfOverlay = new QCheckBox(QStringLiteral("Performance overlay"));
        m_perfOverlay->setChecked(m_settings.value(QStringLiteral("perfOverlay"), false).toBool());
        m_skipNotice = new QCheckBox(QStringLiteral("Skip notices"));
        m_skipNotice->setChecked(m_settings.value(QStringLiteral("skipNotice"), false).toBool());

        settingsLayout->addWidget(new QLabel(QStringLiteral("Resolution")));
        settingsLayout->addWidget(m_resolution);
        settingsLayout->addWidget(new QLabel(QStringLiteral("Present mode")));
        settingsLayout->addWidget(m_presentMode);
        settingsLayout->addWidget(m_fullscreen);
        settingsLayout->addWidget(m_perfOverlay);
        settingsLayout->addWidget(m_skipNotice);
        settingsLayout->addStretch(1);
        rootLayout->addWidget(settingsBox);

        m_scroll = new QScrollArea();
        m_scroll->setWidgetResizable(true);
        m_scroll->setFrameShape(QFrame::NoFrame);
        rootLayout->addWidget(m_scroll, 1);

        setCentralWidget(central);

        connect(refresh, &QPushButton::clicked, this, [this] { reloadGames(); });
        connect(advanced, &QPushButton::clicked, this, [this] { launchAdvanced(); });

        connect(m_resolution, &QComboBox::currentTextChanged, this, [this](const QString &value) {
            m_settings.setValue(QStringLiteral("resolution"), value);
        });
        connect(m_presentMode, &QComboBox::currentTextChanged, this, [this](const QString &value) {
            m_settings.setValue(QStringLiteral("presentMode"), value);
        });
        connect(m_fullscreen, &QCheckBox::toggled, this, [this](bool value) {
            m_settings.setValue(QStringLiteral("fullscreen"), value);
        });
        connect(m_perfOverlay, &QCheckBox::toggled, this, [this](bool value) {
            m_settings.setValue(QStringLiteral("perfOverlay"), value);
        });
        connect(m_skipNotice, &QCheckBox::toggled, this, [this](bool value) {
            m_settings.setValue(QStringLiteral("skipNotice"), value);
        });

        setStyleSheet(QStringLiteral(R"(
            QWidget {
                background: #11141a;
                color: #f4f6f8;
                font-size: 16px;
            }
            QLabel#mainTitle {
                font-size: 34px;
                font-weight: 700;
            }
            QLabel#subTitle, QLabel#pathLabel {
                color: #9aa4b2;
            }
            QGroupBox {
                border: 1px solid #29313d;
                border-radius: 12px;
                margin-top: 12px;
                padding-top: 10px;
                font-weight: 600;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 14px;
                padding: 0 6px;
            }
            QPushButton, QComboBox {
                background: #202733;
                border: 1px solid #394455;
                border-radius: 10px;
                padding: 10px 16px;
            }
            QPushButton:hover, QPushButton:focus, QComboBox:focus {
                border: 2px solid #6aa9ff;
                background: #293547;
            }
            QPushButton#playButton {
                background: #2463eb;
                border-color: #3b82f6;
                font-weight: 700;
                min-height: 44px;
            }
            QWidget#gameCard {
                background: #181d25;
                border: 1px solid #2a3340;
                border-radius: 16px;
            }
        )"));

        QDir().mkpath(gamesRoot());
        reloadGames();
    }

private:
    void reloadGames()
    {
        auto *content = new QWidget();
        auto *grid = new QGridLayout(content);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(16);
        grid->setVerticalSpacing(16);

        const QList<GameEntry> games = scanGames(gamesRoot());
        if (games.isEmpty()) {
            auto *empty = new QLabel(QStringLiteral(
                "No games found.\n\nPut each game in:\n/storage/emulated/0/KytyPS5/<Game>/\n\nThe game directory must contain eboot.bin."));
            empty->setAlignment(Qt::AlignCenter);
            empty->setMinimumHeight(360);
            grid->addWidget(empty, 0, 0);
        } else {
            constexpr int columns = 3;
            for (int i = 0; i < games.size(); ++i) {
                grid->addWidget(createGameCard(games.at(i)), i / columns, i % columns);
            }
            grid->setRowStretch((games.size() + columns - 1) / columns, 1);
        }

        QWidget *old = m_scroll->takeWidget();
        m_scroll->setWidget(content);
        if (old) {
            old->deleteLater();
        }
    }

    QWidget *createGameCard(const GameEntry &game)
    {
        auto *card = new QWidget();
        card->setObjectName(QStringLiteral("gameCard"));
        card->setMinimumSize(300, 300);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(10);

        auto *cover = new QLabel();
        cover->setAlignment(Qt::AlignCenter);
        cover->setMinimumHeight(180);
        cover->setStyleSheet(QStringLiteral("background:#0d1015;border-radius:10px;"));

        if (!game.icon.isEmpty()) {
            QPixmap pixmap(game.icon);
            if (!pixmap.isNull()) {
                cover->setPixmap(pixmap.scaled(260, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }
        if (cover->pixmap().isNull()) {
            cover->setText(QStringLiteral("PS5"));
            cover->setStyleSheet(QStringLiteral("background:#0d1015;border-radius:10px;font-size:42px;font-weight:700;color:#536174;"));
        }

        auto *title = new QLabel(game.title);
        title->setWordWrap(true);
        title->setStyleSheet(QStringLiteral("font-size:20px;font-weight:700;"));

        auto *path = new QLabel(game.directory);
        path->setWordWrap(true);
        path->setStyleSheet(QStringLiteral("color:#8893a2;font-size:12px;"));

        auto *play = new QPushButton(QStringLiteral("Play"));
        play->setObjectName(QStringLiteral("playButton"));
        connect(play, &QPushButton::clicked, this, [this, game] { launchGame(game); });

        layout->addWidget(cover);
        layout->addWidget(title);
        layout->addWidget(path);
        layout->addStretch(1);
        layout->addWidget(play);

        return card;
    }

    void launchGame(const GameEntry &game)
    {
        const QString emulator = QDir(kytyDir()).filePath(QStringLiteral("kyty_emulator"));
        if (!QFileInfo::exists(emulator)) {
            QMessageBox::critical(this, QStringLiteral("KytyPS5"), QStringLiteral("kyty_emulator was not found:\n%1").arg(emulator));
            return;
        }

        const QStringList resolution = m_resolution->currentText().split(QLatin1Char('x'));
        QStringList args;
        args << QStringLiteral("--game") << game.directory;
        if (resolution.size() == 2) {
            args << QStringLiteral("--screen-width") << resolution.at(0);
            args << QStringLiteral("--screen-height") << resolution.at(1);
        }
        args << QStringLiteral("--present-mode") << m_presentMode->currentText();
        if (m_fullscreen->isChecked()) {
            args << QStringLiteral("--fullscreen");
        }
        if (m_perfOverlay->isChecked()) {
            args << QStringLiteral("--perf-overlay");
        }
        if (m_skipNotice->isChecked()) {
            args << QStringLiteral("--skip-notice-screen");
        }

        QString error;
        if (!spawnDetached(emulator, args, &error)) {
            QMessageBox::critical(this, QStringLiteral("Launch failed"), error);
        }
    }

    void launchAdvanced()
    {
        const QString launcher = QDir(kytyDir()).filePath(QStringLiteral("kyty_launcher"));
        QString error;
        if (!spawnDetached(launcher, {}, &error)) {
            QMessageBox::critical(this, QStringLiteral("Launch failed"), error);
        }
    }

    QSettings m_settings;
    QScrollArea *m_scroll = nullptr;
    QComboBox *m_resolution = nullptr;
    QComboBox *m_presentMode = nullptr;
    QCheckBox *m_fullscreen = nullptr;
    QCheckBox *m_perfOverlay = nullptr;
    QCheckBox *m_skipNotice = nullptr;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("KytyPS5 DroidDeck"));
    QApplication::setOrganizationName(QStringLiteral("evansvl"));

    LauncherWindow window;
    window.show();
    return app.exec();
}
