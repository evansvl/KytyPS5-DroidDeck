#include "Gamepad.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct EvdevGamepad::Device {
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

static bool isGamepadFd(int fd)
{
    std::array<unsigned char, (KEY_MAX + 8) / 8> keys {};
    std::array<unsigned char, (ABS_MAX + 8) / 8> abs {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys.data()) < 0) return false;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs.data()) < 0) return false;
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

static int axisDirection(int value, int center, int threshold)
{
    if (value < center - threshold) return -1;
    if (value > center + threshold) return 1;
    return 0;
}

EvdevGamepad::EvdevGamepad(std::function<void(PadAction)> actionCallback,
                           std::function<void(int)> countCallback,
                           QObject *parent)
    : QObject(parent)
    , m_actionCallback(std::move(actionCallback))
    , m_countCallback(std::move(countCallback))
{
    rescan();
    m_scanTimer = new QTimer(this);
    m_scanTimer->setInterval(1500);
    connect(m_scanTimer, &QTimer::timeout, this, [this] { rescan(); });
    m_scanTimer->start();
}

EvdevGamepad::~EvdevGamepad()
{
    for (auto &device : m_devices) {
        if (device->fd >= 0) close(device->fd);
    }
}

void EvdevGamepad::emitAction(PadAction action)
{
    if (m_actionCallback) m_actionCallback(action);
}

void EvdevGamepad::rescan()
{
    const int oldCount = static_cast<int>(m_devices.size());
    for (int index = 0; index < 64; ++index) {
        const QString path = QStringLiteral("/dev/input/event%1").arg(index);
        const bool exists = std::any_of(m_devices.begin(), m_devices.end(), [&](const auto &d) {
            return d->path == path;
        });
        if (exists) continue;

        const QByteArray encoded = path.toUtf8();
        const int fd = open(encoded.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (!isGamepadFd(fd)) {
            close(fd);
            continue;
        }

        auto device = std::make_unique<Device>();
        device->path = path;
        device->fd = fd;
        configureAxis(fd, ABS_X, device->xCenter, device->xThreshold);
        configureAxis(fd, ABS_Y, device->yCenter, device->yThreshold);
        device->notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read, this);
        Device *raw = device.get();
        connect(device->notifier.get(), &QSocketNotifier::activated, this, [this, raw] {
            readEvents(*raw);
        });
        m_devices.push_back(std::move(device));
    }

    if (m_countCallback && (oldCount != static_cast<int>(m_devices.size()) || oldCount == 0)) {
        m_countCallback(static_cast<int>(m_devices.size()));
    }
}

void EvdevGamepad::readEvents(Device &device)
{
    input_event event {};
    for (;;) {
        const ssize_t size = read(device.fd, &event, sizeof(event));
        if (size == static_cast<ssize_t>(sizeof(event))) {
            if (event.type == EV_KEY && event.value == 1) {
                switch (event.code) {
                case BTN_SOUTH:  emitAction(PadAction::Confirm); break;      // A
                case BTN_EAST:   emitAction(PadAction::Back); break;         // B
                case BTN_WEST:   emitAction(PadAction::NextValue); break;    // X
                case BTN_NORTH:  emitAction(PadAction::AddGame); break;      // Y
                case BTN_TL:     emitAction(PadAction::PrevSection); break;  // LB
                case BTN_TR:     emitAction(PadAction::NextSection); break;  // RB
                case BTN_SELECT: emitAction(PadAction::Refresh); break;      // View
                default: break;
                }
            } else if (event.type == EV_ABS) {
                if (event.code == ABS_HAT0X) {
                    if (event.value < 0) emitAction(PadAction::Left);
                    if (event.value > 0) emitAction(PadAction::Right);
                } else if (event.code == ABS_HAT0Y) {
                    if (event.value < 0) emitAction(PadAction::Up);
                    if (event.value > 0) emitAction(PadAction::Down);
                } else if (event.code == ABS_X) {
                    const int direction = axisDirection(event.value, device.xCenter, device.xThreshold);
                    if (direction != 0 && direction != device.xDirection)
                        emitAction(direction < 0 ? PadAction::Left : PadAction::Right);
                    device.xDirection = direction;
                } else if (event.code == ABS_Y) {
                    const int direction = axisDirection(event.value, device.yCenter, device.yThreshold);
                    if (direction != 0 && direction != device.yDirection)
                        emitAction(direction < 0 ? PadAction::Up : PadAction::Down);
                    device.yDirection = direction;
                }
            }
            continue;
        }
        if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        break;
    }
}
