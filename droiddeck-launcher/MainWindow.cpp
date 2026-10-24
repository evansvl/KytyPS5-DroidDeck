#include "MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputMethod>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

struct SpawnResult {
    bool ok = false;
    pid_t pid = -1;
    QString logFile;
    QString error;
};

SpawnResult spawnWithLog(const QString &program, const QStringList &arguments, const QString &logFile)
{
    SpawnResult result;
    result.logFile = logFile;

    const QByteArray programUtf8 = QFileInfo(program).absoluteFilePath().toUtf8();
    std::vector<QByteArray> encoded;
    encoded.reserve(static_cast<size_t>(arguments.size()) + 1);
    encoded.push_back(programUtf8);
    for (const QString &argument : arguments) encoded.push_back(argument.toUtf8());

    std::vector<char *> argv;
    argv.reserve(encoded.size() + 1);
    for (QByteArray &value : encoded) argv.push_back(value.data());
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
            if (logFd > STDERR_FILENO) close(logFd);
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

QString readLogTail(const QString &path, int maxBytes = 16000)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("No log output was available.");
    if (file.size() > maxBytes) file.seek(file.size() - maxBytes);
    return QString::fromUtf8(file.readAll()).trimmed();
}

QComboBox *boundCombo(EmulatorSettings &settings, const QString &key, const QStringList &items,
                      const QString &fallback, QWidget *parent = nullptr)
{
    auto *combo = new QComboBox(parent);
    combo->addItems(items);
    const QString current = settings.stringValue(key, fallback);
    const int index = combo->findText(current);
    combo->setCurrentIndex(index >= 0 ? index : 0);
    combo->setFocusPolicy(Qt::StrongFocus);
    QObject::connect(combo, &QComboBox::currentTextChanged, combo, [&settings, key](const QString &value) {
        settings.setValue(key, value);
    });
    return combo;
}

QCheckBox *boundToggle(EmulatorSettings &settings, const QString &key, bool fallback,
                       QWidget *parent = nullptr)
{
    auto *check = new QCheckBox(parent);
    check->setChecked(settings.boolValue(key, fallback));
    check->setFocusPolicy(Qt::StrongFocus);
    QObject::connect(check, &QCheckBox::toggled, check, [&settings, key](bool value) {
        settings.setValue(key, value);
    });
    return check;
}

QSpinBox *boundSpin(EmulatorSettings &settings, const QString &key, int fallback,
                    int minimum, int maximum, int step = 1, QWidget *parent = nullptr)
{
    auto *spin = new QSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setValue(settings.intValue(key, fallback));
    spin->setFocusPolicy(Qt::StrongFocus);
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), spin, [&settings, key](int value) {
        settings.setValue(key, value);
    });
    return spin;
}

QLineEdit *boundText(EmulatorSettings &settings, const QString &key, const QString &fallback,
                     int maxLength = 0, QWidget *parent = nullptr)
{
    auto *edit = new QLineEdit(parent);
    edit->setText(settings.stringValue(key, fallback));
    if (maxLength > 0) edit->setMaxLength(maxLength);
    edit->setFocusPolicy(Qt::StrongFocus);
    QObject::connect(edit, &QLineEdit::textChanged, edit, [&settings, key](const QString &value) {
        settings.setValue(key, value);
    });
    return edit;
}

QWidget *pathEditor(EmulatorSettings &settings, const QString &key, const QString &fallback,
                    QWidget *parent = nullptr)
{
    auto *container = new QWidget(parent);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *edit = boundText(settings, key, fallback, 0, container);
    auto *browse = new QPushButton(QStringLiteral("Browse"), container);
    browse->setFocusPolicy(Qt::StrongFocus);
    QObject::connect(browse, &QPushButton::clicked, container, [edit, &settings, key, container] {
        QFileDialog dialog(container, QStringLiteral("Choose folder"),
                           edit->text().isEmpty() ? QStringLiteral("/storage/emulated/0") : edit->text());
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setOption(QFileDialog::ShowDirsOnly, true);
        dialog.setOption(QFileDialog::DontUseNativeDialog, true);
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) {
            const QString path = dialog.selectedFiles().first();
            edit->setText(path);
            settings.setValue(key, path);
        }
    });
    layout->addWidget(edit, 1);
    layout->addWidget(browse);
    return container;
}

QGroupBox *group(const QString &title, QFormLayout **form, QWidget *parent = nullptr)
{
    auto *box = new QGroupBox(title, parent);
    auto *layout = new QFormLayout(box);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->setHorizontalSpacing(18);
    layout->setVerticalSpacing(12);
    *form = layout;
    return box;
}

}

LauncherWindow::LauncherWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_settings(QStringLiteral("evansvl"), QStringLiteral("KytyPS5-DroidDeck"))
    , m_emu(m_settings)
{
    m_emu.ensureDefaults();
    setWindowTitle(QStringLiteral("KytyPS5 DroidDeck"));
    resize(1280, 800);
    buildUi();

    m_gamepad = std::make_unique<EvdevGamepad>(
        [this](PadAction action) { handleGamepad(action); },
        [this](int count) {
            m_controllerLabel->setText(count > 0
                ? QStringLiteral("Controller connected (%1)").arg(count)
                : QStringLiteral("Controller not detected"));
        }, this);

    m_processTimer = new QTimer(this);
    m_processTimer->setInterval(350);
    connect(m_processTimer, &QTimer::timeout, this, [this] { pollChild(); });

    reloadGames();
}

void LauncherWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(18, 14, 18, 14);
    outer->setSpacing(10);

    auto *header = new QHBoxLayout();
    auto *title = new QLabel(QStringLiteral("KytyPS5 DroidDeck"));
    title->setObjectName(QStringLiteral("title"));
    header->addWidget(title);
    header->addStretch(1);
    m_controllerLabel = new QLabel(QStringLiteral("Controller detecting..."));
    m_controllerLabel->setObjectName(QStringLiteral("muted"));
    header->addWidget(m_controllerLabel);
    outer->addLayout(header);

    auto *body = new QHBoxLayout();
    body->setSpacing(14);
    m_sections = new QListWidget();
    m_sections->setObjectName(QStringLiteral("sections"));
    m_sections->setFixedWidth(190);
    m_sections->addItems({QStringLiteral("Library"), QStringLiteral("Graphics"),
                          QStringLiteral("System"), QStringLiteral("Controller"),
                          QStringLiteral("Debug"), QStringLiteral("Expert")});
    m_sections->setCurrentRow(0);
    m_sections->setFocusPolicy(Qt::StrongFocus);
    body->addWidget(m_sections);

    m_stack = new QStackedWidget();
    m_stack->addWidget(buildLibraryPage());
    m_stack->addWidget(buildGraphicsPage());
    m_stack->addWidget(buildSystemPage());
    m_stack->addWidget(buildControllerPage());
    m_stack->addWidget(buildDebugPage());
    m_stack->addWidget(buildExpertPage());
    body->addWidget(m_stack, 1);
    outer->addLayout(body, 1);

    m_statusLabel = new QLabel(QStringLiteral("Ready"));
    m_statusLabel->setObjectName(QStringLiteral("muted"));
    outer->addWidget(m_statusLabel);

    m_hintLabel = new QLabel(QStringLiteral(
        "A Select  ·  B Back  ·  X Next value  ·  Y Add game  ·  LB/RB Sections  ·  D-pad/Stick Navigate"));
    m_hintLabel->setObjectName(QStringLiteral("hint"));
    m_hintLabel->setAlignment(Qt::AlignCenter);
    outer->addWidget(m_hintLabel);

    setCentralWidget(central);

    connect(m_sections, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_stack->count()) {
            m_stack->setCurrentIndex(row);
            QTimer::singleShot(0, this, [this] { focusFirstControl(); });
        }
    });

    setStyleSheet(QStringLiteral(R"(
        QWidget { background:#0d1117; color:#f3f6fa; font-size:16px; }
        QLabel#title { font-size:28px; font-weight:700; }
        QLabel#muted { color:#8d99a8; }
        QLabel#hint { color:#a9b4c2; background:#131922; border-radius:9px; padding:9px; }
        QListWidget#sections { background:#111720; border:1px solid #263140; border-radius:12px; padding:6px; }
        QListWidget#sections::item { padding:14px 12px; border-radius:8px; }
        QListWidget#sections::item:selected { background:#2463eb; color:white; }
        QGroupBox { border:1px solid #263140; border-radius:12px; margin-top:12px; padding:14px; font-weight:700; }
        QGroupBox::title { subcontrol-origin:margin; left:14px; padding:0 6px; }
        QPushButton, QComboBox, QSpinBox, QLineEdit, QPlainTextEdit {
            background:#18202b; border:1px solid #344255; border-radius:9px; padding:9px 12px;
        }
        QPushButton:focus, QComboBox:focus, QSpinBox:focus, QLineEdit:focus, QPlainTextEdit:focus, QCheckBox:focus {
            border:2px solid #69a8ff; background:#203047;
        }
        QPushButton#primary { background:#2463eb; border-color:#3b82f6; font-weight:700; min-height:42px; }
        QWidget#gameCard { background:#141b24; border:1px solid #263140; border-radius:14px; }
        QScrollArea { border:none; }
    )"));
}

QWidget *LauncherWindow::settingsPage(const QString &title, const QString &description, QVBoxLayout **content)
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    auto *heading = new QLabel(title);
    heading->setStyleSheet(QStringLiteral("font-size:24px;font-weight:700;"));
    layout->addWidget(heading);
    auto *desc = new QLabel(description);
    desc->setWordWrap(true);
    desc->setObjectName(QStringLiteral("muted"));
    layout->addWidget(desc);

    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    auto *body = new QWidget();
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(2, 8, 8, 8);
    bodyLayout->setSpacing(14);
    scroll->setWidget(body);
    layout->addWidget(scroll, 1);
    *content = bodyLayout;
    return page;
}

QWidget *LauncherWindow::buildLibraryPage()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *top = new QHBoxLayout();
    auto *heading = new QLabel(QStringLiteral("Game Library"));
    heading->setStyleSheet(QStringLiteral("font-size:24px;font-weight:700;"));
    top->addWidget(heading);
    top->addStretch(1);
    auto *add = new QPushButton(QStringLiteral("Add Game"));
    add->setObjectName(QStringLiteral("primary"));
    add->setFocusPolicy(Qt::StrongFocus);
    auto *refresh = new QPushButton(QStringLiteral("Refresh"));
    refresh->setFocusPolicy(Qt::StrongFocus);
    top->addWidget(add);
    top->addWidget(refresh);
    layout->addLayout(top);

    auto *path = new QLabel(QStringLiteral("Default library: %1").arg(gamesRoot()));
    path->setObjectName(QStringLiteral("muted"));
    layout->addWidget(path);

    m_libraryScroll = new QScrollArea();
    m_libraryScroll->setWidgetResizable(true);
    layout->addWidget(m_libraryScroll, 1);

    connect(add, &QPushButton::clicked, this, [this] { addGame(); });
    connect(refresh, &QPushButton::clicked, this, [this] { reloadGames(); });
    return page;
}

QWidget *LauncherWindow::buildGraphicsPage()
{
    QVBoxLayout *content = nullptr;
    QWidget *page = settingsPage(QStringLiteral("Graphics"),
        QStringLiteral("Rendering and compatibility settings passed directly to kyty_emulator."), &content);

    QFormLayout *display = nullptr;
    auto *displayBox = group(QStringLiteral("Display"), &display);
    display->addRow(QStringLiteral("Resolution"), boundCombo(m_emu, QStringLiteral("resolution"), EmulatorSettings::resolutions(), QStringLiteral("1280x720")));
    display->addRow(QStringLiteral("Present mode"), boundCombo(m_emu, QStringLiteral("present_mode"), EmulatorSettings::presentModes(), QStringLiteral("Mailbox")));
    display->addRow(QStringLiteral("GPU index (-1 = auto)"), boundSpin(m_emu, QStringLiteral("gpu_index"), -1, -1, 16));
    display->addRow(QStringLiteral("Vblank frequency"), boundSpin(m_emu, QStringLiteral("vblank_frequency"), 60, 30, 360));
    display->addRow(QStringLiteral("Fullscreen"), boundToggle(m_emu, QStringLiteral("fullscreen"), true));
    display->addRow(QStringLiteral("HDR"), boundToggle(m_emu, QStringLiteral("hdr"), false));
    display->addRow(QStringLiteral("Hide cursor"), boundToggle(m_emu, QStringLiteral("hide_cursor"), false));
    display->addRow(QStringLiteral("Performance overlay"), boundToggle(m_emu, QStringLiteral("perf_overlay"), false));
    content->addWidget(displayBox);

    QFormLayout *compat = nullptr;
    auto *compatBox = group(QStringLiteral("Compatibility"), &compat);
    compat->addRow(QStringLiteral("Linear image readback"), boundToggle(m_emu, QStringLiteral("readback_linear"), false));
    compat->addRow(QStringLiteral("Tiled image readback"), boundToggle(m_emu, QStringLiteral("readback_tiled"), false));
    compat->addRow(QStringLiteral("Sync raw image buffers"), boundToggle(m_emu, QStringLiteral("sync_raw_images"), false));
    compat->addRow(QStringLiteral("Tessellation"), boundToggle(m_emu, QStringLiteral("tessellation"), false));
    compat->addRow(QStringLiteral("Occlusion queries"), boundToggle(m_emu, QStringLiteral("occlusion_queries"), false));
    content->addWidget(compatBox);
    content->addStretch(1);
    return page;
}

QWidget *LauncherWindow::buildSystemPage()
{
    QVBoxLayout *content = nullptr;
    QWidget *page = settingsPage(QStringLiteral("System"),
        QStringLiteral("Virtual console identity, audio, network, trophies and content paths."), &content);

    QFormLayout *user = nullptr;
    auto *userBox = group(QStringLiteral("User / Console"), &user);
    user->addRow(QStringLiteral("User name"), boundText(m_emu, QStringLiteral("user_name"), QStringLiteral("Kyty"), 16));
    user->addRow(QStringLiteral("User ID"), boundSpin(m_emu, QStringLiteral("user_id"), 1000, 1, 2147483000));
    user->addRow(QStringLiteral("Console language (0-29)"), boundSpin(m_emu, QStringLiteral("console_language"), 1, 0, 29));
    user->addRow(QStringLiteral("Microphone device"), boundText(m_emu, QStringLiteral("mic"), QString()));
    user->addRow(QStringLiteral("Network connected"), boundToggle(m_emu, QStringLiteral("network_connected"), false));
    content->addWidget(userBox);

    QFormLayout *notifications = nullptr;
    auto *notificationsBox = group(QStringLiteral("Notifications"), &notifications);
    notifications->addRow(QStringLiteral("Trophy notifications"), boundToggle(m_emu, QStringLiteral("trophies"), true));
    notifications->addRow(QStringLiteral("Trophy duration"), boundSpin(m_emu, QStringLiteral("trophy_duration"), 6, 5, 10));
    notifications->addRow(QStringLiteral("Skip notice screen"), boundToggle(m_emu, QStringLiteral("skip_notice"), false));
    content->addWidget(notificationsBox);

    QFormLayout *paths = nullptr;
    auto *pathsBox = group(QStringLiteral("Content"), &paths);
    paths->addRow(QStringLiteral("Add-ons / DLC folder"), pathEditor(m_emu, QStringLiteral("addons_dir"), QString()));
    content->addWidget(pathsBox);
    content->addStretch(1);
    return page;
}

QWidget *LauncherWindow::buildControllerPage()
{
    QVBoxLayout *content = nullptr;
    QWidget *page = settingsPage(QStringLiteral("Controller"),
        QStringLiteral("DualSense emulation and host input mapping. One keymap per line: Control=Input."), &content);

    QFormLayout *pad = nullptr;
    auto *padBox = group(QStringLiteral("DualSense"), &pad);
    pad->addRow(QStringLiteral("Lightbar color (#RRGGBB)"), boundText(m_emu, QStringLiteral("controller_color"), QString()));
    pad->addRow(QStringLiteral("Speaker volume"), boundSpin(m_emu, QStringLiteral("controller_volume"), 50, 0, 100));
    pad->addRow(QStringLiteral("Vibration intensity"), boundSpin(m_emu, QStringLiteral("controller_vibration"), 100, 0, 100));
    pad->addRow(QStringLiteral("Keyboard player"), boundSpin(m_emu, QStringLiteral("keyboard_player"), 0, 0, 4));
    content->addWidget(padBox);

    auto *mappingBox = new QGroupBox(QStringLiteral("Input mapping"));
    auto *mappingLayout = new QVBoxLayout(mappingBox);
    auto *mapping = new QPlainTextEdit();
    mapping->setPlaceholderText(QStringLiteral("Cross=KeySpace\nOptions=KeyEnter"));
    mapping->setPlainText(m_emu.stringListValue(QStringLiteral("keymap")).join(QLatin1Char('\n')));
    mapping->setMinimumHeight(180);
    mapping->setFocusPolicy(Qt::StrongFocus);
    connect(mapping, &QPlainTextEdit::textChanged, mapping, [this, mapping] {
        const QStringList lines = mapping->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        m_emu.setValue(QStringLiteral("keymap"), lines);
    });
    mappingLayout->addWidget(mapping);
    content->addWidget(mappingBox);
    content->addStretch(1);
    return page;
}

QWidget *LauncherWindow::buildDebugPage()
{
    QVBoxLayout *content = nullptr;
    QWidget *page = settingsPage(QStringLiteral("Debug"),
        QStringLiteral("Kyty shader, validation, dump and logging options. These can heavily reduce performance."), &content);

    QFormLayout *validation = nullptr;
    auto *validationBox = group(QStringLiteral("Validation"), &validation);
    validation->addRow(QStringLiteral("Vulkan validation"), boundToggle(m_emu, QStringLiteral("vulkan_validation"), false));
    validation->addRow(QStringLiteral("Shader validation"), boundToggle(m_emu, QStringLiteral("shader_validation"), false));
    validation->addRow(QStringLiteral("Shader optimization"), boundCombo(m_emu, QStringLiteral("shader_optimization"), EmulatorSettings::shaderOptimizationModes(), QStringLiteral("Performance")));
    content->addWidget(validationBox);

    QFormLayout *logging = nullptr;
    auto *loggingBox = group(QStringLiteral("Logging / Dumps"), &logging);
    logging->addRow(QStringLiteral("Shader log"), boundCombo(m_emu, QStringLiteral("shader_log_direction"), EmulatorSettings::logDirections(), QStringLiteral("Silent")));
    logging->addRow(QStringLiteral("Shader log folder"), boundText(m_emu, QStringLiteral("shader_log_folder"), QStringLiteral("_Shaders")));
    logging->addRow(QStringLiteral("Command buffer dump"), boundToggle(m_emu, QStringLiteral("command_buffer_dump"), false));
    logging->addRow(QStringLiteral("Command buffer folder"), boundText(m_emu, QStringLiteral("command_buffer_dump_folder"), QStringLiteral("_Buffers")));
    logging->addRow(QStringLiteral("Guest printf"), boundCombo(m_emu, QStringLiteral("printf_direction"), EmulatorSettings::logDirections(), QStringLiteral("Silent")));
    logging->addRow(QStringLiteral("Guest printf file"), boundText(m_emu, QStringLiteral("printf_output_file"), QStringLiteral("_kyty.txt")));
    content->addWidget(loggingBox);

    QFormLayout *tools = nullptr;
    auto *toolsBox = group(QStringLiteral("Tools"), &tools);
    tools->addRow(QStringLiteral("Profiler (Tracy)"), boundToggle(m_emu, QStringLiteral("profiler"), false));
    tools->addRow(QStringLiteral("RenderDoc"), boundToggle(m_emu, QStringLiteral("renderdoc"), false));
    tools->addRow(QStringLiteral("AMD CPU patch"), boundToggle(m_emu, QStringLiteral("amd_cpu"), false));
    content->addWidget(toolsBox);
    content->addStretch(1);
    return page;
}

QWidget *LauncherWindow::buildExpertPage()
{
    QVBoxLayout *content = nullptr;
    QWidget *page = settingsPage(QStringLiteral("Expert"),
        QStringLiteral("Additional kyty_emulator CLI features. Leave these disabled unless a game or debugging workflow needs them."), &content);

    QFormLayout *expert = nullptr;
    auto *expertBox = group(QStringLiteral("Emulator flags"), &expert);
    expert->addRow(QStringLiteral("Disable startup splash"), boundToggle(m_emu, QStringLiteral("no_splash"), false));
    expert->addRow(QStringLiteral("Virtual VR headset"), boundToggle(m_emu, QStringLiteral("vr"), false));
    expert->addRow(QStringLiteral("GPU assisted validation"), boundToggle(m_emu, QStringLiteral("gpu_assisted_validation"), false));
    expert->addRow(QStringLiteral("SPIR-V debug printf"), boundToggle(m_emu, QStringLiteral("spirv_debug_printf"), false));
    expert->addRow(QStringLiteral("Graphics debug dump"), boundToggle(m_emu, QStringLiteral("graphics_debug_dump"), false));
    expert->addRow(QStringLiteral("PlayGo hack"), boundToggle(m_emu, QStringLiteral("playgo_hack"), false));
    content->addWidget(expertBox);

    auto *reset = new QPushButton(QStringLiteral("Reset emulator settings to defaults"));
    reset->setFocusPolicy(Qt::StrongFocus);
    connect(reset, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("Reset settings"),
                                  QStringLiteral("Reset all emulator settings?")) != QMessageBox::Yes) return;
        m_settings.beginGroup(QStringLiteral("emu"));
        m_settings.remove(QString());
        m_settings.endGroup();
        m_emu.ensureDefaults();
        QMessageBox::information(this, QStringLiteral("Settings"),
                                 QStringLiteral("Defaults restored. Reopen the launcher to refresh every control."));
    });
    content->addWidget(reset);
    content->addStretch(1);
    return page;
}

void LauncherWindow::reloadGames()
{
    auto *content = new QWidget();
    auto *grid = new QGridLayout(content);
    grid->setContentsMargins(0, 8, 0, 0);
    grid->setSpacing(14);
    m_firstPlay = nullptr;

    const QList<GameEntry> games = loadGameLibrary(m_settings);
    if (games.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral(
            "No games found.\n\nPress Y or Add Game and select a game folder, eboot.bin, ELF, .zar or .zip archive."));
        empty->setAlignment(Qt::AlignCenter);
        empty->setMinimumHeight(380);
        grid->addWidget(empty, 0, 0);
        m_statusLabel->setText(QStringLiteral("No games found"));
    } else {
        constexpr int columns = 3;
        for (int i = 0; i < games.size(); ++i)
            grid->addWidget(createGameCard(games.at(i)), i / columns, i % columns);
        grid->setRowStretch((games.size() + columns - 1) / columns, 1);
        m_statusLabel->setText(QStringLiteral("%1 game(s) in library").arg(games.size()));
    }

    QWidget *old = m_libraryScroll->takeWidget();
    m_libraryScroll->setWidget(content);
    if (old) old->deleteLater();

    if (m_firstPlay && m_stack->currentIndex() == 0)
        QTimer::singleShot(0, this, [this] { if (m_firstPlay) m_firstPlay->setFocus(); });
}

QWidget *LauncherWindow::createGameCard(const GameEntry &game)
{
    auto *card = new QWidget();
    card->setObjectName(QStringLiteral("gameCard"));
    card->setMinimumSize(280, 300);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 12, 12, 12);

    auto *cover = new QLabel();
    cover->setAlignment(Qt::AlignCenter);
    cover->setMinimumHeight(170);
    cover->setStyleSheet(QStringLiteral("background:#090d12;border-radius:9px;"));
    if (!game.icon.isEmpty()) {
        QPixmap pixmap(game.icon);
        if (!pixmap.isNull()) cover->setPixmap(pixmap.scaled(250, 170, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    if (!cover->pixmap()) {
        cover->setText(QStringLiteral("PS5"));
        cover->setStyleSheet(QStringLiteral("background:#090d12;border-radius:9px;font-size:38px;font-weight:700;color:#4f5e70;"));
    }

    auto *title = new QLabel(game.title);
    title->setWordWrap(true);
    title->setStyleSheet(QStringLiteral("font-size:19px;font-weight:700;"));
    auto *path = new QLabel(game.target);
    path->setWordWrap(true);
    path->setObjectName(QStringLiteral("muted"));
    path->setStyleSheet(QStringLiteral("color:#8190a2;font-size:11px;"));

    auto *buttons = new QHBoxLayout();
    auto *play = new QPushButton(QStringLiteral("Play"));
    play->setObjectName(QStringLiteral("primary"));
    play->setFocusPolicy(Qt::StrongFocus);
    auto *settings = new QPushButton(QStringLiteral("Settings"));
    settings->setFocusPolicy(Qt::StrongFocus);
    connect(play, &QPushButton::clicked, this, [this, game] { launchGame(game); });
    connect(settings, &QPushButton::clicked, this, [this] { m_sections->setCurrentRow(1); });
    if (!m_firstPlay) m_firstPlay = play;
    buttons->addWidget(play, 1);
    buttons->addWidget(settings);

    layout->addWidget(cover);
    layout->addWidget(title);
    layout->addWidget(path);
    layout->addStretch(1);
    layout->addLayout(buttons);
    return card;
}

void LauncherWindow::addGame()
{
    QMessageBox choose(this);
    choose.setWindowTitle(QStringLiteral("Add Game"));
    choose.setText(QStringLiteral("Choose what you want to add."));
    auto *folderButton = choose.addButton(QStringLiteral("Game folder"), QMessageBox::AcceptRole);
    auto *fileButton = choose.addButton(QStringLiteral("File / archive"), QMessageBox::AcceptRole);
    choose.addButton(QMessageBox::Cancel);
    choose.exec();

    if (choose.clickedButton() == folderButton) {
        QFileDialog dialog(this, QStringLiteral("Add PS5 game folder"), pickerStart(m_settings));
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setOption(QFileDialog::ShowDirsOnly, true);
        dialog.setOption(QFileDialog::DontUseNativeDialog, true);
        if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
        QString error;
        if (!addCustomSource(m_settings, dialog.selectedFiles().first(), &error)) {
            QMessageBox::warning(this, QStringLiteral("Add Game"), error);
            return;
        }
        reloadGames();
        return;
    }

    if (choose.clickedButton() == fileButton) {
        QFileDialog dialog(this, QStringLiteral("Add PS5 game"), pickerStart(m_settings));
        dialog.setFileMode(QFileDialog::ExistingFile);
        dialog.setNameFilters({QStringLiteral("PS5 games (*.zip *.zar *.elf *.bin)"), QStringLiteral("All files (*)")});
        dialog.setOption(QFileDialog::DontUseNativeDialog, true);
        if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
        const QString file = dialog.selectedFiles().first();
        m_settings.setValue(QStringLiteral("lastPickerDir"), QFileInfo(file).absolutePath());
        QString error;
        if (QFileInfo(file).suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0) {
            m_statusLabel->setText(QStringLiteral("Extracting %1...").arg(QFileInfo(file).fileName()));
            QApplication::processEvents();
            if (!importZipGame(file, &error)) {
                QMessageBox::warning(this, QStringLiteral("ZIP import failed"), error);
                return;
            }
        } else if (!addCustomSource(m_settings, file, &error)) {
            QMessageBox::warning(this, QStringLiteral("Add Game"), error);
            return;
        }
        reloadGames();
    }
}

void LauncherWindow::launchGame(const GameEntry &game)
{
    const QString emulator = QDir(kytyDir()).filePath(QStringLiteral("kyty_emulator"));
    if (!QFileInfo::exists(emulator)) {
        QMessageBox::critical(this, QStringLiteral("KytyPS5"), QStringLiteral("kyty_emulator not found:\n%1").arg(emulator));
        return;
    }
    startTrackedProcess(emulator, m_emu.buildArgs(game.target), QStringLiteral("game"));
}

void LauncherWindow::startTrackedProcess(const QString &program, const QStringList &args, const QString &kind)
{
    if (m_childPid > 0) {
        QMessageBox::information(this, QStringLiteral("KytyPS5"), QStringLiteral("The emulator is already running."));
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString logFile = QDir(logsRoot()).filePath(QStringLiteral("%1-%2.log").arg(kind, stamp));
    const SpawnResult result = spawnWithLog(program, args, logFile);
    if (!result.ok) {
        QMessageBox::critical(this, QStringLiteral("Launch failed"), result.error + QStringLiteral("\n\nLog: ") + logFile);
        return;
    }

    m_childPid = result.pid;
    m_childLog = result.logFile;
    m_childKind = kind;
    m_childTicks = 0;
    m_statusLabel->setText(QStringLiteral("Starting emulator · PID %1").arg(result.pid));
    m_processTimer->start();
    hide();
}

void LauncherWindow::pollChild()
{
    if (m_childPid <= 0) {
        m_processTimer->stop();
        return;
    }

    ++m_childTicks;
    int status = 0;
    const pid_t result = waitpid(m_childPid, &status, WNOHANG);
    if (result == 0) return;

    m_processTimer->stop();
    m_childPid = -1;
    show();
    raise();
    activateWindow();

    if (result < 0) {
        m_statusLabel->setText(QStringLiteral("Unable to track emulator process"));
        return;
    }

    QString summary;
    if (WIFEXITED(status)) summary = QStringLiteral("Process exited with code %1").arg(WEXITSTATUS(status));
    else if (WIFSIGNALED(status)) summary = QStringLiteral("Process terminated by signal %1").arg(WTERMSIG(status));
    else summary = QStringLiteral("Process ended");

    const QString output = readLogTail(m_childLog);
    m_statusLabel->setText(summary + QStringLiteral(" · ") + m_childLog);

    const bool failed = !WIFEXITED(status) || WEXITSTATUS(status) != 0 || m_childTicks < 10;
    if (failed) {
        QMessageBox box(QMessageBox::Critical, QStringLiteral("Kyty process ended"), summary, QMessageBox::Ok, this);
        if (output.contains(QStringLiteral("could not reserve"), Qt::CaseInsensitive) &&
            output.contains(QStringLiteral("guest direct memory"), Qt::CaseInsensitive)) {
            box.setInformativeText(QStringLiteral(
                "Kyty could not reserve its PS5 guest direct-memory backing under DroidDeck/FEX. "
                "This is a core/runtime memory-mapping problem, not a bad game path or launcher setting."));
        } else {
            box.setInformativeText(QStringLiteral("The launcher captured the emulator output. Open Details below."));
        }
        box.setDetailedText(output.isEmpty() ? QStringLiteral("No output was written.\nLog: %1").arg(m_childLog) : output);
        box.exec();
    }
}

void LauncherWindow::switchSection(int delta)
{
    int row = m_sections->currentRow();
    if (row < 0) row = 0;
    row = (row + delta + m_sections->count()) % m_sections->count();
    m_sections->setCurrentRow(row);
}

void LauncherWindow::focusFirstControl()
{
    QWidget *page = m_stack->currentWidget();
    if (!page) return;
    QWidget *candidate = page;
    for (int i = 0; i < 256; ++i) {
        candidate = candidate->nextInFocusChain();
        if (!candidate || candidate == page) break;
        if (candidate->isVisible() && candidate->isEnabled() && candidate->focusPolicy() != Qt::NoFocus) {
            candidate->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
}

void LauncherWindow::moveFocus(bool forward)
{
    QWidget *focused = QApplication::focusWidget();
    if (!focused) {
        focusFirstControl();
        return;
    }
    QWidget *candidate = focused;
    for (int i = 0; i < 256; ++i) {
        candidate = forward ? candidate->nextInFocusChain() : candidate->previousInFocusChain();
        if (!candidate || candidate == focused) break;
        if (candidate->isVisible() && candidate->isEnabled() && candidate->focusPolicy() != Qt::NoFocus) {
            candidate->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
}

void LauncherWindow::cycleFocusedValue()
{
    QWidget *focused = QApplication::focusWidget();
    if (!focused) return;
    if (auto *combo = qobject_cast<QComboBox *>(focused)) {
        combo->setCurrentIndex((combo->currentIndex() + 1) % std::max(1, combo->count()));
    } else if (auto *spin = qobject_cast<QSpinBox *>(focused)) {
        spin->setValue(spin->value() >= spin->maximum() ? spin->minimum() : std::min(spin->maximum(), spin->value() + spin->singleStep()));
    } else if (auto *check = qobject_cast<QCheckBox *>(focused)) {
        check->toggle();
    } else if (auto *button = qobject_cast<QPushButton *>(focused)) {
        button->click();
    } else if (auto *edit = qobject_cast<QLineEdit *>(focused)) {
        edit->selectAll();
        QApplication::inputMethod()->show();
    }
}

void LauncherWindow::adjustFocusedValue(int delta)
{
    QWidget *focused = QApplication::focusWidget();
    if (auto *combo = qobject_cast<QComboBox *>(focused)) {
        if (combo->count() == 0) return;
        combo->setCurrentIndex((combo->currentIndex() + delta + combo->count()) % combo->count());
        return;
    }
    if (auto *spin = qobject_cast<QSpinBox *>(focused)) {
        spin->setValue(std::clamp(spin->value() + delta * spin->singleStep(), spin->minimum(), spin->maximum()));
        return;
    }
    if (auto *check = qobject_cast<QCheckBox *>(focused)) {
        check->setChecked(delta > 0);
        return;
    }
    moveFocus(delta > 0);
}

void LauncherWindow::handleGamepad(PadAction action)
{
    if (m_childPid > 0 || !isVisible()) return;

    if (action == PadAction::PrevSection) {
        switchSection(-1);
        return;
    }
    if (action == PadAction::NextSection) {
        switchSection(1);
        return;
    }
    if (action == PadAction::AddGame) {
        addGame();
        return;
    }
    if (action == PadAction::Refresh) {
        reloadGames();
        return;
    }
    if (action == PadAction::Back) {
        if (QWidget *modal = QApplication::activeModalWidget()) {
            modal->close();
            return;
        }
        if (m_sections->currentRow() != 0) m_sections->setCurrentRow(0);
        else if (m_firstPlay) m_firstPlay->setFocus();
        return;
    }
    if (action == PadAction::NextValue) {
        cycleFocusedValue();
        return;
    }
    if (action == PadAction::Up) {
        moveFocus(false);
        return;
    }
    if (action == PadAction::Down) {
        moveFocus(true);
        return;
    }
    if (action == PadAction::Left) {
        adjustFocusedValue(-1);
        return;
    }
    if (action == PadAction::Right) {
        adjustFocusedValue(1);
        return;
    }
    if (action == PadAction::Confirm) {
        QWidget *focused = QApplication::focusWidget();
        if (auto *button = qobject_cast<QPushButton *>(focused)) button->click();
        else if (auto *check = qobject_cast<QCheckBox *>(focused)) check->toggle();
        else if (auto *combo = qobject_cast<QComboBox *>(focused)) combo->setCurrentIndex((combo->currentIndex() + 1) % std::max(1, combo->count()));
        else if (auto *edit = qobject_cast<QLineEdit *>(focused)) {
            edit->selectAll();
            QApplication::inputMethod()->show();
        } else if (!focused && m_firstPlay) m_firstPlay->click();
    }
}

void LauncherWindow::addHeaderHint(const QString &text)
{
    if (m_hintLabel) m_hintLabel->setText(text);
}
