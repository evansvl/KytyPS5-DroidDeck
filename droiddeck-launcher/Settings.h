#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>

class EmulatorSettings final
{
public:
    explicit EmulatorSettings(QSettings &settings)
        : m_settings(settings)
    {
    }

    void ensureDefaults();
    QStringList buildArgs(const QString &gameTarget) const;

    static QStringList resolutions();
    static QStringList presentModes();
    static QStringList shaderOptimizationModes();
    static QStringList logDirections();

    QString stringValue(const QString &key, const QString &fallback = {}) const;
    int intValue(const QString &key, int fallback = 0) const;
    bool boolValue(const QString &key, bool fallback = false) const;
    QStringList stringListValue(const QString &key) const;

    void setValue(const QString &key, const QVariant &value);

private:
    QSettings &m_settings;
};
