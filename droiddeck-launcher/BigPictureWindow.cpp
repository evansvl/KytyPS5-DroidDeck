#include "BigPictureWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputMethod>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
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
        result.error = QStringLiteral("Could not verify Kyty process: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return result;
    }

    result.ok = true;
    result.pid = pid;
    return result;
}

QString readLogTail(const QString &path, int maxBytes = 32000)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("No log output was available.");
    if (file.size() > maxBytes) file.seek(file.size() - maxBytes);
    return QString::fromUtf8(file.readAll()).trimmed();
}

QLabel *label(const QString &text, const QString &name = {})
{
    auto *out = new QLabel(text);
    if (!name.isEmpty()) out->setObjectName(name);
    return out;
}

QString richTabs(const QStringList &tabs, int active)
{
    QStringList rendered;
    for (int i = 0; i < tabs.size(); ++i) {
        const QString color = i == active ? QStringLiteral("#ffffff") : QStringLiteral("#6f829c");
        const QString weight = i == active ? QStringLiteral("800") : QStringLiteral("600");
        rendered << QStringLiteral("<span style='color:%1;font-weight:%2'>%3</span>")
                        .arg(color, weight, tabs.at(i).toHtmlEscaped());
    }
    return rendered.join(QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;"));
}

bool supportedDirectFile(const QFileInfo &info)
{
    const QString suffix = info.suffix().toLower();
    return suffix == QStringLiteral("zar") || suffix == QStringLiteral("elf") ||
           info.fileName().compare(QStringLiteral("eboot.bin"), Qt::CaseInsensitive) == 0;
}

} // namespace

BigPictureWindow::BigPictureWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_store(QStringLiteral("evansvl"), QStringLiteral("KytyPS5-DroidDeck"))
    , m_emu(m_store)
{
    m_emu.ensureDefaults();
    buildSettingSpecs();

    setWindowTitle(QStringLiteral("KytyPS5 Big Picture"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_pages = new QStackedWidget(this);
    m_libraryPage = buildLibraryPage();
    m_settingsPage = buildSettingsPage();
    m_browserPage = buildBrowserPage();
    m_errorPage = buildErrorPage();
    m_editorPage = buildEditorPage();
    m_pages->addWidget(m_libraryPage);
    m_pages->addWidget(m_settingsPage);
    m_pages->addWidget(m_browserPage);
    m_pages->addWidget(m_errorPage);
    m_pages->addWidget(m_editorPage);
    setCentralWidget(m_pages);

    m_processTimer = new QTimer(this);
    m_processTimer->setInterval(300);
    connect(m_processTimer, &QTimer::timeout, this, [this] { pollChild(); });

    m_gamepad = std::make_unique<EvdevGamepad>(
        [this](PadAction action) { handleGamepad(action); },
        [this](int count) {
            if (!m_padStatus) return;
            m_padStatus->setText(count > 0 ? QStringLiteral("CONTROLLER CONNECTED") : QStringLiteral("CONTROLLER NOT FOUND"));
            m_padStatus->setProperty("connected", count > 0);
            style()->unpolish(m_padStatus);
            style()->polish(m_padStatus);
        }, this);

    reloadGames();
    switchView(View::Library, m_libraryPage);
    applyResponsiveMetrics();
}

QVBoxLayout *BigPictureWindow::makeSafeLayout(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setSpacing(10);
    m_safeLayouts.push_back(layout);
    return layout;
}

QWidget *BigPictureWindow::makeBrandHeader(const QString &active)
{
    auto *header = new QWidget();
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *mark = label(QStringLiteral("K5"), QStringLiteral("brandMark"));
    mark->setAlignment(Qt::AlignCenter);
    layout->addWidget(mark);

    auto *titles = new QVBoxLayout();
    titles->setSpacing(0);
    titles->addWidget(label(QStringLiteral("KytyPS5"), QStringLiteral("brandTitle")));
    titles->addWidget(label(QStringLiteral("BIG PICTURE · PS5 CORE ON DROIDDECK"), QStringLiteral("brandSubtitle")));
    layout->addLayout(titles);
    layout->addStretch(1);

    auto *activeLabel = label(active, QStringLiteral("activePage"));
    activeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(activeLabel);

    if (active == QStringLiteral("LIBRARY")) {
        m_padStatus = label(QStringLiteral("CONTROLLER DETECTING"), QStringLiteral("padStatus"));
        m_padStatus->setProperty("connected", false);
        layout->addWidget(m_padStatus);
    }
    return header;
}

QWidget *BigPictureWindow::buildLibraryPage()
{
    auto *page = new QWidget();
    page->setObjectName(QStringLiteral("libraryPage"));
    auto *outer = makeSafeLayout(page);
    outer->addWidget(makeBrandHeader(QStringLiteral("LIBRARY")));

    auto *hero = new QFrame();
    hero->setObjectName(QStringLiteral("hero"));
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(22, 18, 22, 18);
    heroLayout->setSpacing(24);

    auto *heroText = new QVBoxLayout();
    heroText->setSpacing(5);
    m_heroMeta = label(QStringLiteral("SELECT A GAME"), QStringLiteral("heroMeta"));
    m_heroTitle = label(QStringLiteral("KytyPS5"), QStringLiteral("heroTitle"));
    m_heroTitle->setWordWrap(true);
    m_heroPath = label(QStringLiteral("D-pad / left stick to browse · A to launch"), QStringLiteral("heroPath"));
    m_heroPath->setWordWrap(true);
    heroText->addWidget(m_heroMeta);
    heroText->addWidget(m_heroTitle);
    heroText->addWidget(m_heroPath);
    heroText->addStretch(1);
    auto *launchHint = label(QStringLiteral("A  LAUNCH     X  SETTINGS     Y  ADD GAME"), QStringLiteral("heroAction"));
    heroText->addWidget(launchHint);
    heroLayout->addLayout(heroText, 1);

    m_heroArt = label(QStringLiteral("PS5"), QStringLiteral("heroArt"));
    m_heroArt->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(m_heroArt);
    outer->addWidget(hero, 1);

    auto *rowTitle = new QHBoxLayout();
    rowTitle->addWidget(label(QStringLiteral("RECENT / LIBRARY"), QStringLiteral("sectionKicker")));
    rowTitle->addStretch(1);
    m_libraryCounter = label(QString(), QStringLiteral("libraryCounter"));
    rowTitle->addWidget(m_libraryCounter);
    outer->addLayout(rowTitle);

    m_libraryScroll = new QScrollArea();
    m_libraryScroll->setWidgetResizable(false);
    m_libraryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_libraryScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_libraryScroll->setFrameShape(QFrame::NoFrame);
    m_libraryContainer = new QWidget();
    m_libraryStrip = new QHBoxLayout(m_libraryContainer);
    m_libraryStrip->setContentsMargins(2, 2, 2, 2);
    m_libraryStrip->setSpacing(12);
    m_libraryScroll->setWidget(m_libraryContainer);
    outer->addWidget(m_libraryScroll);

    m_libraryHint = label(QStringLiteral("◄ ►  SELECT      A  PLAY      X  SETTINGS      Y  ADD GAME      VIEW  REFRESH"), QStringLiteral("bottomHint"));
    m_libraryHint->setAlignment(Qt::AlignCenter);
    outer->addWidget(m_libraryHint);
    return page;
}

void BigPictureWindow::buildSettingSpecs()
{
    using K = SettingKind;
    const auto toggle = [](QString l, QString d, QString k) { return SettingSpec{l, d, k, K::Toggle, {}, 0, 0, 1}; };
    const auto choice = [](QString l, QString d, QString k, QStringList c) { return SettingSpec{l, d, k, K::Choice, c, 0, 0, 1}; };
    const auto integer = [](QString l, QString d, QString k, int min, int max, int step = 1) { return SettingSpec{l, d, k, K::Integer, {}, min, max, step}; };
    const auto text = [](QString l, QString d, QString k) { return SettingSpec{l, d, k, K::Text, {}, 0, 0, 1}; };
    const auto textList = [](QString l, QString d, QString k) { return SettingSpec{l, d, k, K::TextList, {}, 0, 0, 1}; };

    m_settingSections = {
        {
            choice(QStringLiteral("Resolution"), QStringLiteral("Guest render/output size"), QStringLiteral("resolution"), EmulatorSettings::resolutions()),
            choice(QStringLiteral("Present mode"), QStringLiteral("Vulkan presentation strategy"), QStringLiteral("present_mode"), EmulatorSettings::presentModes()),
            integer(QStringLiteral("GPU index"), QStringLiteral("-1 uses the default device"), QStringLiteral("gpu_index"), -1, 16),
            integer(QStringLiteral("VBlank frequency"), QStringLiteral("Virtual display refresh"), QStringLiteral("vblank_frequency"), 30, 360),
            toggle(QStringLiteral("Fullscreen game"), QStringLiteral("Launch guest output fullscreen"), QStringLiteral("fullscreen")),
            toggle(QStringLiteral("HDR"), QStringLiteral("Enable HDR output"), QStringLiteral("hdr")),
            toggle(QStringLiteral("Hide cursor"), QStringLiteral("Hide host cursor over guest"), QStringLiteral("hide_cursor")),
            toggle(QStringLiteral("Performance overlay"), QStringLiteral("Show Kyty performance statistics"), QStringLiteral("perf_overlay")),
            toggle(QStringLiteral("Linear image readback"), QStringLiteral("Compatibility option"), QStringLiteral("readback_linear")),
            toggle(QStringLiteral("Tiled image readback"), QStringLiteral("Compatibility option"), QStringLiteral("readback_tiled")),
            toggle(QStringLiteral("Sync raw image buffers"), QStringLiteral("Compatibility option"), QStringLiteral("sync_raw_images")),
            toggle(QStringLiteral("Tessellation"), QStringLiteral("Enable tessellation support"), QStringLiteral("tessellation")),
            toggle(QStringLiteral("Occlusion queries"), QStringLiteral("Enable guest occlusion queries"), QStringLiteral("occlusion_queries")),
        },
        {
            text(QStringLiteral("User name"), QStringLiteral("Virtual console user"), QStringLiteral("user_name")),
            integer(QStringLiteral("User ID"), QStringLiteral("Virtual console user ID"), QStringLiteral("user_id"), 1, 2147483000),
            integer(QStringLiteral("Console language"), QStringLiteral("Kyty language index 0–29"), QStringLiteral("console_language"), 0, 29),
            text(QStringLiteral("Microphone device"), QStringLiteral("Optional host microphone name"), QStringLiteral("mic")),
            toggle(QStringLiteral("Network connected"), QStringLiteral("Expose connected network state"), QStringLiteral("network_connected")),
            toggle(QStringLiteral("Trophy notifications"), QStringLiteral("Show trophy notifications"), QStringLiteral("trophies")),
            integer(QStringLiteral("Trophy duration"), QStringLiteral("Notification duration in seconds"), QStringLiteral("trophy_duration"), 5, 10),
            toggle(QStringLiteral("Skip notice screen"), QStringLiteral("Skip Kyty startup notice"), QStringLiteral("skip_notice")),
            text(QStringLiteral("Add-ons / DLC folder"), QStringLiteral("Optional addcont directory"), QStringLiteral("addons_dir")),
        },
        {
            text(QStringLiteral("Lightbar color"), QStringLiteral("Optional #RRGGBB"), QStringLiteral("controller_color")),
            integer(QStringLiteral("Speaker volume"), QStringLiteral("Virtual DualSense speaker"), QStringLiteral("controller_volume"), 0, 100, 5),
            integer(QStringLiteral("Vibration intensity"), QStringLiteral("Virtual DualSense vibration"), QStringLiteral("controller_vibration"), 0, 100, 5),
            integer(QStringLiteral("Keyboard player"), QStringLiteral("Keyboard mapped player 0–4"), QStringLiteral("keyboard_player"), 0, 4),
            textList(QStringLiteral("Key map"), QStringLiteral("Semicolon-separated Control=Input mappings"), QStringLiteral("keymap")),
        },
        {
            toggle(QStringLiteral("Vulkan validation"), QStringLiteral("Heavy debug validation"), QStringLiteral("vulkan_validation")),
            toggle(QStringLiteral("Shader validation"), QStringLiteral("Validate translated shaders"), QStringLiteral("shader_validation")),
            choice(QStringLiteral("Shader optimization"), QStringLiteral("SPIR-V optimization mode"), QStringLiteral("shader_optimization"), EmulatorSettings::shaderOptimizationModes()),
            choice(QStringLiteral("Shader log"), QStringLiteral("Shader log destination"), QStringLiteral("shader_log_direction"), EmulatorSettings::logDirections()),
            text(QStringLiteral("Shader log folder"), QStringLiteral("Folder for shader logs"), QStringLiteral("shader_log_folder")),
            toggle(QStringLiteral("Command buffer dump"), QStringLiteral("Dump guest command buffers"), QStringLiteral("command_buffer_dump")),
            text(QStringLiteral("Command buffer folder"), QStringLiteral("Folder for buffer dumps"), QStringLiteral("command_buffer_dump_folder")),
            choice(QStringLiteral("Guest printf"), QStringLiteral("Guest printf destination"), QStringLiteral("printf_direction"), EmulatorSettings::logDirections()),
            text(QStringLiteral("Guest printf file"), QStringLiteral("Output filename"), QStringLiteral("printf_output_file")),
            toggle(QStringLiteral("Profiler / Tracy"), QStringLiteral("Enable profiler hooks"), QStringLiteral("profiler")),
            toggle(QStringLiteral("RenderDoc"), QStringLiteral("Enable RenderDoc integration"), QStringLiteral("renderdoc")),
            toggle(QStringLiteral("AMD CPU patch"), QStringLiteral("Compatibility CPU path"), QStringLiteral("amd_cpu")),
        },
        {
            toggle(QStringLiteral("Disable splash"), QStringLiteral("Disable Kyty splash"), QStringLiteral("no_splash")),
            toggle(QStringLiteral("Virtual VR headset"), QStringLiteral("Expose VR state"), QStringLiteral("vr")),
            toggle(QStringLiteral("GPU-assisted validation"), QStringLiteral("Vulkan GPU validation"), QStringLiteral("gpu_assisted_validation")),
            toggle(QStringLiteral("SPIR-V debug printf"), QStringLiteral("Shader debug printf"), QStringLiteral("spirv_debug_printf")),
            toggle(QStringLiteral("Graphics debug dump"), QStringLiteral("Dump graphics debugging data"), QStringLiteral("graphics_debug_dump")),
            toggle(QStringLiteral("PlayGo hack"), QStringLiteral("Experimental compatibility option"), QStringLiteral("playgo_hack")),
        },
    };
}

QWidget *BigPictureWindow::buildSettingsPage()
{
    auto *page = new QWidget();
    page->setObjectName(QStringLiteral("settingsPage"));
    auto *outer = makeSafeLayout(page);
    outer->addWidget(makeBrandHeader(QStringLiteral("SETTINGS")));

    m_settingsTabs = label(QString(), QStringLiteral("settingsTabs"));
    m_settingsTabs->setAlignment(Qt::AlignCenter);
    outer->addWidget(m_settingsTabs);

    m_settingsSectionTitle = label(QStringLiteral("Graphics"), QStringLiteral("settingsTitle"));
    m_settingsSectionDesc = label(QString(), QStringLiteral("settingsDescription"));
    m_settingsSectionDesc->setWordWrap(true);
    outer->addWidget(m_settingsSectionTitle);
    outer->addWidget(m_settingsSectionDesc);

    m_settingsScroll = new QScrollArea();
    m_settingsScroll->setWidgetResizable(true);
    m_settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_settingsScroll->setFrameShape(QFrame::NoFrame);
    m_settingsRowsWidget = new QWidget();
    m_settingsRows = new QVBoxLayout(m_settingsRowsWidget);
    m_settingsRows->setContentsMargins(0, 4, 0, 4);
    m_settingsRows->setSpacing(7);
    m_settingsScroll->setWidget(m_settingsRowsWidget);
    outer->addWidget(m_settingsScroll, 1);

    auto *hint = label(QStringLiteral("↑ ↓  SELECT     ◄ ► / X  CHANGE     A  EDIT / TOGGLE     B  LIBRARY     LB/RB  SECTION"), QStringLiteral("bottomHint"));
    hint->setAlignment(Qt::AlignCenter);
    outer->addWidget(hint);
    return page;
}

QWidget *BigPictureWindow::buildBrowserPage()
{
    auto *page = new QWidget();
    page->setObjectName(QStringLiteral("browserPage"));
    auto *outer = makeSafeLayout(page);
    outer->addWidget(makeBrandHeader(QStringLiteral("ADD GAME")));
    outer->addWidget(label(QStringLiteral("Choose a game folder, ZIP, ZAR or ELF"), QStringLiteral("settingsTitle")));

    m_browserPath = label(QString(), QStringLiteral("browserPath"));
    m_browserPath->setWordWrap(true);
    outer->addWidget(m_browserPath);

    m_browserList = new QListWidget();
    m_browserList->setObjectName(QStringLiteral("browserList"));
    m_browserList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_browserList->setFocusPolicy(Qt::StrongFocus);
    outer->addWidget(m_browserList, 1);

    m_browserStatus = label(QString(), QStringLiteral("browserStatus"));
    m_browserStatus->setWordWrap(true);
    outer->addWidget(m_browserStatus);
    auto *hint = label(QStringLiteral("A  OPEN / ADD      B  BACK      X  ADD CURRENT FOLDER      Y  INTERNAL STORAGE"), QStringLiteral("bottomHint"));
    hint->setAlignment(Qt::AlignCenter);
    outer->addWidget(hint);

    connect(m_browserList, &QListWidget::itemActivated, this, [this](QListWidgetItem *) { activateBrowserEntry(); });
    return page;
}

QWidget *BigPictureWindow::buildErrorPage()
{
    auto *page = new QWidget();
    page->setObjectName(QStringLiteral("errorPage"));
    auto *outer = makeSafeLayout(page);
    outer->addWidget(makeBrandHeader(QStringLiteral("CORE ERROR")));
    outer->addStretch(1);

    auto *panel = new QFrame();
    panel->setObjectName(QStringLiteral("errorPanel"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(10);
    m_errorTitle = label(QStringLiteral("KYTY CORE ERROR"), QStringLiteral("errorKicker"));
    m_errorSummary = label(QString(), QStringLiteral("errorSummary"));
    m_errorSummary->setWordWrap(true);
    m_errorHint = label(QStringLiteral("A / B  BACK TO LIBRARY      X  SHOW LOG"), QStringLiteral("errorHint"));
    m_errorDetails = new QPlainTextEdit();
    m_errorDetails->setObjectName(QStringLiteral("errorDetails"));
    m_errorDetails->setReadOnly(true);
    m_errorDetails->hide();
    layout->addWidget(m_errorTitle);
    layout->addWidget(m_errorSummary);
    layout->addWidget(m_errorHint);
    layout->addWidget(m_errorDetails, 1);
    outer->addWidget(panel, 3);
    outer->addStretch(1);
    return page;
}

QWidget *BigPictureWindow::buildEditorPage()
{
    auto *page = new QWidget();
    page->setObjectName(QStringLiteral("editorPage"));
    auto *outer = makeSafeLayout(page);
    outer->addWidget(makeBrandHeader(QStringLiteral("EDIT VALUE")));
    outer->addStretch(1);
    m_editorTitle = label(QString(), QStringLiteral("settingsTitle"));
    m_editorDescription = label(QString(), QStringLiteral("settingsDescription"));
    m_editorDescription->setWordWrap(true);
    m_editorLine = new QLineEdit();
    m_editorLine->setObjectName(QStringLiteral("bigEditor"));
    outer->addWidget(m_editorTitle);
    outer->addWidget(m_editorDescription);
    outer->addWidget(m_editorLine);
    outer->addWidget(label(QStringLiteral("A  SAVE      B  CANCEL      use the keyboard for text"), QStringLiteral("bottomHint")));
    outer->addStretch(2);
    return page;
}

QString BigPictureWindow::settingValueText(const SettingSpec &spec) const
{
    switch (spec.kind) {
    case SettingKind::Toggle:
        return m_emu.boolValue(spec.key) ? QStringLiteral("ON") : QStringLiteral("OFF");
    case SettingKind::Choice:
    case SettingKind::Text: {
        const QString value = m_emu.stringValue(spec.key);
        return value.isEmpty() ? QStringLiteral("NOT SET") : value;
    }
    case SettingKind::Integer:
        return QString::number(m_emu.intValue(spec.key));
    case SettingKind::TextList: {
        const QString value = m_emu.stringListValue(spec.key).join(QStringLiteral("; "));
        return value.isEmpty() ? QStringLiteral("NOT SET") : value;
    }
    }
    return {};
}

void BigPictureWindow::rebuildSettingsRows()
{
    while (QLayoutItem *item = m_settingsRows->takeAt(0)) {
        if (QWidget *widget = item->widget()) widget->deleteLater();
        delete item;
    }
    m_settingButtons.clear();

    static const QStringList titles = {
        QStringLiteral("GRAPHICS"), QStringLiteral("SYSTEM"), QStringLiteral("CONTROLLER"),
        QStringLiteral("DEBUG"), QStringLiteral("EXPERT")
    };
    static const QStringList descriptions = {
        QStringLiteral("Display, Vulkan output and compatibility options."),
        QStringLiteral("Virtual console identity, content and notifications."),
        QStringLiteral("DualSense emulation and guest input mapping."),
        QStringLiteral("Validation, logs, dumps and capture tools."),
        QStringLiteral("Experimental Kyty core flags."),
    };
    m_settingsTabs->setText(richTabs(titles, m_settingSection));
    m_settingsSectionTitle->setText(titles.at(m_settingSection));
    m_settingsSectionDesc->setText(descriptions.at(m_settingSection));

    const QList<SettingSpec> &specs = m_settingSections.at(m_settingSection);
    for (int i = 0; i < specs.size(); ++i) {
        const SettingSpec spec = specs.at(i);
        auto *button = new QPushButton();
        button->setObjectName(QStringLiteral("settingRow"));
        button->setFocusPolicy(Qt::StrongFocus);
        button->setProperty("rowIndex", i);
        button->setText(QStringLiteral("%1\n%2     ·     %3")
                            .arg(spec.label, settingValueText(spec), spec.description));
        connect(button, &QPushButton::clicked, this, [this, i] {
            m_settingIndex = i;
            activateSetting();
        });
        m_settingsRows->addWidget(button);
        m_settingButtons.push_back(button);
    }
    m_settingsRows->addStretch(1);
    selectSettingIndex(std::clamp(m_settingIndex, 0, std::max(0, m_settingButtons.size() - 1)));
}

void BigPictureWindow::openSettings(int section)
{
    m_settingSection = std::clamp(section, 0, m_settingSections.size() - 1);
    m_settingIndex = 0;
    rebuildSettingsRows();
    switchView(View::Settings, m_settingsPage);
    selectSettingIndex(0);
}

void BigPictureWindow::closeSettings()
{
    switchView(View::Library, m_libraryPage);
    selectLibraryIndex(m_libraryIndex);
}

void BigPictureWindow::switchSettingsSection(int delta)
{
    const int count = m_settingSections.size();
    m_settingSection = (m_settingSection + delta + count) % count;
    m_settingIndex = 0;
    rebuildSettingsRows();
}

void BigPictureWindow::selectSettingIndex(int index)
{
    if (m_settingButtons.isEmpty()) return;
    m_settingIndex = std::clamp(index, 0, m_settingButtons.size() - 1);
    QPushButton *button = m_settingButtons.at(m_settingIndex);
    button->setFocus(Qt::OtherFocusReason);
    m_settingsScroll->ensureWidgetVisible(button, 12, 70);
}

void BigPictureWindow::adjustSetting(int delta)
{
    if (m_settingSection < 0 || m_settingSection >= m_settingSections.size()) return;
    const auto &specs = m_settingSections.at(m_settingSection);
    if (m_settingIndex < 0 || m_settingIndex >= specs.size()) return;
    const SettingSpec &spec = specs.at(m_settingIndex);

    if (spec.kind == SettingKind::Toggle) {
        m_emu.setValue(spec.key, !m_emu.boolValue(spec.key));
    } else if (spec.kind == SettingKind::Choice) {
        if (spec.choices.isEmpty()) return;
        int index = spec.choices.indexOf(m_emu.stringValue(spec.key));
        if (index < 0) index = 0;
        index = (index + delta + spec.choices.size()) % spec.choices.size();
        m_emu.setValue(spec.key, spec.choices.at(index));
    } else if (spec.kind == SettingKind::Integer) {
        const int current = m_emu.intValue(spec.key);
        m_emu.setValue(spec.key, std::clamp(current + delta * spec.step, spec.minimum, spec.maximum));
    } else {
        openTextEditor(spec);
        return;
    }
    rebuildSettingsRows();
    selectSettingIndex(m_settingIndex);
}

void BigPictureWindow::activateSetting()
{
    const auto &specs = m_settingSections.at(m_settingSection);
    if (m_settingIndex < 0 || m_settingIndex >= specs.size()) return;
    const SettingSpec &spec = specs.at(m_settingIndex);
    if (spec.kind == SettingKind::Text || spec.kind == SettingKind::TextList) {
        openTextEditor(spec);
    } else {
        adjustSetting(1);
    }
}

void BigPictureWindow::openTextEditor(const SettingSpec &spec)
{
    m_editorKey = spec.key;
    m_editorKind = spec.kind;
    m_editorTitle->setText(spec.label);
    m_editorDescription->setText(spec.description);
    if (spec.kind == SettingKind::TextList)
        m_editorLine->setText(m_emu.stringListValue(spec.key).join(QStringLiteral("; ")));
    else
        m_editorLine->setText(m_emu.stringValue(spec.key));
    switchView(View::Editor, m_editorPage);
    m_editorLine->setFocus(Qt::OtherFocusReason);
    m_editorLine->selectAll();
    QApplication::inputMethod()->show();
}

void BigPictureWindow::closeTextEditor(bool save)
{
    if (save) {
        if (m_editorKind == SettingKind::TextList)
            m_emu.setValue(m_editorKey, m_editorLine->text().split(QLatin1Char(';'), Qt::SkipEmptyParts));
        else
            m_emu.setValue(m_editorKey, m_editorLine->text());
    }
    QApplication::inputMethod()->hide();
    switchView(View::Settings, m_settingsPage);
    rebuildSettingsRows();
    selectSettingIndex(m_settingIndex);
}

void BigPictureWindow::reloadGames()
{
    while (QLayoutItem *item = m_libraryStrip->takeAt(0)) {
        if (QWidget *widget = item->widget()) widget->deleteLater();
        delete item;
    }
    m_libraryTiles.clear();
    m_games = loadGameLibrary(m_store);

    for (int i = 0; i < m_games.size(); ++i) {
        QPushButton *tile = createGameTile(m_games.at(i), i);
        m_libraryStrip->addWidget(tile);
        m_libraryTiles.push_back(tile);
    }
    QPushButton *add = createAddTile(m_games.size());
    m_libraryStrip->addWidget(add);
    m_libraryTiles.push_back(add);
    m_libraryStrip->addStretch(1);

    m_libraryCounter->setText(QStringLiteral("%1 GAME%2").arg(m_games.size()).arg(m_games.size() == 1 ? QString() : QStringLiteral("S")));
    m_libraryIndex = std::clamp(m_libraryIndex, 0, std::max(0, m_libraryTiles.size() - 1));
    updateHero();
    applyResponsiveMetrics();
    QTimer::singleShot(0, this, [this] { selectLibraryIndex(m_libraryIndex); });
}

QPushButton *BigPictureWindow::createGameTile(const GameEntry &game, int index)
{
    auto *button = new QPushButton();
    button->setObjectName(QStringLiteral("gameTile"));
    button->setFocusPolicy(Qt::StrongFocus);
    button->setProperty("tileIndex", index);
    auto *layout = new QVBoxLayout(button);
    layout->setContentsMargins(8, 8, 8, 10);
    layout->setSpacing(7);
    auto *art = label(QStringLiteral("PS5"), QStringLiteral("tileArt"));
    art->setAlignment(Qt::AlignCenter);
    if (!game.icon.isEmpty()) {
        QPixmap pixmap(game.icon);
        if (!pixmap.isNull()) art->setPixmap(pixmap.scaled(180, 130, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    auto *title = label(game.title, QStringLiteral("tileTitle"));
    title->setWordWrap(true);
    title->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(art, 1);
    layout->addWidget(title);
    connect(button, &QPushButton::clicked, this, [this, index] {
        selectLibraryIndex(index);
        activateLibrarySelection();
    });
    return button;
}

QPushButton *BigPictureWindow::createAddTile(int index)
{
    auto *button = new QPushButton(QStringLiteral("+\nADD GAME"));
    button->setObjectName(QStringLiteral("addTile"));
    button->setFocusPolicy(Qt::StrongFocus);
    button->setProperty("tileIndex", index);
    connect(button, &QPushButton::clicked, this, [this, index] {
        selectLibraryIndex(index);
        openBrowser();
    });
    return button;
}

void BigPictureWindow::selectLibraryIndex(int index)
{
    if (m_libraryTiles.isEmpty()) return;
    m_libraryIndex = std::clamp(index, 0, m_libraryTiles.size() - 1);
    QPushButton *tile = m_libraryTiles.at(m_libraryIndex);
    tile->setFocus(Qt::OtherFocusReason);
    m_libraryScroll->ensureWidgetVisible(tile, 28, 8);
    updateHero();
}

void BigPictureWindow::updateHero()
{
    if (m_libraryIndex >= 0 && m_libraryIndex < m_games.size()) {
        const GameEntry &game = m_games.at(m_libraryIndex);
        m_heroMeta->setText(QStringLiteral("PS5 GAME · KYTY CORE · READY"));
        m_heroTitle->setText(game.title);
        m_heroPath->setText(game.target);
        QPixmap pixmap(game.icon);
        if (!pixmap.isNull()) {
            m_heroArt->setText(QString());
            m_heroArt->setPixmap(pixmap.scaled(m_heroArt->size() - QSize(16, 16), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            m_heroArt->setPixmap(QPixmap());
            m_heroArt->setText(QStringLiteral("PS5"));
        }
    } else {
        m_heroMeta->setText(QStringLiteral("LIBRARY"));
        m_heroTitle->setText(QStringLiteral("Add a PS5 game"));
        m_heroPath->setText(QStringLiteral("Browse Android storage without opening another window."));
        m_heroArt->setPixmap(QPixmap());
        m_heroArt->setText(QStringLiteral("+"));
    }
}

void BigPictureWindow::activateLibrarySelection()
{
    if (m_libraryIndex >= 0 && m_libraryIndex < m_games.size()) launchGame(m_games.at(m_libraryIndex));
    else openBrowser();
}

void BigPictureWindow::openBrowser()
{
    QString start = pickerStart(m_store);
    if (!QDir(start).exists()) start = QStringLiteral("/storage/emulated/0");
    populateBrowser(start);
    switchView(View::Browser, m_browserPage);
    m_browserList->setFocus(Qt::OtherFocusReason);
}

void BigPictureWindow::closeBrowser()
{
    switchView(View::Library, m_libraryPage);
    selectLibraryIndex(m_libraryIndex);
}

bool BigPictureWindow::currentBrowserFolderIsGame() const
{
    return QFileInfo(QDir(m_browserDir).filePath(QStringLiteral("eboot.bin"))).isFile();
}

void BigPictureWindow::populateBrowser(const QString &directory)
{
    QDir dir(directory);
    if (!dir.exists()) {
        setBrowserStatus(QStringLiteral("Folder is not available."), true);
        return;
    }
    m_browserDir = dir.absolutePath();
    m_store.setValue(QStringLiteral("lastPickerDir"), m_browserDir);
    m_browserPath->setText(m_browserDir);
    m_browserList->clear();

    const auto addItem = [this](const QString &text, BrowserKind kind, const QString &path) {
        auto *item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, static_cast<int>(kind));
        item->setData(Qt::UserRole + 1, path);
        m_browserList->addItem(item);
    };

    const QString storageRoot = QStringLiteral("/storage/emulated/0");
    if (m_browserDir != storageRoot) {
        QDir parent(m_browserDir);
        if (parent.cdUp() && parent.absolutePath().startsWith(storageRoot))
            addItem(QStringLiteral("←  PARENT FOLDER"), BrowserKind::Parent, parent.absolutePath());
    }
    if (currentBrowserFolderIsGame())
        addItem(QStringLiteral("＋  ADD THIS GAME FOLDER     eboot.bin found"), BrowserKind::AddCurrent, m_browserDir);

    for (const QFileInfo &info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase))
        addItem(QStringLiteral("▣  %1").arg(info.fileName()), BrowserKind::Directory, info.absoluteFilePath());

    for (const QFileInfo &info : dir.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        if (info.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0)
            addItem(QStringLiteral("ZIP  %1     import & extract").arg(info.fileName()), BrowserKind::Zip, info.absoluteFilePath());
        else if (supportedDirectFile(info))
            addItem(QStringLiteral("PS5  %1").arg(info.fileName()), BrowserKind::DirectGame, info.absoluteFilePath());
    }

    if (m_browserList->count() == 0) {
        auto *item = new QListWidgetItem(QStringLiteral("No supported game files here"));
        item->setFlags(Qt::NoItemFlags);
        m_browserList->addItem(item);
    }
    m_browserList->setCurrentRow(0);
    setBrowserStatus(currentBrowserFolderIsGame()
        ? QStringLiteral("Game folder detected. Press X or choose ADD THIS GAME FOLDER.")
        : QStringLiteral("A opens folders or imports a game. B goes back."));
}

void BigPictureWindow::activateBrowserEntry()
{
    QListWidgetItem *item = m_browserList->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) return;
    const auto kind = static_cast<BrowserKind>(item->data(Qt::UserRole).toInt());
    const QString path = item->data(Qt::UserRole + 1).toString();
    QString error;

    if (kind == BrowserKind::Parent || kind == BrowserKind::Directory) {
        populateBrowser(path);
        return;
    }
    if (kind == BrowserKind::AddCurrent || kind == BrowserKind::DirectGame) {
        if (!addCustomSource(m_store, path, &error)) {
            setBrowserStatus(error, true);
            return;
        }
        reloadGames();
        closeBrowser();
        return;
    }
    if (kind == BrowserKind::Zip) {
        setBrowserStatus(QStringLiteral("Importing %1 …").arg(QFileInfo(path).fileName()));
        QApplication::processEvents();
        if (!importZipGame(path, &error)) {
            setBrowserStatus(error, true);
            return;
        }
        reloadGames();
        closeBrowser();
    }
}

void BigPictureWindow::addCurrentBrowserFolder()
{
    if (!currentBrowserFolderIsGame()) {
        setBrowserStatus(QStringLiteral("This folder does not contain eboot.bin."), true);
        return;
    }
    QString error;
    if (!addCustomSource(m_store, m_browserDir, &error)) {
        setBrowserStatus(error, true);
        return;
    }
    reloadGames();
    closeBrowser();
}

void BigPictureWindow::setBrowserStatus(const QString &text, bool error)
{
    m_browserStatus->setText(text);
    m_browserStatus->setProperty("error", error);
    style()->unpolish(m_browserStatus);
    style()->polish(m_browserStatus);
}

void BigPictureWindow::showError(const QString &title, const QString &summary, const QString &details)
{
    m_errorTitle->setText(title.toUpper());
    m_errorSummary->setText(summary);
    m_errorDetails->setPlainText(details);
    m_errorDetails->verticalScrollBar()->setValue(0);
    m_errorDetailsVisible = false;
    m_errorDetails->hide();
    m_errorHint->setText(QStringLiteral("A / B  BACK TO LIBRARY      X  SHOW LOG"));
    switchView(View::Error, m_errorPage);
}

void BigPictureWindow::closeError()
{
    switchView(View::Library, m_libraryPage);
    selectLibraryIndex(m_libraryIndex);
}

void BigPictureWindow::toggleErrorDetails()
{
    m_errorDetailsVisible = !m_errorDetailsVisible;
    m_errorDetails->setVisible(m_errorDetailsVisible);
    m_errorHint->setText(m_errorDetailsVisible
        ? QStringLiteral("A / B  BACK TO LIBRARY      X  HIDE LOG")
        : QStringLiteral("A / B  BACK TO LIBRARY      X  SHOW LOG"));
}

void BigPictureWindow::launchGame(const GameEntry &game)
{
    const QString emulator = QDir(kytyDir()).filePath(QStringLiteral("kyty_emulator"));
    if (!QFileInfo::exists(emulator)) {
        showError(QStringLiteral("Core missing"), QStringLiteral("kyty_emulator was not found inside the AppImage."), emulator);
        return;
    }
    startTrackedProcess(emulator, m_emu.buildArgs(game.target));
}

void BigPictureWindow::startTrackedProcess(const QString &program, const QStringList &args)
{
    if (m_childPid > 0) return;
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString logFile = QDir(logsRoot()).filePath(QStringLiteral("game-%1.log").arg(stamp));
    const SpawnResult result = spawnWithLog(program, args, logFile);
    if (!result.ok) {
        showError(QStringLiteral("Launch failed"), result.error, QStringLiteral("Log: %1").arg(logFile));
        return;
    }
    m_childPid = result.pid;
    m_childTicks = 0;
    m_childLog = result.logFile;
    m_processTimer->start();
    hide();
}

void BigPictureWindow::pollChild()
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
    forceFullscreen();

    if (result < 0) {
        showError(QStringLiteral("Process tracking failed"), QStringLiteral("Could not read the Kyty process state."), QString::fromLocal8Bit(std::strerror(errno)));
        return;
    }

    int exitCode = 0;
    QString summary;
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
        summary = QStringLiteral("Kyty core exited with code %1").arg(exitCode);
    } else if (WIFSIGNALED(status)) {
        summary = QStringLiteral("Kyty core terminated by signal %1").arg(WTERMSIG(status));
    } else {
        summary = QStringLiteral("Kyty core stopped");
    }

    const QString output = readLogTail(m_childLog);
    const bool failed = !WIFEXITED(status) || exitCode != 0 || m_childTicks < 10;
    if (!failed) {
        switchView(View::Library, m_libraryPage);
        selectLibraryIndex(m_libraryIndex);
        return;
    }

    if (output.contains(QStringLiteral("could not reserve 13824 MB"), Qt::CaseInsensitive) ||
        (output.contains(QStringLiteral("could not reserve"), Qt::CaseInsensitive) &&
         output.contains(QStringLiteral("guest direct memory"), Qt::CaseInsensitive))) {
        showError(QStringLiteral("Memory mapping"),
                  QStringLiteral("The Kyty core could not create the PS5 direct-memory mapping under DroidDeck/FEX."), output);
        return;
    }
    showError(QStringLiteral("Kyty core error"), summary, output.isEmpty() ? QStringLiteral("No output was written.\n%1").arg(m_childLog) : output);
}

void BigPictureWindow::handleGamepad(PadAction action)
{
    if (m_childPid > 0 || !isVisible()) return;

    if (m_view == View::Library) {
        if (action == PadAction::Left || action == PadAction::Up) selectLibraryIndex(m_libraryIndex - 1);
        else if (action == PadAction::Right || action == PadAction::Down) selectLibraryIndex(m_libraryIndex + 1);
        else if (action == PadAction::Confirm) activateLibrarySelection();
        else if (action == PadAction::NextValue) openSettings(0);
        else if (action == PadAction::AddGame) openBrowser();
        else if (action == PadAction::Refresh) reloadGames();
        else if (action == PadAction::PrevSection) openSettings(m_settingSections.size() - 1);
        else if (action == PadAction::NextSection) openSettings(0);
        return;
    }

    if (m_view == View::Settings) {
        if (action == PadAction::Up) selectSettingIndex(m_settingIndex - 1);
        else if (action == PadAction::Down) selectSettingIndex(m_settingIndex + 1);
        else if (action == PadAction::Left) adjustSetting(-1);
        else if (action == PadAction::Right || action == PadAction::NextValue) adjustSetting(1);
        else if (action == PadAction::Confirm) activateSetting();
        else if (action == PadAction::Back) closeSettings();
        else if (action == PadAction::PrevSection) switchSettingsSection(-1);
        else if (action == PadAction::NextSection) switchSettingsSection(1);
        return;
    }

    if (m_view == View::Browser) {
        if (action == PadAction::Up || action == PadAction::Left)
            m_browserList->setCurrentRow(std::max(0, m_browserList->currentRow() - 1));
        else if (action == PadAction::Down || action == PadAction::Right)
            m_browserList->setCurrentRow(std::min(m_browserList->count() - 1, m_browserList->currentRow() + 1));
        else if (action == PadAction::Confirm) activateBrowserEntry();
        else if (action == PadAction::NextValue) addCurrentBrowserFolder();
        else if (action == PadAction::AddGame) populateBrowser(QStringLiteral("/storage/emulated/0"));
        else if (action == PadAction::Back) {
            const QString root = QStringLiteral("/storage/emulated/0");
            if (m_browserDir != root) {
                QDir dir(m_browserDir);
                if (dir.cdUp() && dir.absolutePath().startsWith(root)) populateBrowser(dir.absolutePath());
                else closeBrowser();
            } else closeBrowser();
        }
        return;
    }

    if (m_view == View::Error) {
        if (action == PadAction::NextValue) toggleErrorDetails();
        else if (action == PadAction::Back || action == PadAction::Confirm) closeError();
        return;
    }

    if (m_view == View::Editor) {
        if (action == PadAction::Back) closeTextEditor(false);
        else if (action == PadAction::Confirm) closeTextEditor(true);
    }
}

void BigPictureWindow::switchView(View view, QWidget *page)
{
    m_view = view;
    m_pages->setCurrentWidget(page);
}

void BigPictureWindow::applyStyle(double scale)
{
    const auto px = [scale](int value) { return std::max(1, static_cast<int>(value * scale)); };
    setStyleSheet(QStringLiteral(R"(
        QWidget { background:#05080e; color:#f5f8ff; font-family:"DejaVu Sans",sans-serif; }
        QWidget#libraryPage, QWidget#settingsPage, QWidget#browserPage, QWidget#errorPage, QWidget#editorPage {
            background:qradialgradient(cx:.62,cy:.24,radius:1.1,fx:.62,fy:.24,stop:0 #102849,stop:.34 #091529,stop:.72 #050b14,stop:1 #030509);
        }
        QLabel#brandMark { background:#1769ff; color:white; border-radius:%1px; font-weight:900; font-size:%2px; min-width:%3px; min-height:%3px; max-width:%3px; max-height:%3px; }
        QLabel#brandTitle { font-size:%4px; font-weight:900; }
        QLabel#brandSubtitle { color:#7790ad; font-size:%5px; font-weight:700; }
        QLabel#activePage { color:#7acbff; font-size:%6px; font-weight:900; padding:%7px %8px; }
        QLabel#padStatus { background:#0c1725; border:1px solid #203852; border-radius:%7px; padding:%7px %8px; color:#8398b2; font-size:%5px; font-weight:800; }
        QLabel#padStatus[connected="true"] { color:#66efbf; border-color:#206f58; background:#082018; }
        QLabel#padStatus[connected="false"] { color:#ff8497; border-color:#66303d; background:#210b12; }

        QFrame#hero { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 rgba(22,56,105,225),stop:.55 rgba(9,22,43,235),stop:1 rgba(15,11,34,235)); border:1px solid #2a4f7b; border-radius:%9px; }
        QLabel#heroMeta { color:#6ed7ff; font-size:%6px; font-weight:900; letter-spacing:1px; }
        QLabel#heroTitle { font-size:%10px; font-weight:900; }
        QLabel#heroPath { color:#9aabc0; font-size:%6px; }
        QLabel#heroAction { color:#d9e7f6; font-size:%6px; font-weight:800; }
        QLabel#heroArt { background:rgba(2,6,12,180); border:1px solid #2b496c; border-radius:%9px; color:#6f86a1; font-size:%11px; font-weight:900; }
        QLabel#sectionKicker, QLabel#libraryCounter { color:#8398b0; font-size:%5px; font-weight:900; letter-spacing:1px; }
        QLabel#bottomHint { background:rgba(6,13,24,210); border:1px solid #182c46; border-radius:%7px; padding:%7px; color:#c9d7e8; font-size:%5px; font-weight:800; }

        QPushButton#gameTile, QPushButton#addTile { background:#0a111d; border:2px solid #192a41; border-radius:%8px; padding:0; color:white; text-align:center; }
        QPushButton#gameTile:focus, QPushButton#addTile:focus { background:#102d50; border:%12px solid #65d8ff; }
        QLabel#tileArt { background:#03070c; border:1px solid #17283d; border-radius:%7px; color:#506986; font-size:%13px; font-weight:900; }
        QLabel#tileTitle { font-size:%6px; font-weight:800; background:transparent; }
        QPushButton#addTile { color:#80dfff; font-size:%14px; font-weight:900; }

        QLabel#settingsTabs { background:rgba(6,13,24,210); border:1px solid #172c46; border-radius:%7px; padding:%8px; font-size:%6px; }
        QLabel#settingsTitle { font-size:%15px; font-weight:900; }
        QLabel#settingsDescription { color:#8497ae; font-size:%6px; }
        QPushButton#settingRow { background:#0b1422; border:2px solid #172b46; border-radius:%7px; color:#c9d6e5; padding:%8px %16px; text-align:left; font-size:%6px; font-weight:700; }
        QPushButton#settingRow:focus { background:#12385f; border:%12px solid #6ddcff; color:white; }

        QLabel#browserPath { background:#07182a; border:1px solid #1f4b74; border-radius:%7px; color:#83ddff; padding:%8px; font-family:monospace; font-size:%6px; }
        QListWidget#browserList { background:#07101b; border:1px solid #182b43; border-radius:%8px; padding:%7px; outline:0; font-size:%6px; }
        QListWidget#browserList::item { padding:%8px %16px; margin:%17px; border-radius:%7px; color:#b8c7d8; }
        QListWidget#browserList::item:selected { background:#123b66; border:2px solid #6cddff; color:white; font-weight:800; }
        QLabel#browserStatus { color:#9badc1; font-size:%6px; }
        QLabel#browserStatus[error="true"] { color:#ff8195; }

        QFrame#errorPanel { background:rgba(15,12,22,235); border:2px solid #653043; border-radius:%9px; }
        QLabel#errorKicker { color:#ff718b; font-size:%6px; font-weight:900; }
        QLabel#errorSummary { font-size:%15px; font-weight:900; }
        QLabel#errorHint { color:#bdd2e8; font-size:%6px; font-weight:800; }
        QPlainTextEdit#errorDetails { background:#03060a; border:1px solid #263a51; border-radius:%7px; color:#d7e2ee; font-family:monospace; font-size:%5px; padding:%8px; }

        QLineEdit#bigEditor { background:#081525; border:%12px solid #65d8ff; border-radius:%8px; color:white; padding:%8px; font-size:%15px; }
        QScrollArea { border:none; background:transparent; }
        QScrollBar:vertical { width:%7px; background:#07101a; }
        QScrollBar::handle:vertical { background:#294d73; border-radius:%17px; min-height:%3px; }
    )")
        .arg(px(11)).arg(px(20)).arg(px(50)).arg(px(24)).arg(px(10)).arg(px(13)).arg(px(8)).arg(px(12)).arg(px(18)).arg(px(38)).arg(px(42)).arg(px(3)).arg(px(30)).arg(px(18)).arg(px(27)).arg(px(15)).arg(px(3)));
}

void BigPictureWindow::applyResponsiveMetrics()
{
    if (width() <= 0 || height() <= 0) return;
    const double scale = std::clamp(height() / 720.0, 0.72, 1.65);
    const int side = std::clamp(static_cast<int>(width() * 0.115), 72, 260);
    const int top = std::max(12, static_cast<int>(18 * scale));
    const int bottom = std::max(12, static_cast<int>(16 * scale));
    for (QVBoxLayout *layout : m_safeLayouts) layout->setContentsMargins(side, top, side, bottom);

    const int artSide = std::clamp(static_cast<int>(height() * 0.27), 150, 290);
    if (m_heroArt) m_heroArt->setFixedSize(artSide, artSide);

    const int tileWidth = std::clamp(static_cast<int>(150 * scale), 116, 220);
    const int tileHeight = std::clamp(static_cast<int>(190 * scale), 145, 285);
    for (QPushButton *tile : m_libraryTiles) tile->setFixedSize(tileWidth, tileHeight);
    if (m_libraryScroll) m_libraryScroll->setFixedHeight(tileHeight + std::max(12, static_cast<int>(16 * scale)));
    if (m_libraryContainer) m_libraryContainer->setMinimumWidth(std::max(width(), m_libraryTiles.size() * (tileWidth + 12) + 20));

    applyStyle(scale);
    updateHero();
}

void BigPictureWindow::forceFullscreen()
{
    QScreen *target = screen();
    if (!target) target = QGuiApplication::primaryScreen();
    if (target) setGeometry(target->geometry());
    setWindowState(windowState() | Qt::WindowFullScreen);
    showFullScreen();
    raise();
    activateWindow();
}

void BigPictureWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    applyResponsiveMetrics();
}

void BigPictureWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    QTimer::singleShot(0, this, [this] { forceFullscreen(); });
    QTimer::singleShot(250, this, [this] { forceFullscreen(); });
}
