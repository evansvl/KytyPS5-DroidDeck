#include "ConsoleWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputMethod>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <functional>
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

QString readLogTail(const QString &path, int maxBytes = 24000)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("No log output was available.");
    if (file.size() > maxBytes) file.seek(file.size() - maxBytes);
    return QString::fromUtf8(file.readAll()).trimmed();
}

class FocusRow final : public QFrame
{
public:
    FocusRow(const QString &name, QWidget *control, const QString &description = {}, QWidget *parent = nullptr)
        : QFrame(parent)
        , m_control(control)
    {
        setObjectName(QStringLiteral("settingRow"));
        setProperty("active", false);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(16, 11, 14, 11);
        layout->setSpacing(18);

        auto *text = new QVBoxLayout();
        auto *label = new QLabel(name, this);
        label->setObjectName(QStringLiteral("settingName"));
        text->addWidget(label);
        if (!description.isEmpty()) {
            auto *desc = new QLabel(description, this);
            desc->setObjectName(QStringLiteral("settingDescription"));
            desc->setWordWrap(true);
            text->addWidget(desc);
        }
        layout->addLayout(text, 1);
        layout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);

        control->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_control && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)) {
            setProperty("active", event->type() == QEvent::FocusIn);
            style()->unpolish(this);
            style()->polish(this);
            update();
        }
        return QFrame::eventFilter(watched, event);
    }

private:
    QWidget *m_control = nullptr;
};

class GameCardButton final : public QPushButton
{
public:
    GameCardButton(const GameEntry &game, std::function<void()> focused, QWidget *parent = nullptr)
        : QPushButton(parent)
        , m_focused(std::move(focused))
    {
        setObjectName(QStringLiteral("gameCard"));
        setFocusPolicy(Qt::StrongFocus);
        setFixedSize(250, 310);
        setText(QString());

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(11, 11, 11, 13);
        layout->setSpacing(10);

        auto *cover = new QLabel(this);
        cover->setObjectName(QStringLiteral("gameCover"));
        cover->setAlignment(Qt::AlignCenter);
        cover->setFixedHeight(192);
        cover->setAttribute(Qt::WA_TransparentForMouseEvents);
        if (!game.icon.isEmpty()) {
            QPixmap pixmap(game.icon);
            if (!pixmap.isNull()) {
                cover->setPixmap(pixmap.scaled(218, 184, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }
        if (cover->pixmap().isNull()) cover->setText(QStringLiteral("PS5"));

        auto *badge = new QLabel(QStringLiteral("PS5 GAME"), this);
        badge->setObjectName(QStringLiteral("gameBadge"));
        badge->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto *title = new QLabel(game.title, this);
        title->setObjectName(QStringLiteral("gameTitle"));
        title->setWordWrap(true);
        title->setAttribute(Qt::WA_TransparentForMouseEvents);

        layout->addWidget(cover);
        layout->addWidget(badge, 0, Qt::AlignLeft);
        layout->addWidget(title);
        layout->addStretch(1);
    }

protected:
    void focusInEvent(QFocusEvent *event) override
    {
        QPushButton::focusInEvent(event);
        if (m_focused) m_focused();
    }

private:
    std::function<void()> m_focused;
};

QComboBox *boundCombo(EmulatorSettings &settings, const QString &key, const QStringList &items,
                      const QString &fallback, QWidget *parent = nullptr)
{
    auto *combo = new QComboBox(parent);
    combo->addItems(items);
    const int index = combo->findText(settings.stringValue(key, fallback));
    combo->setCurrentIndex(index >= 0 ? index : 0);
    combo->setMinimumWidth(220);
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
    spin->setMinimumWidth(180);
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
    edit->setMinimumWidth(260);
    edit->setFocusPolicy(Qt::StrongFocus);
    QObject::connect(edit, &QLineEdit::textChanged, edit, [&settings, key](const QString &value) {
        settings.setValue(key, value);
    });
    return edit;
}

QGroupBox *settingsGroup(const QString &title, QVBoxLayout **rows, QWidget *parent = nullptr)
{
    auto *box = new QGroupBox(title, parent);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(10, 18, 10, 10);
    layout->setSpacing(7);
    *rows = layout;
    return box;
}

void addSetting(QVBoxLayout *rows, const QString &name, QWidget *control, const QString &description = {})
{
    rows->addWidget(new FocusRow(name, control, description));
}

QLabel *makePill(const QString &text, const QString &objectName, QWidget *parent = nullptr)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(objectName);
    label->setAlignment(Qt::AlignCenter);
    return label;
}

bool supportedDirectFile(const QFileInfo &info)
{
    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("zar") || suffix == QStringLiteral("elf")) return true;
    return info.fileName().compare(QStringLiteral("eboot.bin"), Qt::CaseInsensitive) == 0;
}

} // namespace

ConsoleWindow::ConsoleWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_settings(QStringLiteral("evansvl"), QStringLiteral("KytyPS5-DroidDeck"))
    , m_emu(m_settings)
{
    m_emu.ensureDefaults();
    setWindowTitle(QStringLiteral("KytyPS5 · DroidDeck"));
    resize(1280, 800);
    setMinimumSize(960, 600);
    buildUi();

    m_gamepad = std::make_unique<EvdevGamepad>(
        [this](PadAction action) { handleGamepad(action); },
        [this](int count) {
            m_controllerLabel->setText(count > 0
                ? QStringLiteral("PAD  CONNECTED")
                : QStringLiteral("PAD  NOT FOUND"));
            m_controllerLabel->setProperty("connected", count > 0);
            m_controllerLabel->style()->unpolish(m_controllerLabel);
            m_controllerLabel->style()->polish(m_controllerLabel);
        }, this);

    m_processTimer = new QTimer(this);
    m_processTimer->setInterval(350);
    connect(m_processTimer, &QTimer::timeout, this, [this] { pollChild(); });

    reloadGames();
    updateHints();
}

void ConsoleWindow::buildUi()
{
    m_rootStack = new QStackedWidget(this);
    m_shellPage = buildShellPage();
    m_browserPage = buildBrowserPage();
    m_errorPage = buildErrorPage();
    m_rootStack->addWidget(m_shellPage);
    m_rootStack->addWidget(m_browserPage);
    m_rootStack->addWidget(m_errorPage);
    setCentralWidget(m_rootStack);

    setStyleSheet(QStringLiteral(R"(
        QWidget {
            background:#070b12;
            color:#f6f9ff;
            font-size:15px;
            font-family:"DejaVu Sans", sans-serif;
        }
        QLabel#brandMark {
            min-width:54px; min-height:54px; max-width:54px; max-height:54px;
            border-radius:16px;
            background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #1769ff,stop:0.58 #315cff,stop:1 #8f55ff);
            color:white; font-size:21px; font-weight:900;
        }
        QLabel#brandTitle { font-size:28px; font-weight:800; letter-spacing:.5px; }
        QLabel#brandSubtitle { color:#8898ad; font-size:12px; }
        QLabel#corePill, QLabel#controllerPill {
            background:#101827; border:1px solid #24344d; border-radius:12px;
            padding:8px 12px; color:#a9bad0; font-size:11px; font-weight:800;
        }
        QLabel#controllerPill[connected="true"] { color:#83f2ca; border-color:#236c5d; background:#0c211e; }
        QLabel#controllerPill[connected="false"] { color:#ff9a9a; border-color:#6e3038; background:#241014; }

        QListWidget#sections {
            background:#0b111c; border:1px solid #182235; border-radius:18px; padding:8px;
            outline:0;
        }
        QListWidget#sections::item {
            color:#8797aa; padding:15px 13px; margin:3px; border-radius:11px; font-weight:700;
        }
        QListWidget#sections::item:selected {
            color:white; background:#1769ff; border-left:4px solid #8ee7ff;
        }

        QFrame#heroPanel {
            border:1px solid #1d3152; border-radius:22px;
            background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #0d1f3d,stop:.52 #0c1424,stop:1 #111024);
        }
        QLabel#heroArt {
            background:#060a10; border:1px solid #233451; border-radius:16px;
            color:#536680; font-size:42px; font-weight:900;
        }
        QLabel#heroTitle { font-size:30px; font-weight:900; }
        QLabel#heroMeta { color:#77cfff; font-size:12px; font-weight:800; letter-spacing:.7px; }
        QLabel#heroPath { color:#7f8da1; font-size:11px; }
        QPushButton#heroLaunch {
            background:#1769ff; color:white; border:1px solid #4b96ff; border-radius:12px;
            padding:11px 20px; font-weight:900;
        }

        QPushButton#gameCard {
            background:#0d1420; border:2px solid #1c2a3c; border-radius:18px; padding:0; text-align:left;
        }
        QPushButton#gameCard:hover { border-color:#34577e; background:#101b2b; }
        QPushButton#gameCard:focus {
            border:4px solid #6ed9ff;
            background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #16345d,stop:.25 #10233d,stop:1 #0d1726);
        }
        QLabel#gameCover {
            background:#060a10; border:1px solid #1b2838; border-radius:12px;
            color:#4e6179; font-size:38px; font-weight:900;
        }
        QLabel#gameBadge {
            color:#72d5ff; background:#0d2740; border-radius:7px; padding:3px 7px;
            font-size:9px; font-weight:900;
        }
        QLabel#gameTitle { font-size:17px; font-weight:800; color:#f8fbff; }
        QPushButton#addGameCard {
            background:#0a101a; border:2px dashed #2a4565; border-radius:18px;
            color:#7ea2c9; font-size:20px; font-weight:800;
        }
        QPushButton#addGameCard:focus {
            color:white; border:4px solid #6ed9ff; background:#102b49;
        }

        QLabel#pageTitle { font-size:26px; font-weight:900; }
        QLabel#pageDescription { color:#8190a3; font-size:12px; }
        QGroupBox {
            background:#0a1019; border:1px solid #182438; border-radius:16px;
            margin-top:12px; padding:10px; font-weight:800; color:#a9b8cb;
        }
        QGroupBox::title { subcontrol-origin:margin; left:14px; padding:0 7px; }
        QFrame#settingRow {
            background:#0d1521; border:1px solid #17263a; border-radius:12px;
        }
        QFrame#settingRow[active="true"] {
            background:#102b4c; border:2px solid #67d6ff;
        }
        QLabel#settingName { font-size:15px; font-weight:800; }
        QLabel#settingDescription { color:#728398; font-size:10px; }
        QComboBox, QSpinBox, QLineEdit, QPlainTextEdit {
            background:#111c2a; border:1px solid #2b405b; border-radius:9px; padding:8px 11px;
            selection-background-color:#1769ff;
        }
        QComboBox:focus, QSpinBox:focus, QLineEdit:focus, QPlainTextEdit:focus {
            border:2px solid #8ee7ff; background:#132944;
        }
        QCheckBox { spacing:10px; }
        QCheckBox::indicator { width:25px; height:25px; border:2px solid #38506e; border-radius:7px; background:#0a111b; }
        QCheckBox::indicator:checked { background:#1769ff; border-color:#74d9ff; }
        QCheckBox:focus::indicator { border:3px solid #8ee7ff; }

        QPushButton#actionButton {
            background:#111c2a; border:1px solid #2a405c; border-radius:11px; padding:10px 15px; font-weight:800;
        }
        QPushButton#actionButton:hover { background:#17273b; }
        QPushButton#actionButton:focus { border:3px solid #6ed9ff; background:#153358; }
        QPushButton#primaryButton {
            background:#1769ff; border:1px solid #4e9cff; border-radius:11px; padding:10px 16px; font-weight:900;
        }
        QPushButton#primaryButton:focus { border:3px solid #91e9ff; background:#2878ff; }

        QLabel#statusBar {
            color:#96a6b9; background:#0a111c; border:1px solid #172338; border-radius:10px; padding:7px 11px;
            font-size:11px;
        }
        QLabel#hintBar {
            color:#c4d1df; background:#0b1320; border-top:1px solid #18263b; padding:10px;
            font-size:12px; font-weight:700;
        }

        QListWidget#browserList {
            background:#0a1019; border:1px solid #1a2a40; border-radius:16px; padding:8px; outline:0;
        }
        QListWidget#browserList::item {
            color:#c0ccda; padding:14px 16px; margin:3px; border-radius:10px; border:1px solid transparent;
        }
        QListWidget#browserList::item:selected {
            color:white; background:#12345a; border:2px solid #6ed9ff;
        }
        QLabel#browserPath {
            color:#8ee7ff; background:#0a1625; border:1px solid #1c3e62; border-radius:10px; padding:9px 12px;
            font-family:monospace;
        }
        QLabel#browserStatus { color:#8fa0b5; padding:6px; }
        QLabel#browserStatus[error="true"] { color:#ff8e9e; }

        QFrame#errorPanel { background:#0d121c; border:1px solid #512938; border-radius:20px; }
        QLabel#errorTitle { color:#ff788e; font-size:13px; font-weight:900; }
        QLabel#errorSummary { font-size:23px; font-weight:900; }
        QPlainTextEdit#errorDetails { background:#070b10; border:1px solid #29364a; font-family:monospace; font-size:11px; }

        QScrollArea { border:none; background:transparent; }
        QScrollBar:horizontal { background:#0a1018; height:7px; border-radius:3px; }
        QScrollBar::handle:horizontal { background:#294a70; border-radius:3px; min-width:40px; }
        QScrollBar:vertical { background:#0a1018; width:7px; border-radius:3px; }
        QScrollBar::handle:vertical { background:#294a70; border-radius:3px; min-height:40px; }
    )"));
}

QWidget *ConsoleWindow::buildShellPage()
{
    auto *page = new QWidget();
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(20, 16, 20, 14);
    outer->setSpacing(12);

    auto *header = new QHBoxLayout();
    header->setSpacing(12);
    auto *mark = new QLabel(QStringLiteral("K5"));
    mark->setObjectName(QStringLiteral("brandMark"));
    mark->setAlignment(Qt::AlignCenter);
    header->addWidget(mark);

    auto *brand = new QVBoxLayout();
    brand->setSpacing(1);
    auto *title = new QLabel(QStringLiteral("KytyPS5"));
    title->setObjectName(QStringLiteral("brandTitle"));
    auto *subtitle = new QLabel(QStringLiteral("PS5 translation layer  ·  DroidDeck / FEX"));
    subtitle->setObjectName(QStringLiteral("brandSubtitle"));
    brand->addWidget(title);
    brand->addWidget(subtitle);
    header->addLayout(brand);
    header->addStretch(1);

    header->addWidget(makePill(QStringLiteral("KYTY CORE  ·  VULKAN"), QStringLiteral("corePill")));
    m_controllerLabel = makePill(QStringLiteral("PAD  DETECTING"), QStringLiteral("controllerPill"));
    m_controllerLabel->setProperty("connected", false);
    header->addWidget(m_controllerLabel);
    outer->addLayout(header);

    auto *body = new QHBoxLayout();
    body->setSpacing(14);

    m_sections = new QListWidget();
    m_sections->setObjectName(QStringLiteral("sections"));
    m_sections->setFixedWidth(195);
    m_sections->setFocusPolicy(Qt::NoFocus);
    m_sections->addItems({
        QStringLiteral("▦   LIBRARY"),
        QStringLiteral("◇   GRAPHICS"),
        QStringLiteral("●   SYSTEM"),
        QStringLiteral("✦   CONTROLLER"),
        QStringLiteral("!   DEBUG"),
        QStringLiteral("…   EXPERT")
    });
    m_sections->setCurrentRow(0);
    body->addWidget(m_sections);

    m_sectionStack = new QStackedWidget();
    m_sectionStack->addWidget(buildLibraryPage());
    m_sectionStack->addWidget(buildGraphicsPage());
    m_sectionStack->addWidget(buildSystemPage());
    m_sectionStack->addWidget(buildControllerPage());
    m_sectionStack->addWidget(buildDebugPage());
    m_sectionStack->addWidget(buildExpertPage());
    body->addWidget(m_sectionStack, 1);
    outer->addLayout(body, 1);

    m_statusLabel = new QLabel(QStringLiteral("Ready"));
    m_statusLabel->setObjectName(QStringLiteral("statusBar"));
    outer->addWidget(m_statusLabel);

    m_hintLabel = new QLabel();
    m_hintLabel->setObjectName(QStringLiteral("hintBar"));
    m_hintLabel->setAlignment(Qt::AlignCenter);
    outer->addWidget(m_hintLabel);

    connect(m_sections, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= m_sectionStack->count()) return;
        m_lastSection = row;
        m_sectionStack->setCurrentIndex(row);
        updateHints();
        QTimer::singleShot(0, this, [this, row] {
            if (row == 0) focusLibraryButton(std::max(0, focusedLibraryIndex()));
            else focusFirstControl();
        });
    });

    return page;
}

QWidget *ConsoleWindow::buildLibraryPage()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(11);

    auto *top = new QHBoxLayout();
    auto *heading = new QLabel(QStringLiteral("Your games"));
    heading->setObjectName(QStringLiteral("pageTitle"));
    top->addWidget(heading);
    top->addStretch(1);
    auto *refresh = new QPushButton(QStringLiteral("Refresh"));
    refresh->setObjectName(QStringLiteral("actionButton"));
    refresh->setFocusPolicy(Qt::NoFocus);
    auto *add = new QPushButton(QStringLiteral("+  Add game"));
    add->setObjectName(QStringLiteral("primaryButton"));
    add->setFocusPolicy(Qt::NoFocus);
    top->addWidget(refresh);
    top->addWidget(add);
    layout->addLayout(top);

    auto *hero = new QFrame();
    hero->setObjectName(QStringLiteral("heroPanel"));
    hero->setMinimumHeight(190);
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(18, 16, 18, 16);
    heroLayout->setSpacing(18);

    m_heroArt = new QLabel(QStringLiteral("PS5"));
    m_heroArt->setObjectName(QStringLiteral("heroArt"));
    m_heroArt->setAlignment(Qt::AlignCenter);
    m_heroArt->setFixedSize(156, 156);
    heroLayout->addWidget(m_heroArt);

    auto *heroText = new QVBoxLayout();
    heroText->setSpacing(5);
    m_heroStatus = new QLabel(QStringLiteral("LIBRARY READY"));
    m_heroStatus->setObjectName(QStringLiteral("heroMeta"));
    m_heroTitle = new QLabel(QStringLiteral("Select a game"));
    m_heroTitle->setObjectName(QStringLiteral("heroTitle"));
    m_heroTitle->setWordWrap(true);
    m_heroPath = new QLabel(QStringLiteral("Use the D-pad / left stick to move through the library."));
    m_heroPath->setObjectName(QStringLiteral("heroPath"));
    m_heroPath->setWordWrap(true);
    heroText->addWidget(m_heroStatus);
    heroText->addWidget(m_heroTitle);
    heroText->addWidget(m_heroPath);
    heroText->addStretch(1);
    heroLayout->addLayout(heroText, 1);

    m_heroLaunch = new QPushButton(QStringLiteral("A   LAUNCH"));
    m_heroLaunch->setObjectName(QStringLiteral("heroLaunch"));
    m_heroLaunch->setFocusPolicy(Qt::NoFocus);
    m_heroLaunch->setEnabled(false);
    heroLayout->addWidget(m_heroLaunch, 0, Qt::AlignBottom);
    layout->addWidget(hero);

    auto *label = new QLabel(QStringLiteral("LIBRARY"));
    label->setObjectName(QStringLiteral("pageDescription"));
    layout->addWidget(label);

    m_libraryScroll = new QScrollArea();
    m_libraryScroll->setWidgetResizable(false);
    m_libraryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_libraryScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_libraryScroll->setFixedHeight(342);
    m_libraryContainer = new QWidget();
    m_libraryStrip = new QHBoxLayout(m_libraryContainer);
    m_libraryStrip->setContentsMargins(2, 2, 10, 7);
    m_libraryStrip->setSpacing(13);
    m_libraryScroll->setWidget(m_libraryContainer);
    layout->addWidget(m_libraryScroll);
    layout->addStretch(1);

    connect(add, &QPushButton::clicked, this, [this] { openBrowser(); });
    connect(refresh, &QPushButton::clicked, this, [this] { reloadGames(); });
    connect(m_heroLaunch, &QPushButton::clicked, this, [this] {
        if (m_hasSelectedGame) launchGame(m_selectedGame);
    });
    return page;
}

QWidget *ConsoleWindow::settingsPage(const QString &title, const QString &description, QVBoxLayout **content)
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(5);

    auto *heading = new QLabel(title);
    heading->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(heading);
    auto *desc = new QLabel(description);
    desc->setObjectName(QStringLiteral("pageDescription"));
    desc->setWordWrap(true);
    layout->addWidget(desc);

    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    auto *body = new QWidget();
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(2, 9, 8, 8);
    bodyLayout->setSpacing(12);
    scroll->setWidget(body);
    layout->addWidget(scroll, 1);
    *content = bodyLayout;
    return page;
}

QWidget *ConsoleWindow::buildGraphicsPage()
{
    QVBoxLayout *content = nullptr;
    QWidget *page = settingsPage(QStringLiteral("Graphics"),
        QStringLiteral("Rendering, display and GPU compatibility. Focused rows glow cyan; Left/Right or X changes values."), &content);

    QVBoxLayout *display = nullptr;
    auto *displayBox = settingsGroup(QStringLiteral("DISPLAY"), &display);
    addSetting(display, QStringLiteral("Resolution"), boundCombo(m_emu, QStringLiteral("resolution"), EmulatorSettings::resolutions(), QStringLiteral("1280x720")), QStringLiteral("Internal render / output size."));
    addSetting(display, QStringLiteral("Present mode"), boundCombo(m_emu, QStringLiteral("present_mode"), EmulatorSettings::presentModes(), QStringLiteral("Mailbox")), QStringLiteral("Vulkan presentation strategy."));
    addSetting(display, QStringLiteral("GPU index"), boundSpin(m_emu, QStringLiteral("gpu_index"), -1, -1, 16), QStringLiteral("-1 selects the default Vulkan device."));
    addSetting(display, QStringLiteral("VBlank frequency"), boundSpin(m_emu, QStringLiteral("vblank_frequency"), 60, 30, 360));
    addSetting(display, QStringLiteral("Fullscreen"), boundToggle(m_emu, QStringLiteral("fullscreen"), true));
    addSetting(display, QStringLiteral("HDR"), boundToggle(m_emu, QStringLiteral("hdr"), false));
    addSetting(display, QStringLiteral("Hide cursor"), boundToggle(m_emu, QStringLiteral("hide_cursor"), false));
    addSetting(display, QStringLiteral("Performance overlay"), boundToggle(m_emu, QStringLiteral("perf_overlay"), false));
    content->addWidget(displayBox);

    QVBoxLayout *compat = nullptr;
    auto *compatBox = settingsGroup(QStringLiteral("COMPATIBILITY"), &compat);
    addSetting(compat, QStringLiteral("Linear image readback"), boundToggle(m_emu, QStringLiteral("readback_linear"), false));
    addSetting(compat, QStringLiteral("Tiled image readback"), boundToggle(m_emu, QStringLiteral("readback_tiled"), false));
    addSetting(compat, QStringLiteral("Sync raw image buffers"), boundToggle(m_emu, QStringLiteral("sync_raw_images"), false));
    addSetting(compat, QStringLiteral("Tessellation"), boundToggle(m_emu, QStringLiteral("tessellation"), false));
    addSetting(compat, QStringLiteral("Occlusion queries"), boundToggle(m_emu, QStringLiteral("occlusion_queries"), false));
    content->addWidget(compatBox);
    content->addStretch(1);
    return page;
}

QWidget *ConsoleWindow::buildSystemPage()
{
    QVBoxLayout *content = nullptr;
    QWidget *page = settingsPage(QStringLiteral("System"),
        QStringLiteral("Virtual console identity, notifications, network and content paths."), &content);

    QVBoxLayout *user = nullptr;
    auto *userBox = settingsGroup(QStringLiteral("USER / CONSOLE"), &user);
    addSetting(user, QStringLiteral("User name"), boundText(m_emu, QStringLiteral("user_name"), QStringLiteral("Kyty"), 16));
    addSetting(user, QStringLiteral("User ID"), boundSpin(m_emu, QStringLiteral("user_id"), 1000, 1, 2147483000));
    addSetting(user, QStringLiteral("Console language"), boundSpin(m_emu, QStringLiteral("console_language"), 1, 0, 29), QStringLiteral("Kyty language index, 0–29."));
    addSetting(user, QStringLiteral("Microphone device"), boundText(m_emu, QStringLiteral("mic"), QString()));
    addSetting(user, QStringLiteral("Network connected"), boundToggle(m_emu, QStringLiteral("network_connected"), false));
    content->addWidget(userBox);

    QVBoxLayout *notifications = nullptr;
    auto *notificationsBox = settingsGroup(QStringLiteral("NOTIFICATIONS"), &notifications);
    addSetting(notifications, QStringLiteral("Trophy notifications"), boundToggle(m_emu, QStringLiteral("trophies"), true));
    addSetting(notifications, QStringLiteral("Trophy duration"), boundSpin(m_emu, QStringLiteral("trophy_duration"), 6, 5, 10));
    addSetting(notifications, QStringLiteral("Skip notice screen"), boundToggle(m_emu, QStringLiteral("skip_notice"), false));
    content->addWidget(notificationsBox);

    QVBoxLayout *paths = nullptr;
    auto *pathsBox = settingsGroup(QStringLiteral("CONTENT"), &paths);
    addSetting(paths, QStringLiteral("Add-ons / DLC folder"), boundText(m_emu, QStringLiteral("addons_dir"), QString()), QStringLiteral("Type or paste a path. No external dialog is opened."));
    content->addWidget(pathsBox);
    content->addStretch(1);
    return page;
}

QWidget *ConsoleWindow::buildControllerPage()
{
    QVBoxLayout *content = nullptr;
    QWidget *page = settingsPage(QStringLiteral("Controller"),
        QStringLiteral("DualSense emulation and guest input mapping."), &content);

    QVBoxLayout *pad = nullptr;
    auto *padBox = settingsGroup(QStringLiteral("DUALSENSE"), &pad);
    addSetting(pad, QStringLiteral("Lightbar color"), boundText(m_emu, QStringLiteral("controller_color"), QString()), QStringLiteral("Optional #RRGGBB value."));
    addSetting(pad, QStringLiteral("Speaker volume"), boundSpin(m_emu, QStringLiteral("controller_volume"), 50, 0, 100));
    addSetting(pad, QStringLiteral("Vibration intensity"), boundSpin(m_emu, QStringLiteral("controller_vibration"), 100, 0, 100));
    addSetting(pad, QStringLiteral("Keyboard player"), boundSpin(m_emu, QStringLiteral("keyboard_player"), 0, 0, 4));
    content->addWidget(padBox);

    QVBoxLayout *mappingRows = nullptr;
    auto *mappingBox = settingsGroup(QStringLiteral("INPUT MAPPING"), &mappingRows);
    auto *mapping = new QPlainTextEdit();
    mapping->setPlaceholderText(QStringLiteral("Cross=KeySpace\nOptions=KeyEnter"));
    mapping->setPlainText(m_emu.stringListValue(QStringLiteral("keymap")).join(QLatin1Char('\n')));
    mapping->setMinimumHeight(150);
    mapping->setFocusPolicy(Qt::StrongFocus);
    connect(mapping, &QPlainTextEdit::textChanged, mapping, [this, mapping] {
        m_emu.setValue(QStringLiteral("keymap"), mapping->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts));
    });
    addSetting(mappingRows, QStringLiteral("Key map"), mapping, QStringLiteral("One Control=Input mapping per line."));
    content->addWidget(mappingBox);
    content->addStretch(1);
    return page;
}

QWidget *ConsoleWindow::buildDebugPage()
{
    QVBoxLayout *content = nullptr;
    QWidget *page = settingsPage(QStringLiteral("Debug"),
        QStringLiteral("Validation, shader logging and capture options. These can reduce performance heavily."), &content);

    QVBoxLayout *validation = nullptr;
    auto *validationBox = settingsGroup(QStringLiteral("VALIDATION"), &validation);
    addSetting(validation, QStringLiteral("Vulkan validation"), boundToggle(m_emu, QStringLiteral("vulkan_validation"), false));
    addSetting(validation, QStringLiteral("Shader validation"), boundToggle(m_emu, QStringLiteral("shader_validation"), false));
    addSetting(validation, QStringLiteral("Shader optimization"), boundCombo(m_emu, QStringLiteral("shader_optimization"), EmulatorSettings::shaderOptimizationModes(), QStringLiteral("Performance")));
    content->addWidget(validationBox);

    QVBoxLayout *logging = nullptr;
    auto *loggingBox = settingsGroup(QStringLiteral("LOGGING / DUMPS"), &logging);
    addSetting(logging, QStringLiteral("Shader log"), boundCombo(m_emu, QStringLiteral("shader_log_direction"), EmulatorSettings::logDirections(), QStringLiteral("Silent")));
    addSetting(logging, QStringLiteral("Shader log folder"), boundText(m_emu, QStringLiteral("shader_log_folder"), QStringLiteral("_Shaders")));
    addSetting(logging, QStringLiteral("Command buffer dump"), boundToggle(m_emu, QStringLiteral("command_buffer_dump"), false));
    addSetting(logging, QStringLiteral("Command buffer folder"), boundText(m_emu, QStringLiteral("command_buffer_dump_folder"), QStringLiteral("_Buffers")));
    addSetting(logging, QStringLiteral("Guest printf"), boundCombo(m_emu, QStringLiteral("printf_direction"), EmulatorSettings::logDirections(), QStringLiteral("Silent")));
    addSetting(logging, QStringLiteral("Guest printf file"), boundText(m_emu, QStringLiteral("printf_output_file"), QStringLiteral("_kyty.txt")));
    content->addWidget(loggingBox);

    QVBoxLayout *tools = nullptr;
    auto *toolsBox = settingsGroup(QStringLiteral("TOOLS"), &tools);
    addSetting(tools, QStringLiteral("Profiler / Tracy"), boundToggle(m_emu, QStringLiteral("profiler"), false));
    addSetting(tools, QStringLiteral("RenderDoc"), boundToggle(m_emu, QStringLiteral("renderdoc"), false));
    addSetting(tools, QStringLiteral("AMD CPU patch"), boundToggle(m_emu, QStringLiteral("amd_cpu"), false));
    content->addWidget(toolsBox);
    content->addStretch(1);
    return page;
}

QWidget *ConsoleWindow::buildExpertPage()
{
    QVBoxLayout *content = nullptr;
    QWidget *page = settingsPage(QStringLiteral("Expert"),
        QStringLiteral("Additional Kyty core flags. Leave these off unless a game or debugging workflow requires them."), &content);

    QVBoxLayout *expert = nullptr;
    auto *expertBox = settingsGroup(QStringLiteral("CORE FLAGS"), &expert);
    addSetting(expert, QStringLiteral("Disable startup splash"), boundToggle(m_emu, QStringLiteral("no_splash"), false));
    addSetting(expert, QStringLiteral("Virtual VR headset"), boundToggle(m_emu, QStringLiteral("vr"), false));
    addSetting(expert, QStringLiteral("GPU assisted validation"), boundToggle(m_emu, QStringLiteral("gpu_assisted_validation"), false));
    addSetting(expert, QStringLiteral("SPIR-V debug printf"), boundToggle(m_emu, QStringLiteral("spirv_debug_printf"), false));
    addSetting(expert, QStringLiteral("Graphics debug dump"), boundToggle(m_emu, QStringLiteral("graphics_debug_dump"), false));
    addSetting(expert, QStringLiteral("PlayGo hack"), boundToggle(m_emu, QStringLiteral("playgo_hack"), false));
    content->addWidget(expertBox);

    auto *reset = new QPushButton(QStringLiteral("Reset emulator settings to defaults"));
    reset->setObjectName(QStringLiteral("actionButton"));
    reset->setFocusPolicy(Qt::StrongFocus);
    connect(reset, &QPushButton::clicked, this, [this] {
        m_settings.beginGroup(QStringLiteral("emu"));
        m_settings.remove(QString());
        m_settings.endGroup();
        m_emu.ensureDefaults();
        m_statusLabel->setText(QStringLiteral("Defaults restored. Reopen KytyPS5 to refresh all visible controls."));
    });
    content->addWidget(reset);
    content->addStretch(1);
    return page;
}

QWidget *ConsoleWindow::buildBrowserPage()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 18, 24, 16);
    layout->setSpacing(12);

    auto *header = new QHBoxLayout();
    auto *mark = new QLabel(QStringLiteral("+"));
    mark->setObjectName(QStringLiteral("brandMark"));
    mark->setAlignment(Qt::AlignCenter);
    header->addWidget(mark);
    auto *titles = new QVBoxLayout();
    auto *title = new QLabel(QStringLiteral("Add a PS5 game"));
    title->setObjectName(QStringLiteral("brandTitle"));
    auto *subtitle = new QLabel(QStringLiteral("Built-in file browser · no external dialogs"));
    subtitle->setObjectName(QStringLiteral("brandSubtitle"));
    titles->addWidget(title);
    titles->addWidget(subtitle);
    header->addLayout(titles);
    header->addStretch(1);
    layout->addLayout(header);

    m_browserPath = new QLabel();
    m_browserPath->setObjectName(QStringLiteral("browserPath"));
    m_browserPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_browserPath);

    auto *explain = new QLabel(QStringLiteral(
        "Open folders with A. If the current folder contains eboot.bin, an ADD THIS GAME FOLDER row appears. "
        "ZIP files are extracted into Internal storage/KytyPS5; .zar, ELF and eboot.bin can be added directly."));
    explain->setObjectName(QStringLiteral("pageDescription"));
    explain->setWordWrap(true);
    layout->addWidget(explain);

    m_browserList = new QListWidget();
    m_browserList->setObjectName(QStringLiteral("browserList"));
    m_browserList->setFocusPolicy(Qt::StrongFocus);
    m_browserList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    layout->addWidget(m_browserList, 1);

    m_browserStatus = new QLabel(QStringLiteral("A Open / Add  ·  B Parent / Back  ·  X Add current folder  ·  Y Internal storage"));
    m_browserStatus->setObjectName(QStringLiteral("browserStatus"));
    m_browserStatus->setProperty("error", false);
    layout->addWidget(m_browserStatus);

    auto *hint = new QLabel(QStringLiteral("A  OPEN / ADD     B  BACK     X  ADD FOLDER     Y  STORAGE ROOT"));
    hint->setObjectName(QStringLiteral("hintBar"));
    hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(hint);

    connect(m_browserList, &QListWidget::itemActivated, this, [this](QListWidgetItem *) { activateBrowserEntry(); });
    return page;
}

QWidget *ConsoleWindow::buildErrorPage()
{
    auto *page = new QWidget();
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(40, 28, 40, 28);
    outer->addStretch(1);

    auto *panel = new QFrame();
    panel->setObjectName(QStringLiteral("errorPanel"));
    panel->setMaximumWidth(980);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(12);

    m_errorTitle = new QLabel(QStringLiteral("KYTY CORE ERROR"));
    m_errorTitle->setObjectName(QStringLiteral("errorTitle"));
    m_errorSummary = new QLabel();
    m_errorSummary->setObjectName(QStringLiteral("errorSummary"));
    m_errorSummary->setWordWrap(true);
    auto *desc = new QLabel(QStringLiteral("The launcher kept the error inside this window. Full core output is shown below."));
    desc->setObjectName(QStringLiteral("pageDescription"));
    desc->setWordWrap(true);
    m_errorDetails = new QPlainTextEdit();
    m_errorDetails->setObjectName(QStringLiteral("errorDetails"));
    m_errorDetails->setReadOnly(true);
    m_errorDetails->setMinimumHeight(300);
    m_errorBack = new QPushButton(QStringLiteral("B   Back to library"));
    m_errorBack->setObjectName(QStringLiteral("primaryButton"));
    m_errorBack->setFocusPolicy(Qt::StrongFocus);

    layout->addWidget(m_errorTitle);
    layout->addWidget(m_errorSummary);
    layout->addWidget(desc);
    layout->addWidget(m_errorDetails, 1);
    layout->addWidget(m_errorBack, 0, Qt::AlignRight);

    auto *center = new QHBoxLayout();
    center->addStretch(1);
    center->addWidget(panel, 1);
    center->addStretch(1);
    outer->addLayout(center, 1);
    outer->addStretch(1);

    connect(m_errorBack, &QPushButton::clicked, this, [this] { closeErrorPage(); });
    return page;
}

void ConsoleWindow::reloadGames()
{
    while (QLayoutItem *item = m_libraryStrip->takeAt(0)) {
        if (QWidget *widget = item->widget()) widget->deleteLater();
        delete item;
    }
    m_libraryButtons.clear();
    m_games = loadGameLibrary(m_settings);
    m_hasSelectedGame = false;

    for (const GameEntry &game : m_games) {
        QPushButton *card = createGameCard(game);
        m_libraryButtons.push_back(card);
        m_libraryStrip->addWidget(card);
    }

    QPushButton *add = createAddGameCard();
    m_libraryButtons.push_back(add);
    m_libraryStrip->addWidget(add);
    m_libraryStrip->addStretch(1);

    const int width = std::max(1, m_libraryButtons.size()) * 263 + 20;
    m_libraryContainer->setMinimumSize(width, 325);

    if (!m_games.isEmpty()) {
        selectGame(m_games.first());
        m_statusLabel->setText(QStringLiteral("%1 game(s) ready · Kyty core").arg(m_games.size()));
    } else {
        m_heroArt->setPixmap(QPixmap());
        m_heroArt->setText(QStringLiteral("PS5"));
        m_heroTitle->setText(QStringLiteral("No games yet"));
        m_heroStatus->setText(QStringLiteral("LIBRARY EMPTY"));
        m_heroPath->setText(QStringLiteral("Select + Add game or press Y to browse Android storage inside this window."));
        m_heroLaunch->setEnabled(false);
        m_statusLabel->setText(QStringLiteral("No games found"));
    }

    if (m_viewMode == ViewMode::Shell && m_sections->currentRow() == 0)
        QTimer::singleShot(0, this, [this] { focusLibraryButton(0); });
}

QPushButton *ConsoleWindow::createGameCard(const GameEntry &game)
{
    auto *card = new GameCardButton(game, [this, game] {
        selectGame(game);
        if (m_libraryScroll) m_libraryScroll->ensureWidgetVisible(QApplication::focusWidget(), 28, 10);
    });
    connect(card, &QPushButton::clicked, this, [this, game] {
        selectGame(game);
        launchGame(game);
    });
    return card;
}

QPushButton *ConsoleWindow::createAddGameCard()
{
    auto *button = new QPushButton(QStringLiteral("+\n\nADD GAME\n\nBrowse folders, ZIP, ZAR or ELF"));
    button->setObjectName(QStringLiteral("addGameCard"));
    button->setFocusPolicy(Qt::StrongFocus);
    button->setFixedSize(250, 310);
    connect(button, &QPushButton::clicked, this, [this] { openBrowser(); });
    return button;
}

void ConsoleWindow::selectGame(const GameEntry &game)
{
    m_selectedGame = game;
    m_hasSelectedGame = true;
    m_heroTitle->setText(game.title);
    m_heroStatus->setText(QStringLiteral("READY  ·  KYTY CORE  ·  PS5"));
    m_heroPath->setText(game.target);
    m_heroLaunch->setEnabled(true);

    QPixmap pixmap(game.icon);
    if (!pixmap.isNull()) {
        m_heroArt->setText(QString());
        m_heroArt->setPixmap(pixmap.scaled(148, 148, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        m_heroArt->setPixmap(QPixmap());
        m_heroArt->setText(QStringLiteral("PS5"));
    }
}

int ConsoleWindow::focusedLibraryIndex() const
{
    QWidget *focused = QApplication::focusWidget();
    for (int i = 0; i < m_libraryButtons.size(); ++i) {
        if (m_libraryButtons.at(i) == focused) return i;
    }
    return -1;
}

void ConsoleWindow::focusLibraryButton(int index)
{
    if (m_libraryButtons.isEmpty()) return;
    index = std::clamp(index, 0, m_libraryButtons.size() - 1);
    QPushButton *button = m_libraryButtons.at(index);
    button->setFocus(Qt::OtherFocusReason);
    m_libraryScroll->ensureWidgetVisible(button, 28, 10);
}

void ConsoleWindow::openBrowser()
{
    m_viewMode = ViewMode::Browser;
    QString start = pickerStart(m_settings);
    if (!QDir(start).exists()) start = QStringLiteral("/storage/emulated/0");
    populateBrowser(start);
    m_rootStack->setCurrentWidget(m_browserPage);
    m_browserList->setFocus(Qt::OtherFocusReason);
}

void ConsoleWindow::closeBrowser()
{
    m_viewMode = ViewMode::Shell;
    m_rootStack->setCurrentWidget(m_shellPage);
    m_sections->setCurrentRow(0);
    QTimer::singleShot(0, this, [this] { focusLibraryButton(0); });
    updateHints();
}

bool ConsoleWindow::currentBrowserFolderIsGame() const
{
    return QFileInfo(QDir(m_browserDir).filePath(QStringLiteral("eboot.bin"))).isFile();
}

void ConsoleWindow::populateBrowser(const QString &directory)
{
    QDir dir(directory);
    if (!dir.exists()) {
        showBrowserStatus(QStringLiteral("Folder is no longer available."), true);
        return;
    }

    m_browserDir = dir.absolutePath();
    m_settings.setValue(QStringLiteral("lastPickerDir"), m_browserDir);
    m_browserPath->setText(m_browserDir);
    m_browserList->clear();

    auto addItem = [this](const QString &text, BrowserEntryKind kind, const QString &path) {
        auto *item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, static_cast<int>(kind));
        item->setData(Qt::UserRole + 1, path);
        m_browserList->addItem(item);
    };

    const QString storageRoot = QStringLiteral("/storage/emulated/0");
    if (m_browserDir != storageRoot && QDir(m_browserDir).cdUp()) {
        QDir parent(m_browserDir);
        parent.cdUp();
        addItem(QStringLiteral("←   PARENT FOLDER"), BrowserEntryKind::Parent, parent.absolutePath());
    }

    if (currentBrowserFolderIsGame()) {
        addItem(QStringLiteral("＋   ADD THIS GAME FOLDER    ·    eboot.bin found"),
                BrowserEntryKind::AddCurrent, m_browserDir);
    }

    const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : dirs)
        addItem(QStringLiteral("DIR    %1").arg(info.fileName()), BrowserEntryKind::Directory, info.absoluteFilePath());

    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : files) {
        const QString suffix = info.suffix().toLower();
        if (suffix == QStringLiteral("zip")) {
            addItem(QStringLiteral("ZIP    %1    ·    import & extract").arg(info.fileName()), BrowserEntryKind::Zip, info.absoluteFilePath());
        } else if (supportedDirectFile(info)) {
            addItem(QStringLiteral("GAME   %1").arg(info.fileName()), BrowserEntryKind::DirectGame, info.absoluteFilePath());
        }
    }

    if (m_browserList->count() == 0) {
        addItem(QStringLiteral("No supported games or folders here"), BrowserEntryKind::Directory, m_browserDir);
        m_browserList->item(0)->setFlags(Qt::NoItemFlags);
    }
    m_browserList->setCurrentRow(0);
    showBrowserStatus(currentBrowserFolderIsGame()
        ? QStringLiteral("Game folder detected. Press X, or select ADD THIS GAME FOLDER.")
        : QStringLiteral("A opens folders or imports supported files. B goes back."));
}

void ConsoleWindow::activateBrowserEntry()
{
    QListWidgetItem *item = m_browserList->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) return;

    const auto kind = static_cast<BrowserEntryKind>(item->data(Qt::UserRole).toInt());
    const QString path = item->data(Qt::UserRole + 1).toString();
    QString error;

    switch (kind) {
    case BrowserEntryKind::Parent:
    case BrowserEntryKind::Directory:
        populateBrowser(path);
        m_browserList->setFocus(Qt::OtherFocusReason);
        return;
    case BrowserEntryKind::AddCurrent:
        if (!addCustomSource(m_settings, path, &error)) {
            showBrowserStatus(error, true);
            return;
        }
        reloadGames();
        closeBrowser();
        return;
    case BrowserEntryKind::Zip:
        showBrowserStatus(QStringLiteral("Importing %1 …").arg(QFileInfo(path).fileName()));
        QApplication::processEvents();
        if (!importZipGame(path, &error)) {
            showBrowserStatus(error, true);
            return;
        }
        reloadGames();
        closeBrowser();
        return;
    case BrowserEntryKind::DirectGame:
        if (!addCustomSource(m_settings, path, &error)) {
            showBrowserStatus(error, true);
            return;
        }
        reloadGames();
        closeBrowser();
        return;
    }
}

void ConsoleWindow::addCurrentBrowserFolder()
{
    if (!currentBrowserFolderIsGame()) {
        showBrowserStatus(QStringLiteral("This folder does not contain eboot.bin."), true);
        return;
    }
    QString error;
    if (!addCustomSource(m_settings, m_browserDir, &error)) {
        showBrowserStatus(error, true);
        return;
    }
    reloadGames();
    closeBrowser();
}

void ConsoleWindow::showBrowserStatus(const QString &text, bool error)
{
    m_browserStatus->setText(text);
    m_browserStatus->setProperty("error", error);
    m_browserStatus->style()->unpolish(m_browserStatus);
    m_browserStatus->style()->polish(m_browserStatus);
}

void ConsoleWindow::showErrorPage(const QString &title, const QString &summary, const QString &details)
{
    m_errorTitle->setText(title.toUpper());
    m_errorSummary->setText(summary);
    m_errorDetails->setPlainText(details);
    m_errorDetails->verticalScrollBar()->setValue(0);
    m_viewMode = ViewMode::Error;
    m_rootStack->setCurrentWidget(m_errorPage);
    m_errorBack->setFocus(Qt::OtherFocusReason);
}

void ConsoleWindow::closeErrorPage()
{
    m_viewMode = ViewMode::Shell;
    m_rootStack->setCurrentWidget(m_shellPage);
    m_sections->setCurrentRow(0);
    QTimer::singleShot(0, this, [this] { focusLibraryButton(0); });
    updateHints();
}

void ConsoleWindow::launchGame(const GameEntry &game)
{
    const QString emulator = QDir(kytyDir()).filePath(QStringLiteral("kyty_emulator"));
    if (!QFileInfo::exists(emulator)) {
        showErrorPage(QStringLiteral("Core missing"),
                      QStringLiteral("kyty_emulator was not found inside the AppImage."), emulator);
        return;
    }
    startTrackedProcess(emulator, m_emu.buildArgs(game.target), QStringLiteral("game"));
}

void ConsoleWindow::startTrackedProcess(const QString &program, const QStringList &args, const QString &kind)
{
    if (m_childPid > 0) {
        m_statusLabel->setText(QStringLiteral("Kyty core is already running."));
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString logFile = QDir(logsRoot()).filePath(QStringLiteral("%1-%2.log").arg(kind, stamp));
    const SpawnResult result = spawnWithLog(program, args, logFile);
    if (!result.ok) {
        showErrorPage(QStringLiteral("Launch failed"), result.error, QStringLiteral("Log: %1").arg(logFile));
        return;
    }

    m_childPid = result.pid;
    m_childLog = result.logFile;
    m_childKind = kind;
    m_childTicks = 0;
    m_statusLabel->setText(QStringLiteral("Starting Kyty core · PID %1").arg(result.pid));
    m_processTimer->start();
    hide();
}

void ConsoleWindow::pollChild()
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
        showErrorPage(QStringLiteral("Process tracking failed"),
                      QStringLiteral("The launcher could not read the Kyty core process state."),
                      QString::fromLocal8Bit(std::strerror(errno)));
        return;
    }

    QString summary;
    int exitCode = 0;
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
        m_statusLabel->setText(summary);
        m_viewMode = ViewMode::Shell;
        m_rootStack->setCurrentWidget(m_shellPage);
        return;
    }

    if (output.contains(QStringLiteral("could not reserve 13824 MB"), Qt::CaseInsensitive) ||
        (output.contains(QStringLiteral("could not reserve"), Qt::CaseInsensitive) &&
         output.contains(QStringLiteral("guest direct memory"), Qt::CaseInsensitive))) {
        showErrorPage(QStringLiteral("Kyty memory mapping"),
            QStringLiteral("The Kyty core cannot reserve its 13.5 GB PS5 direct-memory alias under the current DroidDeck/FEX runtime."),
            output + QStringLiteral("\n\nThis happens before the game boots. The game path and library entry are valid; the blocker is the core/runtime virtual-memory mapping."));
        return;
    }

    showErrorPage(QStringLiteral("Kyty core error"), summary,
                  output.isEmpty() ? QStringLiteral("No output was written.\nLog: %1").arg(m_childLog) : output);
}

void ConsoleWindow::switchSection(int delta)
{
    if (m_viewMode != ViewMode::Shell) return;
    int row = m_sections->currentRow();
    if (row < 0) row = 0;
    row = (row + delta + m_sections->count()) % m_sections->count();
    m_sections->setCurrentRow(row);
}

void ConsoleWindow::focusFirstControl()
{
    QWidget *page = m_sectionStack->currentWidget();
    if (!page) return;
    QWidget *candidate = page;
    for (int i = 0; i < 512; ++i) {
        candidate = candidate->nextInFocusChain();
        if (!candidate || candidate == page) break;
        if (candidate->isVisible() && candidate->isEnabled() && candidate->focusPolicy() != Qt::NoFocus) {
            candidate->setFocus(Qt::OtherFocusReason);
            ensureFocusedVisible(candidate);
            return;
        }
    }
}

void ConsoleWindow::ensureFocusedVisible(QWidget *widget)
{
    if (!widget) return;
    for (QWidget *parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
        if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
            scroll->ensureWidgetVisible(widget, 25, 25);
            return;
        }
    }
}

void ConsoleWindow::moveFocus(bool forward)
{
    QWidget *focused = QApplication::focusWidget();
    if (!focused) {
        focusFirstControl();
        return;
    }
    QWidget *candidate = focused;
    for (int i = 0; i < 512; ++i) {
        candidate = forward ? candidate->nextInFocusChain() : candidate->previousInFocusChain();
        if (!candidate || candidate == focused) break;
        if (candidate->isVisible() && candidate->isEnabled() && candidate->focusPolicy() != Qt::NoFocus) {
            candidate->setFocus(Qt::OtherFocusReason);
            ensureFocusedVisible(candidate);
            return;
        }
    }
}

void ConsoleWindow::cycleFocusedValue()
{
    QWidget *focused = QApplication::focusWidget();
    if (!focused) return;
    if (auto *combo = qobject_cast<QComboBox *>(focused)) {
        if (combo->count() > 0) combo->setCurrentIndex((combo->currentIndex() + 1) % combo->count());
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

void ConsoleWindow::adjustFocusedValue(int delta)
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

void ConsoleWindow::handleGamepad(PadAction action)
{
    if (m_childPid > 0 || !isVisible()) return;

    if (m_viewMode == ViewMode::Browser) {
        if (action == PadAction::Up || action == PadAction::Left) {
            m_browserList->setCurrentRow(std::max(0, m_browserList->currentRow() - 1));
        } else if (action == PadAction::Down || action == PadAction::Right) {
            m_browserList->setCurrentRow(std::min(m_browserList->count() - 1, m_browserList->currentRow() + 1));
        } else if (action == PadAction::Confirm) {
            activateBrowserEntry();
        } else if (action == PadAction::Back) {
            const QString root = QStringLiteral("/storage/emulated/0");
            if (m_browserDir != root) {
                QDir dir(m_browserDir);
                if (dir.cdUp() && dir.absolutePath().startsWith(root)) populateBrowser(dir.absolutePath());
                else closeBrowser();
            } else {
                closeBrowser();
            }
        } else if (action == PadAction::NextValue) {
            addCurrentBrowserFolder();
        } else if (action == PadAction::AddGame) {
            populateBrowser(QStringLiteral("/storage/emulated/0"));
        }
        return;
    }

    if (m_viewMode == ViewMode::Error) {
        if (action == PadAction::Back || action == PadAction::Confirm) closeErrorPage();
        return;
    }

    if (action == PadAction::PrevSection) {
        switchSection(-1);
        return;
    }
    if (action == PadAction::NextSection) {
        switchSection(1);
        return;
    }
    if (action == PadAction::AddGame) {
        openBrowser();
        return;
    }
    if (action == PadAction::Refresh) {
        reloadGames();
        return;
    }

    const bool library = m_sections->currentRow() == 0;
    if (library) {
        int index = focusedLibraryIndex();
        if (index < 0) index = 0;
        if (action == PadAction::Left || action == PadAction::Up) {
            focusLibraryButton(std::max(0, index - 1));
        } else if (action == PadAction::Right || action == PadAction::Down) {
            focusLibraryButton(std::min(m_libraryButtons.size() - 1, index + 1));
        } else if (action == PadAction::Confirm) {
            if (index >= 0 && index < m_libraryButtons.size()) m_libraryButtons.at(index)->click();
        } else if (action == PadAction::NextValue) {
            m_sections->setCurrentRow(1);
        }
        return;
    }

    if (action == PadAction::Back) {
        m_sections->setCurrentRow(0);
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
        else if (auto *combo = qobject_cast<QComboBox *>(focused)) {
            if (combo->count() > 0) combo->setCurrentIndex((combo->currentIndex() + 1) % combo->count());
        } else if (auto *edit = qobject_cast<QLineEdit *>(focused)) {
            edit->selectAll();
            QApplication::inputMethod()->show();
        }
    }
}

void ConsoleWindow::updateHints()
{
    if (!m_hintLabel) return;
    if (m_sections && m_sections->currentRow() == 0) {
        m_hintLabel->setText(QStringLiteral(
            "A  LAUNCH / SELECT     B  BACK     X  GAME SETTINGS     Y  ADD GAME     LB/RB  TABS     VIEW  REFRESH"));
    } else {
        m_hintLabel->setText(QStringLiteral(
            "A  SELECT / TOGGLE     B  LIBRARY     X  NEXT VALUE     ←/→  CHANGE     ↑/↓  MOVE     LB/RB  TABS"));
    }
}
