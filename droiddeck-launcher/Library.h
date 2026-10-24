#pragma once

#include <QList>
#include <QSettings>
#include <QString>

struct GameEntry {
    QString target;
    QString title;
    QString icon;
};

QString gamesRoot();
QString kytyDir();
QString unzipProgram();
QString logsRoot();
QString pickerStart(QSettings &settings);

QList<GameEntry> loadGameLibrary(QSettings &settings);
bool addCustomSource(QSettings &settings, const QString &path, QString *error = nullptr);
bool importZipGame(const QString &zipPath, QString *error = nullptr);
