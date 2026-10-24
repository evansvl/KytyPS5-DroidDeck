#pragma once

#include <QObject>
#include <QSocketNotifier>
#include <QTimer>

#include <functional>
#include <memory>
#include <vector>

enum class PadAction {
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Back,
    NextValue,
    AddGame,
    PrevSection,
    NextSection,
    Refresh,
};

class EvdevGamepad final : public QObject
{
public:
    EvdevGamepad(std::function<void(PadAction)> actionCallback,
                 std::function<void(int)> countCallback,
                 QObject *parent = nullptr);
    ~EvdevGamepad() override;

private:
    struct Device;
    std::vector<std::unique_ptr<Device>> m_devices;
    QTimer *m_scanTimer = nullptr;
    std::function<void(PadAction)> m_actionCallback;
    std::function<void(int)> m_countCallback;

    void rescan();
    void readEvents(Device &device);
    void emitAction(PadAction action);
};
