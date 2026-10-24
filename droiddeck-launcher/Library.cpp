#include "Library.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <filesystem>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

QString gamesRoot()
{
    const QByteArray env = qgetenv("KYTY_GAMES_DIR");
    if (!env.isEmpty()) return QString::fromUtf8(env);
    return QStringLiteral("/storage/emulated/0/KytyPS5");
}

QString kytyDir()
{
    const QByteArray env = qgetenv("KYTY_DIR");
    if (!env.isEmpty()) return QString::fromUtf8(env);
    return QCoreApplication::applicationDirPath();
}

QString unzipProgram()
{
    const QByteArray env = qgetenv("KYTY_UNZIP");
    if (!env.isEmpty()) return QString::fromUtf8(env);
    return QStringLiteral("/usr/bin/unzip");
}

QString logsRoot()
{
    const QString path = QDir::homePath() + QStringLiteral("/.local/share/KytyPS5-DroidDeck/logs");
    QDir().mkpath(path);
    return path;
}

QString pickerStart(QSettings &settings)
{
    const QString saved = settings.value(QStringLiteral("lastPickerDir")).toString();
    if (!saved.isEmpty() && QDir(saved).exists()) return saved;
    const QString downloads = QStringLiteral("/storage/emulated/0/Download");
    if (QDir(downloads).exists()) return downloads;
    const QString storage = QStringLiteral("/storage/emulated/0");
    return QDir(storage).exists() ? storage : QDir::homePath();
}

static GameEntry folderEntry(const QString &directory)
{
    QFileInfo info(directory);
    GameEntry game;
    game.target = info.absoluteFilePath();
    game.title = info.fileName().isEmpty() ? game.target : info.fileName();
    const QString icon = QDir(game.target).filePath(QStringLiteral("sce_sys/icon0.png"));
    if (QFileInfo::isFile(icon)) game.icon = icon;
    return game;
}

static GameEntry fileEntry(const QString &file)
{
    QFileInfo info(file);
    GameEntry game;
    game.target = info.absoluteFilePath();
    game.title = info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
    return game;
}

static QList<GameEntry> scanDirectory(const QString &root)
{
    QList<GameEntry> result;
    std::error_code ec;
    const fs::path base(root.toStdString());
    if (!fs::exists(base, ec) || !fs::is_directory(base, ec)) return result;

    fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    while (it != end) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }
        const auto entry = *it;
        if (entry.is_regular_file(ec) && entry.path().filename() == "eboot.bin") {
            result.push_back(folderEntry(QString::fromStdString(entry.path().parent_path().string())));
            it.disable_recursion_pending();
        }
        it.increment(ec);
    }
    return result;
}

static QString canonicalKey(const GameEntry &game)
{
    QFileInfo info(game.target);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

static void appendUnique(QList<GameEntry> &dst, const QList<GameEntry> &src)
{
    QStringList keys;
    for (const auto &game : dst) keys << canonicalKey(game);
    for (const auto &game : src) {
        const QString key = canonicalKey(game);
        if (!keys.contains(key)) {
            dst.push_back(game);
            keys << key;
        }
    }
}

static bool isDirectGameFile(const QString &path)
{
    QFileInfo info(path);
    if (!info.isFile()) return false;
    const QString suffix = info.suffix().toLower();
    return suffix == QStringLiteral("zar") || suffix == QStringLiteral("elf") ||
           info.fileName().compare(QStringLiteral("eboot.bin"), Qt::CaseInsensitive) == 0;
}

QList<GameEntry> loadGameLibrary(QSettings &settings)
{
    QList<GameEntry> games = scanDirectory(gamesRoot());
    const QStringList sources = settings.value(QStringLiteral("customSources")).toStringList();
    for (const QString &source : sources) {
        QFileInfo info(source);
        if (!info.exists()) continue;
        if (info.isDir()) appendUnique(games, scanDirectory(info.absoluteFilePath()));
        else if (isDirectGameFile(source)) appendUnique(games, {fileEntry(source)});
    }
    std::sort(games.begin(), games.end(), [](const GameEntry &a, const GameEntry &b) {
        return a.title.localeAwareCompare(b.title) < 0;
    });
    return games;
}

bool addCustomSource(QSettings &settings, const QString &path, QString *error)
{
    QFileInfo info(path);
    if (!info.exists()) {
        if (error) *error = QStringLiteral("Selected path no longer exists.");
        return false;
    }

    bool valid = false;
    if (info.isDir()) valid = !scanDirectory(info.absoluteFilePath()).isEmpty();
    else valid = isDirectGameFile(info.absoluteFilePath());

    if (!valid) {
        if (error) *error = QStringLiteral("No PS5 game was detected. Select a folder containing eboot.bin, a .zar archive, an ELF, or eboot.bin itself.");
        return false;
    }

    QStringList sources = settings.value(QStringLiteral("customSources")).toStringList();
    const QString absolute = info.absoluteFilePath();
    if (!sources.contains(absolute)) {
        sources << absolute;
        settings.setValue(QStringLiteral("customSources"), sources);
    }
    settings.setValue(QStringLiteral("lastPickerDir"), info.isDir() ? absolute : info.absolutePath());
    return true;
}

bool importZipGame(const QString &zipPath, QString *error)
{
    QFileInfo zip(zipPath);
    if (!zip.isFile() || zip.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) != 0) {
        if (error) *error = QStringLiteral("The selected file is not a ZIP archive.");
        return false;
    }

    QDir().mkpath(gamesRoot());
    const QByteArray program = unzipProgram().toUtf8();
    const QByteArray archive = zip.absoluteFilePath().toUtf8();
    const QByteArray destination = gamesRoot().toUtf8();

    const pid_t pid = fork();
    if (pid < 0) {
        if (error) *error = QStringLiteral("fork() failed while starting unzip.");
        return false;
    }
    if (pid == 0) {
        execl(program.constData(), program.constData(), "-o", archive.constData(), "-d", destination.constData(), static_cast<char *>(nullptr));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (error) *error = QStringLiteral("ZIP extraction failed. The archive may be damaged or use an unsupported layout.");
        return false;
    }
    return true;
}
