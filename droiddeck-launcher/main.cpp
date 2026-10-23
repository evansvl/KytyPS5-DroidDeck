#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
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
#include <QSocketNotifier>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <functional>
#include <linux/input.h>
#include <memory>
#include <string>
#include <sys/ioctl.h>
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

static QString logsRoot()
{
    const QString dir = QDir::homePath() + QStringLiteral("/.local/share/KytyPS5-DroidDeck/logs");
    QDir().mkpath(dir);
    return dir;
}

struct SpawnResult {
    bool ok = false;
    pid_t pid = -1;
    QString logFile;
    QString error;
};

static SpawnResult spawnWithLog(const QString &program, const QStringList &arguments, const QString &logFile)
{
    SpawnResult result;
    result.logFile = logFile;

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

    int errorPipe[2] = {-1, -1};
    if (pipe2(errorPipe, O_CLOEXEC) != 0) {
        result.error = QStringLiteral("pipe2() failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return result;
    }

    const QByteArray logUtf8 = logFile.toUtf8();
    const pid_t pid = fork();
    if (pid < 0) {
        close(errorPipe[0]);
        close(errorPipe[1]);
        result.error = QStringLiteral("fork() failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return result;
    }

    if (pid == 0) {
        close(errorPipe[0]);

        const int logFd = open(logUtf8.constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logFd >= 0) {
            dup2(logFd, STDOUT_FILENO);
            dup2(logFd, STDERR_FILENO);
            if (logFd > STDERR_FILENO) {
                close(logFd);
            }
        }

        execv(programUtf8.constData(), argv.data());

        const int childErrno = errno;
        (void)!write(errorPipe[1], &childErrno, sizeof(childErrno));
        _exit(127);
    }

    close(errorPipe[1]);

    int childErrno = 0;
    ssize_t n;
    do {
        n = read(errorPipe[0], &childErrno, sizeof(childErrno));
    } while (n < 0 && errno == EINTR);
    close(errorPipe[0]);

    if (n == static_cast<ssize_t>(sizeof(childErrno))) {
        (void)waitpid(pid, nullptr, 0);
        result.error = QStringLiteral("execv() failed: %1").arg(QString::fromLocal8Bit(std::strerror(childErrno)));
        return result;
    }

    if (n < 0) {
        result.error = QStringLiteral("Failed to verify child exec: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return result;
    }

    result.ok = true;
    result.pid = pid;
    return result;
}

static QString readLogTail(const QString &path, int maxBytes = 12000)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("No log output was available.");
    }

    if (file.size() > maxBytes) {
        file.seek(file.size() - maxBytes);
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

struct GameEntry {
    QString directory;
    QString title;
    QString icon;
};

static QList<GameEntry> scanGames(const QString &root)
{
    QList<GameEntry> result;
    const fs::path base(root.toStdString());
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

enum class PadAction {
    Up,
    Down,
    Left,
    Right,
    Activate,
    Back,
    Refresh,
    Advanced,
};

class EvdevGamepad final : public QObject
{
public:
    EvdevGamepad(std::function<void(PadAction)> actionCallback,
                 std::function<void(int)> countCallback,
                 QObject *parent = nullptr)
        : QObject(parent)
        , m_actionCallback(std::move(actionCallback))
        , m_countCallback(std::move(countCallback))
    {
        rescan();
        m_scanTimer = new QTimer(this);
        m_scanTimer->setInterval(2000);
        connect(m_scanTimer, &QTimer::timeout, this, [this] { rescan(); });
        m_scanTimer->start();
    }

    ~EvdevGamepad() override
    {
        for (auto &device : m_devices) {
            if (device.fd >= 0) {
                close(device.fd);
            }
        }
    }

private:
    struct Device {
        QString path;
        int fd = -1;
        std::unique_ptr<QSocketNotifier> notifier;
        int xCenter = 0;
        int yCenter = 0;
        int xThreshold = 12000;
        int yThreshold = 12000;
        int xDirection = 0;
        int yDirection = 0;
    };

    static bool bitSet(const unsigned char *bits, int code)
    {
        return (bits[code / 8] & static_cast<unsigned char>(1u << (code % 8))) != 0;
    }

    static bool isGamepad(int fd)
    {
        std::array<unsigned char, (KEY_MAX + 8) / 8> keys {};
        std::array<unsigned char, (ABS_MAX + 8) / 8> abs {};

        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys.data()) < 0) {
            return false;
        }
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs.data()) < 0) {
            return false;
        }

        return bitSet(keys.data(), BTN_SOUTH) && bitSet(abs.data(), ABS_X);
    }

    static void configureAxis(int fd, int code, int &center, int &threshold)
    {
        input_absinfo info {};
        if (ioctl(fd, EVIOCGABS(code), &info) == 0 && info.maximum > info.minimum) {
            center = info.minimum + ((info.maximum - info.minimum) / 2);
            threshold = std::max(1, (info.maximum - info.minimum) / 4);
        }
    }

    bool alreadyOpen(const QString &path) const
    {
        return std::any_of(m_devices.begin(), m_devices.end(), [&path](const Device &device) {
            return device.path == path;
        });
    }

    void rescan()
    {
        const int previousCount = static_cast<int>(m_devices.size());

        for (int index = 0; index < 64; ++index) {
            const QString path = QStringLiteral("/dev/input/event%1").arg(index);
            if (alreadyOpen(path)) {
                continue;
            }

            const QByteArray pathUtf8 = path.toUtf8();
            const int fd = open(pathUtf8.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) {
                continue;
            }

            if (!isGamepad(fd)) {
                close(fd);
                continue;
            }

            Device device;
            device.path = path;
            device.fd = fd;
            configureAxis(fd, ABS_X, device.xCenter, device.xThreshold);
            configureAxis(fd, ABS_Y, device.yCenter, device.yThreshold);
            device.notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read, this);
            connect(device.notifier.get(), &QSocketNotifier::activated, this, [this, fd] {
                readEvents(fd);
            });
            m_devices.push_back(std::move(device));
        }

        if (static_cast<int>(m_devices.size()) != previousCount && m_countCallback) {
            m_countCallback(static_cast<int>(m_devices.size()));
        } else if (previousCount == 0 && m_countCallback) {
            m_countCallback(static_cast<int>(m_devices.size()));
        }
    }

    Device *deviceForFd(int fd)
    {
        for (auto &device : m_devices) {
            if (device.fd == fd) {
                return &device;
            }
        }
        return nullptr;
    }

    void emitAction(PadAction action)
    {
        if (m_actionCallback) {
            m_actionCallback(action);
        }
    }

    static int directionForAxis(int value, int center, int threshold)
    {
        if (value < center - threshold) {
            return -1;
        }
        if (value > center + threshold) {
            return 1;
        }
        return 0;
    }

    void handleAbs(Device &device, const input_event &event)
    {
        if (event.code == ABS_HAT0X) {
            if (event.value < 0) emitAction(PadAction::Left);
            if (event.value > 0) emitAction(PadAction::Right);
            return;
        }
        if (event.code == ABS_HAT0Y) {
            if (event.value < 0) emitAction(PadAction::Up);
            if (event.value > 0) emitAction(PadAction::Down);
            return;
        }

        if (event.code == ABS_X) {
            const int direction = directionForAxis(event.value, device.xCenter, device.xThreshold);
            if (direction != 0 && direction != device.xDirection) {
                emitAction(direction < 0 ? PadAction::Left : PadAction::Right);
            }
            device.xDirection = direction;
            return;
        }

        if (event.code == ABS_Y) {
            const int direction = directionForAxis(event.value, device.yCenter, device.yThreshold);
            if (direction != 0 && direction != device.yDirection) {
                emitAction(direction < 0 ? PadAction::Up : PadAction::Down);
            }
            device.yDirection = direction;
        }
    }

    void readEvents(int fd)
    {
        Device *device = deviceForFd(fd);
        if (!device) {
            return;
        }

        input_event event {};
        for (;;) {
            const ssize_t size = read(fd, &event, sizeof(event));
            if (size == static_cast<ssize_t>(sizeof(event))) {
                if (event.type == EV_KEY && event.value == 1) {
                    switch (event.code) {
                    case BTN_SOUTH:
                        emitAction(PadAction::Activate);
                        break;
                    case BTN_EAST:
                        emitAction(PadAction::Back);
                        break;
                    case BTN_NORTH:
                        emitAction(PadAction::Refresh);
                        break;
                    case BTN_WEST:
                        emitAction(PadAction::Advanced);
                        break;
                    default:
                        break;
                    }
                } else if (event.type == EV_ABS) {
                    handleAbs(*device, event);
                }
                continue;
            }

            if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            break;
        }
    }

    std::vector<Device> m_devices;
    QTimer *m_scanTimer = nullptr;
    std::function<void(PadAction)> m_actionCallback;
    std::function<void(int)> m_countCallback;
};

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

        m_controllerLabel = new QLabel(QStringLiteral("Controller: detecting..."));
        m_controllerLabel->setObjectName(QStringLiteral("controllerLabel"));
        header->addWidget(m_controllerLabel);

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

        auto *controlsHint = new QLabel(QStringLiteral("Gamepad: D-pad / left stick = navigate · A = select · Y = refresh · X = Advanced Kyty"));
        controlsHint->setObjectName(QStringLiteral("controlsHint"));
        rootLayout->addWidget(controlsHint);

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

        m_statusLabel = new QLabel(QStringLiteral("Ready"));
        m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
        rootLayout->addWidget(m_statusLabel);

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
            QLabel#subTitle, QLabel#pathLabel, QLabel#controlsHint, QLabel#statusLabel, QLabel#controllerLabel {
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
            QPushButton:hover, QPushButton:focus, QComboBox:focus, QCheckBox:focus {
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

        m_gamepad = std::make_unique<EvdevGamepad>(
            [this](PadAction action) { handleGamepad(action); },
            [this](int count) {
                m_controllerLabel->setText(count > 0
                    ? QStringLiteral("Controller: connected (%1)").arg(count)
                    : QStringLiteral("Controller: not detected"));
            },
            this);

        m_processTimer = new QTimer(this);
        m_processTimer->setInterval(350);
        connect(m_processTimer, &QTimer::timeout, this, [this] { pollChild(); });
    }

private:
    void reloadGames()
    {
        auto *content = new QWidget();
        auto *grid = new QGridLayout(content);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(16);
        grid->setVerticalSpacing(16);

        m_firstPlay = nullptr;
        const QList<GameEntry> games = scanGames(gamesRoot());
        if (games.isEmpty()) {
            auto *empty = new QLabel(QStringLiteral(
                "No games found.\n\nPut each game in:\n/storage/emulated/0/KytyPS5/<Game>/\n\nThe game directory must contain eboot.bin."));
            empty->setAlignment(Qt::AlignCenter);
            empty->setMinimumHeight(360);
            grid->addWidget(empty, 0, 0);
            m_statusLabel->setText(QStringLiteral("No games found"));
        } else {
            constexpr int columns = 3;
            for (int i = 0; i < games.size(); ++i) {
                grid->addWidget(createGameCard(games.at(i)), i / columns, i % columns);
            }
            grid->setRowStretch((games.size() + columns - 1) / columns, 1);
            m_statusLabel->setText(QStringLiteral("Found %1 game(s)").arg(games.size()));
        }

        QWidget *old = m_scroll->takeWidget();
        m_scroll->setWidget(content);
        if (old) {
            old->deleteLater();
        }

        if (m_firstPlay) {
            QTimer::singleShot(0, this, [this] {
                if (m_firstPlay) {
                    m_firstPlay->setFocus(Qt::OtherFocusReason);
                }
            });
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

        bool hasCover = false;
        if (!game.icon.isEmpty()) {
            QPixmap pixmap(game.icon);
            if (!pixmap.isNull()) {
                cover->setPixmap(pixmap.scaled(260, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                hasCover = true;
            }
        }
        if (!hasCover) {
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
        play->setFocusPolicy(Qt::StrongFocus);
        connect(play, &QPushButton::clicked, this, [this, game] { launchGame(game); });

        if (!m_firstPlay) {
            m_firstPlay = play;
        }

        layout->addWidget(cover);
        layout->addWidget(title);
        layout->addWidget(path);
        layout->addStretch(1);
        layout->addWidget(play);

        return card;
    }

    void startTrackedProcess(const QString &program, const QStringList &args, const QString &kind)
    {
        if (m_childPid > 0) {
            QMessageBox::information(this, QStringLiteral("KytyPS5"), QStringLiteral("A Kyty process is already running."));
            return;
        }

        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
        const QString logFile = QDir(logsRoot()).filePath(QStringLiteral("%1-%2.log").arg(kind, stamp));
        const SpawnResult result = spawnWithLog(program, args, logFile);

        if (!result.ok) {
            QMessageBox::critical(this,
                                  QStringLiteral("Launch failed"),
                                  result.error + QStringLiteral("\n\nLog: ") + logFile);
            return;
        }

        m_childPid = result.pid;
        m_childLog = result.logFile;
        m_childKind = kind;
        m_childTicks = 0;
        m_statusLabel->setText(QStringLiteral("Starting %1... PID %2").arg(kind).arg(result.pid));
        m_processTimer->start();
        hide();
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

        startTrackedProcess(emulator, args, QStringLiteral("game"));
    }

    void launchAdvanced()
    {
        const QString launcher = QDir(kytyDir()).filePath(QStringLiteral("kyty_launcher"));
        if (!QFileInfo::exists(launcher)) {
            QMessageBox::critical(this, QStringLiteral("KytyPS5"), QStringLiteral("kyty_launcher was not found:\n%1").arg(launcher));
            return;
        }
        startTrackedProcess(launcher, {}, QStringLiteral("advanced"));
    }

    void pollChild()
    {
        if (m_childPid <= 0) {
            m_processTimer->stop();
            return;
        }

        ++m_childTicks;
        int status = 0;
        const pid_t result = waitpid(m_childPid, &status, WNOHANG);
        if (result == 0) {
            if (m_childTicks == 5) {
                m_statusLabel->setText(QStringLiteral("%1 is running · log: %2").arg(m_childKind, m_childLog));
            }
            return;
        }

        if (result < 0) {
            m_processTimer->stop();
            m_childPid = -1;
            show();
            raise();
            activateWindow();
            m_statusLabel->setText(QStringLiteral("Unable to track child process"));
            return;
        }

        m_processTimer->stop();
        const bool quickExit = m_childTicks < 10;
        const QString output = readLogTail(m_childLog);
        QString summary;

        if (WIFEXITED(status)) {
            summary = QStringLiteral("Process exited with code %1").arg(WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            summary = QStringLiteral("Process was terminated by signal %1").arg(WTERMSIG(status));
        } else {
            summary = QStringLiteral("Process stopped");
        }

        m_childPid = -1;
        show();
        raise();
        activateWindow();
        m_statusLabel->setText(summary + QStringLiteral(" · log: ") + m_childLog);

        if (quickExit || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            QMessageBox box(QMessageBox::Critical,
                            QStringLiteral("Kyty process ended"),
                            summary,
                            QMessageBox::Ok,
                            this);
            box.setInformativeText(QStringLiteral("The launcher captured the emulator output. Open Details below."));
            box.setDetailedText(output.isEmpty() ? QStringLiteral("No output was written.\nLog: %1").arg(m_childLog) : output);
            box.exec();
        }
    }

    void handleGamepad(PadAction action)
    {
        if (m_childPid > 0 || !isVisible()) {
            return;
        }

        QWidget *focused = QApplication::focusWidget();

        if (action == PadAction::Refresh) {
            reloadGames();
            return;
        }
        if (action == PadAction::Advanced) {
            launchAdvanced();
            return;
        }
        if (action == PadAction::Activate) {
            if (auto *button = qobject_cast<QPushButton *>(focused)) {
                button->click();
                return;
            }
            if (auto *check = qobject_cast<QCheckBox *>(focused)) {
                check->toggle();
                return;
            }
            if (auto *combo = qobject_cast<QComboBox *>(focused)) {
                combo->setCurrentIndex((combo->currentIndex() + 1) % combo->count());
                return;
            }
            if (m_firstPlay) {
                m_firstPlay->click();
            }
            return;
        }
        if (action == PadAction::Back) {
            if (m_firstPlay) {
                m_firstPlay->setFocus(Qt::OtherFocusReason);
            }
            return;
        }

        if (auto *combo = qobject_cast<QComboBox *>(focused)) {
            if (action == PadAction::Left || action == PadAction::Up) {
                combo->setCurrentIndex(std::max(0, combo->currentIndex() - 1));
                return;
            }
            if (action == PadAction::Right || action == PadAction::Down) {
                combo->setCurrentIndex(std::min(combo->count() - 1, combo->currentIndex() + 1));
                return;
            }
        }

        if (action == PadAction::Up || action == PadAction::Left) {
            focusPreviousChild();
        } else if (action == PadAction::Down || action == PadAction::Right) {
            focusNextChild();
        }
    }

    QSettings m_settings;
    QScrollArea *m_scroll = nullptr;
    QComboBox *m_resolution = nullptr;
    QComboBox *m_presentMode = nullptr;
    QCheckBox *m_fullscreen = nullptr;
    QCheckBox *m_perfOverlay = nullptr;
    QCheckBox *m_skipNotice = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_controllerLabel = nullptr;
    QPushButton *m_firstPlay = nullptr;
    std::unique_ptr<EvdevGamepad> m_gamepad;
    QTimer *m_processTimer = nullptr;
    pid_t m_childPid = -1;
    QString m_childLog;
    QString m_childKind;
    int m_childTicks = 0;
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
