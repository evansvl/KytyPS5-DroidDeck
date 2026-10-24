#include "Settings.h"

#include <QFileInfo>

namespace {
constexpr auto P = "emu/";

void addFlag(QStringList &args, bool enabled, const char *name)
{
    if (enabled) args << QString::fromLatin1(name);
}
}

void EmulatorSettings::ensureDefaults()
{
    const auto put = [this](const QString &key, const QVariant &value) {
        const QString full = QString::fromLatin1(P) + key;
        if (!m_settings.contains(full)) m_settings.setValue(full, value);
    };

    put(QStringLiteral("resolution"), QStringLiteral("1280x720"));
    put(QStringLiteral("user_name"), QStringLiteral("Kyty"));
    put(QStringLiteral("user_id"), 1000);
    put(QStringLiteral("mic"), QString());
    put(QStringLiteral("controller_color"), QString());
    put(QStringLiteral("controller_volume"), 50);
    put(QStringLiteral("controller_vibration"), 100);
    put(QStringLiteral("keyboard_player"), 0);
    put(QStringLiteral("present_mode"), QStringLiteral("Mailbox"));
    put(QStringLiteral("gpu_index"), -1);
    put(QStringLiteral("fullscreen"), true);
    put(QStringLiteral("hdr"), false);
    put(QStringLiteral("hide_cursor"), false);
    put(QStringLiteral("readback_linear"), false);
    put(QStringLiteral("readback_tiled"), false);
    put(QStringLiteral("sync_raw_images"), false);
    put(QStringLiteral("tessellation"), false);
    put(QStringLiteral("occlusion_queries"), false);
    put(QStringLiteral("trophies"), true);
    put(QStringLiteral("trophy_duration"), 6);
    put(QStringLiteral("perf_overlay"), false);
    put(QStringLiteral("skip_notice"), false);
    put(QStringLiteral("network_connected"), false);
    put(QStringLiteral("vblank_frequency"), 60);
    put(QStringLiteral("console_language"), 1);
    put(QStringLiteral("vulkan_validation"), false);
    put(QStringLiteral("shader_validation"), false);
    put(QStringLiteral("shader_optimization"), QStringLiteral("Performance"));
    put(QStringLiteral("shader_log_direction"), QStringLiteral("Silent"));
    put(QStringLiteral("shader_log_folder"), QStringLiteral("_Shaders"));
    put(QStringLiteral("command_buffer_dump"), false);
    put(QStringLiteral("command_buffer_dump_folder"), QStringLiteral("_Buffers"));
    put(QStringLiteral("printf_direction"), QStringLiteral("Silent"));
    put(QStringLiteral("printf_output_file"), QStringLiteral("_kyty.txt"));
    put(QStringLiteral("profiler"), false);
    put(QStringLiteral("renderdoc"), false);
    put(QStringLiteral("amd_cpu"), false);
    put(QStringLiteral("keymap"), QStringList {});
    put(QStringLiteral("addons_dir"), QString());

    // Emulator CLI options not exposed in the old desktop GUI, kept in Expert.
    put(QStringLiteral("no_splash"), false);
    put(QStringLiteral("vr"), false);
    put(QStringLiteral("gpu_assisted_validation"), false);
    put(QStringLiteral("spirv_debug_printf"), false);
    put(QStringLiteral("graphics_debug_dump"), false);
    put(QStringLiteral("playgo_hack"), false);
}

QStringList EmulatorSettings::resolutions()
{
    return {QStringLiteral("1280x720"), QStringLiteral("1920x1080"),
            QStringLiteral("2560x1440"), QStringLiteral("3840x2160")};
}

QStringList EmulatorSettings::presentModes()
{
    return {QStringLiteral("Fifo"), QStringLiteral("Mailbox"), QStringLiteral("Immediate")};
}

QStringList EmulatorSettings::shaderOptimizationModes()
{
    return {QStringLiteral("None"), QStringLiteral("Size"), QStringLiteral("Performance")};
}

QStringList EmulatorSettings::logDirections()
{
    return {QStringLiteral("Silent"), QStringLiteral("Console"), QStringLiteral("File")};
}

QString EmulatorSettings::stringValue(const QString &key, const QString &fallback) const
{
    return m_settings.value(QString::fromLatin1(P) + key, fallback).toString();
}

int EmulatorSettings::intValue(const QString &key, int fallback) const
{
    return m_settings.value(QString::fromLatin1(P) + key, fallback).toInt();
}

bool EmulatorSettings::boolValue(const QString &key, bool fallback) const
{
    return m_settings.value(QString::fromLatin1(P) + key, fallback).toBool();
}

QStringList EmulatorSettings::stringListValue(const QString &key) const
{
    return m_settings.value(QString::fromLatin1(P) + key).toStringList();
}

void EmulatorSettings::setValue(const QString &key, const QVariant &value)
{
    m_settings.setValue(QString::fromLatin1(P) + key, value);
}

QStringList EmulatorSettings::buildArgs(const QString &gameTarget) const
{
    QStringList args;
    const QString resolution = stringValue(QStringLiteral("resolution"), QStringLiteral("1280x720"));
    const QStringList size = resolution.split(QLatin1Char('x'));
    if (size.size() == 2) {
        args << QStringLiteral("--screen-width") << size.at(0)
             << QStringLiteral("--screen-height") << size.at(1);
    }

    args << QStringLiteral("--user-name") << stringValue(QStringLiteral("user_name"), QStringLiteral("Kyty"));
    args << QStringLiteral("--user-id") << QString::number(intValue(QStringLiteral("user_id"), 1000));

    const QString mic = stringValue(QStringLiteral("mic"));
    if (!mic.isEmpty()) args << QStringLiteral("--mic") << mic;

    const QString color = stringValue(QStringLiteral("controller_color"));
    if (!color.isEmpty()) args << QStringLiteral("--controller-color") << color;
    args << QStringLiteral("--controller-volume") << QString::number(intValue(QStringLiteral("controller_volume"), 50));
    args << QStringLiteral("--controller-vibration") << QString::number(intValue(QStringLiteral("controller_vibration"), 100));
    args << QStringLiteral("--keyboard-player") << QString::number(intValue(QStringLiteral("keyboard_player"), 0));

    args << QStringLiteral("--present-mode") << stringValue(QStringLiteral("present_mode"), QStringLiteral("Mailbox"));
    const int gpu = intValue(QStringLiteral("gpu_index"), -1);
    if (gpu >= 0) args << QStringLiteral("--gpu") << QString::number(gpu);

    addFlag(args, boolValue(QStringLiteral("fullscreen"), true), "--fullscreen");
    addFlag(args, boolValue(QStringLiteral("hdr")), "--hdr");
    addFlag(args, boolValue(QStringLiteral("hide_cursor")), "--hide-cursor");
    addFlag(args, boolValue(QStringLiteral("readback_linear")), "--readback-linear-images");
    addFlag(args, boolValue(QStringLiteral("readback_tiled")), "--readback-tiled-images");
    addFlag(args, boolValue(QStringLiteral("sync_raw_images")), "--sync-raw-image-buffers");
    addFlag(args, boolValue(QStringLiteral("tessellation")), "--tessellation");
    addFlag(args, boolValue(QStringLiteral("occlusion_queries")), "--occlusion-queries");
    addFlag(args, !boolValue(QStringLiteral("trophies"), true), "--no-trophy-notifications");
    args << QStringLiteral("--trophy-duration") << QString::number(intValue(QStringLiteral("trophy_duration"), 6));
    addFlag(args, boolValue(QStringLiteral("perf_overlay")), "--perf-overlay");
    addFlag(args, boolValue(QStringLiteral("skip_notice")), "--skip-notice-screen");
    addFlag(args, boolValue(QStringLiteral("network_connected")), "--network-connected");
    args << QStringLiteral("--vblank-frequency") << QString::number(intValue(QStringLiteral("vblank_frequency"), 60));
    args << QStringLiteral("--console-language") << QString::number(intValue(QStringLiteral("console_language"), 1));

    addFlag(args, boolValue(QStringLiteral("vulkan_validation")), "--vulkan-validation");
    addFlag(args, boolValue(QStringLiteral("shader_validation")), "--shader-validation");
    args << QStringLiteral("--shader-optimization-type")
         << stringValue(QStringLiteral("shader_optimization"), QStringLiteral("Performance"));
    args << QStringLiteral("--shader-log-direction")
         << stringValue(QStringLiteral("shader_log_direction"), QStringLiteral("Silent"));
    args << QStringLiteral("--shader-log-folder")
         << stringValue(QStringLiteral("shader_log_folder"), QStringLiteral("_Shaders"));
    addFlag(args, boolValue(QStringLiteral("command_buffer_dump")), "--command-buffer-dump");
    args << QStringLiteral("--command-buffer-dump-folder")
         << stringValue(QStringLiteral("command_buffer_dump_folder"), QStringLiteral("_Buffers"));
    args << QStringLiteral("--printf-direction")
         << stringValue(QStringLiteral("printf_direction"), QStringLiteral("Silent"));
    args << QStringLiteral("--printf-output-file")
         << stringValue(QStringLiteral("printf_output_file"), QStringLiteral("_kyty.txt"));
    addFlag(args, boolValue(QStringLiteral("profiler")), "--profile");
    addFlag(args, boolValue(QStringLiteral("renderdoc")), "--rd");
    addFlag(args, boolValue(QStringLiteral("amd_cpu")), "--amd-cpu");

    addFlag(args, boolValue(QStringLiteral("no_splash")), "--no-splash");
    addFlag(args, boolValue(QStringLiteral("vr")), "--vr");
    addFlag(args, boolValue(QStringLiteral("gpu_assisted_validation")), "--gpu-assisted-validation");
    addFlag(args, boolValue(QStringLiteral("spirv_debug_printf")), "--spirv-debug-printf");
    addFlag(args, boolValue(QStringLiteral("graphics_debug_dump")), "--graphics-debug-dump");
    addFlag(args, boolValue(QStringLiteral("playgo_hack")), "--playgo-hack");

    for (const QString &binding : stringListValue(QStringLiteral("keymap"))) {
        if (!binding.trimmed().isEmpty()) args << QStringLiteral("--keymap") << binding.trimmed();
    }

    args << QStringLiteral("--game") << gameTarget;
    const QString addons = stringValue(QStringLiteral("addons_dir"));
    if (!addons.isEmpty() && QFileInfo::exists(addons)) {
        args << QStringLiteral("--addcont-dir") << addons;
    }
    return args;
}
