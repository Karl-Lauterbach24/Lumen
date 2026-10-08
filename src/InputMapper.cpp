#include "InputMapper.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <cstring>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace {

// Funktionen, die sich wiederholen, solange die Taste gehalten wird
const QSet<QString> kRepeating = {QStringLiteral("up"), QStringLiteral("down"), QStringLiteral("left"), QStringLiteral("right"),
                                  QStringLiteral("volup"), QStringLiteral("voldown"), QStringLiteral("rewind"), QStringLiteral("forward")};
constexpr int kRepeatDelay = 420, kRepeatRate = 130; // ms
constexpr int kWizardIdle = 90000;                   // ms ohne Taste: Einrichtung endet

#ifdef Q_OS_LINUX
bool bit(const unsigned long *bits, int n)
{
    return (bits[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long)))) & 1;
}

// Was ein Eingabeknoten ist und kann
struct Probe
{
    QString name, group, id;
    bool keyboard = false; // vollständige Tastatur
    bool pointer = false;  // Maus, Touchpad, Tablett
    bool pad = false;      // Steuerkreuz oder Stick mit Gamepad-Tasten
    bool allowedBus = false;
    int buttons = 0;
};

Probe probe(int fd, const QString &path)
{
    Probe p;
    char text[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(text) - 1), text) >= 0)
        p.name = QString::fromUtf8(text).trimmed();
    input_id id = {};
    ioctl(fd, EVIOCGID, &id);
    p.id = QStringLiteral("%1:%2:%3").arg(id.bustype, 4, 16, QLatin1Char('0')).arg(id.vendor, 4, 16, QLatin1Char('0')).arg(id.product, 4, 16, QLatin1Char('0'));
    // ohne Hersteller- und Gerätenummer (CEC, Infrarot, virtuelle Geräte) unterscheidet der Name
    if (id.vendor == 0 && id.product == 0)
        p.id += QLatin1Char(':') + p.name;

    // Knoten desselben Geräts: gleiche Bluetooth-Adresse bzw. gleicher Anschluss
    std::memset(text, 0, sizeof(text));
    QString uniq, phys;
    if (ioctl(fd, EVIOCGUNIQ(sizeof(text) - 1), text) >= 0)
        uniq = QString::fromUtf8(text).trimmed();
    std::memset(text, 0, sizeof(text));
    if (ioctl(fd, EVIOCGPHYS(sizeof(text) - 1), text) >= 0)
        phys = QString::fromUtf8(text).trimmed();
    p.group = !uniq.isEmpty() ? uniq : phys.section(QLatin1Char('/'), 0, 0);
    if (p.group.isEmpty())
        p.group = p.id;

    // Infrarot-Empfänger hängen am Rechner selbst, der Kern führt sie unter "rc"
    const QString sys = QFileInfo(QStringLiteral("/sys/class/input/") + QFileInfo(path).fileName()).canonicalFilePath();
    const bool rc = sys.contains(QLatin1String("/rc/rc"));
    p.allowedBus = id.bustype == BUS_USB || id.bustype == BUS_BLUETOOTH || id.bustype == BUS_CEC || id.bustype == BUS_VIRTUAL || rc;

    unsigned long ev[EV_MAX / (8 * sizeof(long)) + 1] = {};
    unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = {};
    unsigned long rel[REL_MAX / (8 * sizeof(long)) + 1] = {};
    unsigned long abs[ABS_MAX / (8 * sizeof(long)) + 1] = {};
    ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev);
    if (bit(ev, EV_KEY))
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
    if (bit(ev, EV_REL))
        ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel)), rel);
    if (bit(ev, EV_ABS))
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);

    static const int letters[] = {KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P, KEY_A, KEY_S, KEY_D,
                                  KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M};
    int have = 0;
    for (const int k : letters)
        have += bit(keys, k);
    p.keyboard = have >= 20;
    const bool touch = bit(keys, BTN_TOUCH) || bit(keys, BTN_LEFT) || bit(keys, BTN_TOOL_FINGER) || bit(keys, BTN_TOOL_PEN);
    p.pointer = (bit(rel, REL_X) && bit(rel, REL_Y)) || (bit(abs, ABS_X) && bit(abs, ABS_Y) && touch);
    bool gamepad = false;
    for (int k = 0; k <= KEY_MAX; ++k) {
        if (!bit(keys, k))
            continue;
        if ((k >= BTN_MOUSE && k < BTN_JOYSTICK) || (k >= BTN_DIGI && k < BTN_WHEEL) || k == KEY_POWER || k == KEY_SLEEP || k == KEY_WAKEUP)
            continue;
        gamepad |= k >= BTN_JOYSTICK && k < BTN_DIGI;
        ++p.buttons;
    }
    p.pad = (bit(abs, ABS_HAT0X) && bit(abs, ABS_HAT0Y)) || (gamepad && bit(abs, ABS_X) && bit(abs, ABS_Y));
    return p;
}

// Tasten des Rechners selbst und Schalter von Tonbuchsen: keine Fernbedienung
bool ignoredName(const QString &name)
{
    static const QRegularExpression re(
        QStringLiteral("power button|sleep button|lid switch|video bus|pc speaker|hdmi|dp,pcm|headphone|headset|\\bmic\\b|line out|"
                       "webcam|camera|\\bwmi\\b|hotkeys|extra buttons|hid events|virtual buttons|jack"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(name).hasMatch();
}
#endif

QString firstUpper(const QString &s)
{
    return s.isEmpty() ? s : s.at(0).toUpper() + s.mid(1);
}

} // namespace

struct InputMapper::Node
{
    QString path;
    QString group;
    int fd = -1;
    QSocketNotifier *notifier = nullptr;
    Device *device = nullptr;
    bool grabbed = false;
    bool pad = false;
    QHash<int, int> axis;                 // Achse -> -1/0/+1
    QHash<int, QPair<int, int>> range;    // Achse -> kleinster, größter Wert
};

InputMapper::InputMapper(bool enabled, QObject *parent)
    : QObject(parent)
{
    m_rescan.setSingleShot(true);
    m_rescan.setInterval(400);
    connect(&m_rescan, &QTimer::timeout, this, &InputMapper::scan);
    connect(&m_repeat, &QTimer::timeout, this, [this] {
        if (!m_heldDevice)
            return m_repeat.stop();
        m_repeat.setInterval(kRepeatRate);
        const QString name = m_heldDevice->map.value(m_heldCode);
        if (!name.isEmpty())
            emit action(name, true);
    });
    m_idle.setSingleShot(true);
    m_idle.setInterval(kWizardIdle);
    connect(&m_idle, &QTimer::timeout, this, &InputMapper::cancelWizard);
    load();
    if (!enabled)
        return;
#ifdef Q_OS_LINUX
    // Geräte kommen und gehen (USB, Bluetooth): der Ordner ändert sich
    auto *watcher = new QFileSystemWatcher({QStringLiteral("/dev/input")}, this);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        m_retries = 0;
        m_rescan.start();
    });
    m_watcher = watcher;
    QTimer::singleShot(0, this, &InputMapper::scan);
#endif
}

InputMapper::~InputMapper()
{
    for (Device *d : std::as_const(m_devices)) {
        for (Node *n : std::as_const(d->nodes))
            closeNode(n);
        delete d;
    }
}

QStringList InputMapper::actions()
{
    return {QStringLiteral("up"), QStringLiteral("down"), QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("ok"),
            QStringLiteral("back"), QStringLiteral("menu"), QStringLiteral("playpause"), QStringLiteral("stop"),
            QStringLiteral("rewind"), QStringLiteral("forward"), QStringLiteral("prev"), QStringLiteral("next"),
            QStringLiteral("volup"), QStringLiteral("voldown"), QStringLiteral("mute"), QStringLiteral("info"),
            QStringLiteral("audio"), QStringLiteral("subtitle")};
}

bool InputMapper::available() const
{
    return m_watcher != nullptr;
}

QString InputMapper::storePath() const
{
    if (!m_store.isEmpty())
        return m_store;
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/remotes.json");
}

void InputMapper::setStorePath(const QString &file)
{
    m_store = file;
    m_stored.clear();
    load();
}

void InputMapper::load()
{
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject all = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = all.begin(); it != all.end(); ++it)
        m_stored.insert(it.key(), it.value().toObject().toVariantMap());
}

void InputMapper::save() const
{
    QJsonObject all;
    for (auto it = m_stored.cbegin(); it != m_stored.cend(); ++it)
        all.insert(it.key(), QJsonObject::fromVariantMap(it.value()));
    QDir().mkpath(QFileInfo(storePath()).absolutePath());
    QFile f(storePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(all).toJson());
}

InputMapper::Device *InputMapper::deviceById(const QString &id) const
{
    return m_devices.value(id, nullptr);
}

QVariantList InputMapper::devices() const
{
    QVariantList out;
    for (const Device *d : m_devices)
        out.append(QVariantMap{{"id", d->id}, {"name", d->name}, {"mapped", !d->map.isEmpty()}, {"connected", true}});
    for (auto it = m_stored.cbegin(); it != m_stored.cend(); ++it) {
        if (!m_devices.contains(it.key()))
            out.append(QVariantMap{{"id", it.key()}, {"name", it.value().value("name")}, {"mapped", true}, {"connected", false}});
    }
    return out;
}

void InputMapper::rescan()
{
    m_rescan.start();
}

// ---------------------------------------------------------------------------
// Geräte finden und lesen (Linux)
// ---------------------------------------------------------------------------

void InputMapper::scan()
{
#ifdef Q_OS_LINUX
    const QStringList present = QDir(QStringLiteral("/dev/input")).entryList({QStringLiteral("event*")}, QDir::System | QDir::Files);
    QSet<QString> paths;
    for (const QString &name : present)
        paths.insert(QStringLiteral("/dev/input/") + name);

    // verschwundene Knoten austragen
    bool changed = false;
    for (auto it = m_devices.begin(); it != m_devices.end();) {
        Device *d = *it;
        for (int i = d->nodes.size() - 1; i >= 0; --i) {
            if (!paths.contains(d->nodes[i]->path)) {
                closeNode(d->nodes[i]);
                d->nodes.removeAt(i);
            }
        }
        if (d->nodes.isEmpty() && !d->fake) {
            if (m_heldDevice == d)
                m_heldDevice = nullptr;
            if (m_wizard == d->id)
                finish(false);
            delete d;
            it = m_devices.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    // Neue Knoten ansehen. Erst alle Knoten eines Geräts zusammen sagen, was es ist: Die Lautstärke-
    // Tasten einer Tastatur sind ein eigener Knoten, aber keine Fernbedienung.
    struct Found
    {
        QString path;
        int fd;
        Probe probe;
    };
    QHash<QString, QList<Found>> groups;
    QSet<QString> open;
    for (const Device *d : std::as_const(m_devices))
        for (const Node *n : d->nodes)
            open.insert(n->path);
    bool retry = false;
    for (const QString &path : std::as_const(paths)) {
        const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            retry |= errno == EACCES; // die Rechte setzt udev kurz nach dem Erscheinen
            continue;
        }
        Found f{path, fd, probe(fd, path)};
        groups[f.probe.group].append(f);
    }
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        bool keyboard = false, pad = false, allowed = true, ignored = false;
        int buttons = 0;
        for (const Found &f : *it) {
            keyboard |= f.probe.keyboard;
            pad |= f.probe.pad;
            allowed &= f.probe.allowedBus;
            ignored |= ignoredName(f.probe.name);
            if (!f.probe.pointer || f.probe.buttons > 8) // Maustasten zählen nicht
                buttons += f.probe.buttons;
        }
        const bool remote = !keyboard && allowed && !ignored && (buttons >= 3 || pad);
        for (const Found &f : *it) {
            if (!remote || open.contains(f.path) || (f.probe.buttons == 0 && !f.probe.pad)) {
                ::close(f.fd);
                continue;
            }
            Device *d = m_devices.value(f.probe.id);
            if (!d) {
                d = new Device;
                d->id = f.probe.id;
                d->name = f.probe.name;
                const QVariantMap stored = m_stored.value(d->id);
                const QVariantMap map = stored.value("map").toMap();
                for (auto m = map.cbegin(); m != map.cend(); ++m)
                    d->map.insert(m.key(), m.value().toString());
                m_devices.insert(d->id, d);
                changed = true;
            } else if (f.probe.name.size() < d->name.size()) {
                d->name = f.probe.name; // der kürzeste Name der Knoten: ohne "Consumer Control" und dergleichen
            }
            auto *n = new Node;
            n->path = f.path;
            n->group = f.probe.group;
            n->fd = f.fd;
            n->device = d;
            n->pad = f.probe.pad;
            if (n->pad) {
                for (const int a : {ABS_X, ABS_Y, ABS_RX, ABS_RY}) {
                    input_absinfo info = {};
                    if (ioctl(n->fd, EVIOCGABS(a), &info) >= 0 && info.maximum > info.minimum)
                        n->range.insert(a, {info.minimum, info.maximum});
                }
            }
            n->notifier = new QSocketNotifier(n->fd, QSocketNotifier::Read, this);
            connect(n->notifier, &QSocketNotifier::activated, this, [this, n] { readNode(n); });
            d->nodes.append(n);
        }
    }
    for (Device *d : std::as_const(m_devices)) {
        applyGrab(d);
        if (d->map.isEmpty() && !d->announced) {
            d->announced = true;
            emit setupWanted(d->id, d->name);
        }
    }
    if (changed)
        emit devicesChanged();
    // (ohne Zugriff auf die Eingabegeräte – Nutzer nicht in der Gruppe "input" – nicht endlos)
    if (retry && ++m_retries <= 5)
        QTimer::singleShot(1500, this, [this] { m_rescan.start(); });
#endif
}

void InputMapper::closeNode(Node *node)
{
#ifdef Q_OS_LINUX
    delete node->notifier;
    if (node->fd >= 0)
        ::close(node->fd);
#endif
    delete node;
}

// Eingerichtete Geräte (und das Gerät in der Einrichtung) gehören Lumen allein: sonst käme jede
// Taste ein zweites Mal über das Fenstersystem an, mit der Bedeutung, die es ihr gibt.
void InputMapper::applyGrab(Device *device)
{
#ifdef Q_OS_LINUX
    const bool want = !device->map.isEmpty() || m_wizard == device->id;
    for (Node *n : std::as_const(device->nodes)) {
        if (n->grabbed != want && ioctl(n->fd, EVIOCGRAB, want ? 1 : 0) >= 0)
            n->grabbed = want;
    }
#else
    Q_UNUSED(device)
#endif
}

void InputMapper::readNode(Node *node)
{
#ifdef Q_OS_LINUX
    input_event events[32];
    for (;;) {
        const ssize_t got = ::read(node->fd, events, sizeof(events));
        if (got < ssize_t(sizeof(input_event))) {
            if (got < 0 && errno != EAGAIN && errno != EINTR) {
                node->notifier->setEnabled(false); // abgezogen: der nächste Durchlauf trägt es aus
                m_rescan.start();
            }
            return;
        }
        Device *d = node->device;
        for (size_t i = 0; i < size_t(got) / sizeof(input_event); ++i) {
            const input_event &e = events[i];
            if (e.type == EV_KEY) {
                if (e.value == 2 || (e.code >= BTN_MOUSE && e.code < BTN_JOYSTICK)) // Wiederholung des Kerns, Maustasten
                    continue;
                button(d, QStringLiteral("k%1").arg(e.code), e.value == 1);
            } else if (e.type == EV_ABS && node->pad) {
                int state = 0;
                if (e.code >= ABS_HAT0X && e.code <= ABS_HAT3Y) {
                    state = e.value < 0 ? -1 : e.value > 0 ? 1 : 0;
                } else if (node->range.contains(e.code)) {
                    const auto r = node->range.value(e.code);
                    const double pos = double(e.value - r.first) / double(r.second - r.first);
                    state = pos < 0.2 ? -1 : pos > 0.8 ? 1 : 0;
                } else {
                    continue;
                }
                const int before = node->axis.value(e.code, 0);
                if (state == before)
                    continue;
                node->axis.insert(e.code, state);
                if (before != 0)
                    button(d, QStringLiteral("a%1%2").arg(e.code).arg(before < 0 ? QLatin1Char('-') : QLatin1Char('+')), false);
                if (state != 0)
                    button(d, QStringLiteral("a%1%2").arg(e.code).arg(state < 0 ? QLatin1Char('-') : QLatin1Char('+')), true);
            }
        }
    }
#else
    Q_UNUSED(node)
#endif
}

// ---------------------------------------------------------------------------
// Tasten
// ---------------------------------------------------------------------------

void InputMapper::button(Device *device, const QString &code, bool down)
{
    if (!down) {
        if (m_heldDevice == device && m_heldCode == code) {
            m_heldDevice = nullptr;
            m_repeat.stop();
        }
        return;
    }
    if (!m_wizard.isEmpty()) {
        if (device->id == m_wizard)
            wizardButton(device, code);
        return;
    }
    if (device->map.isEmpty()) {
        emit setupWanted(device->id, device->name); // noch nicht eingerichtet: jede Taste bietet es an
        return;
    }
    const QString name = device->map.value(code);
    if (name.isEmpty())
        return;
    emit action(name, false);
    if (kRepeating.contains(name)) {
        m_heldDevice = device;
        m_heldCode = code;
        m_repeat.start(kRepeatDelay);
    }
}

QString InputMapper::wizardDevice() const
{
    const Device *d = deviceById(m_wizard);
    return d ? d->name : QString();
}

QVariantList InputMapper::wizardSteps() const
{
    QVariantList out;
    const QStringList all = actions();
    for (int i = 0; i < all.size(); ++i) {
        const QString code = m_assigned.value(all.at(i));
        out.append(QVariantMap{{"id", all.at(i)}, {"optional", i >= kRequired}, {"done", !code.isEmpty()}, {"skipped", i < m_step && code.isEmpty()}});
    }
    return out;
}

void InputMapper::startWizard(const QString &deviceId)
{
    Device *d = deviceById(deviceId);
    if (!d || m_wizard == deviceId)
        return;
    if (!m_wizard.isEmpty())
        finish(false);
    m_wizard = deviceId;
    m_step = 0;
    m_assigned.clear();
    m_taken.clear();
    m_heldDevice = nullptr;
    m_repeat.stop();
    applyGrab(d);
    m_idle.start();
    emit wizardChanged();
}

void InputMapper::wizardButton(Device *, const QString &code)
{
    m_idle.start();
    const QStringList all = actions();
    const QString taken = m_assigned.key(code);
    if (!taken.isEmpty()) {
        // Wahlfreie Funktionen: die Taste für "Zurück" lässt eine aus, die für "OK" schließt ab
        if (m_step >= kRequired && taken == QLatin1String("back"))
            return skipStep();
        if (m_step >= kRequired && taken == QLatin1String("ok"))
            return finish(true);
        m_taken = taken;
        emit wizardChanged();
        return;
    }
    m_taken.clear();
    m_assigned.insert(all.at(m_step), code);
    advance();
}

void InputMapper::advance()
{
    ++m_step;
    if (m_step >= actions().size())
        return finish(true);
    emit wizardChanged();
}

void InputMapper::skipStep()
{
    if (m_wizard.isEmpty() || m_step < kRequired)
        return;
    m_taken.clear();
    m_idle.start();
    advance();
}

void InputMapper::cancelWizard()
{
    if (!m_wizard.isEmpty())
        finish(false);
}

void InputMapper::finish(bool saveIt)
{
    const QString id = m_wizard;
    Device *d = deviceById(id);
    const bool complete = saveIt && m_step >= kRequired && d;
    if (complete) {
        d->map.clear();
        QVariantMap map;
        for (auto it = m_assigned.cbegin(); it != m_assigned.cend(); ++it) {
            d->map.insert(it.value(), it.key());
            map.insert(it.value(), it.key());
        }
        m_stored.insert(id, QVariantMap{{"name", d->name}, {"map", map}});
        save();
    }
    m_wizard.clear();
    m_step = 0;
    m_assigned.clear();
    m_taken.clear();
    m_idle.stop();
    if (d)
        applyGrab(d);
    emit wizardChanged();
    emit devicesChanged();
    emit wizardFinished(id, complete);
}

void InputMapper::forget(const QString &deviceId)
{
    m_stored.remove(deviceId);
    save();
    if (Device *d = deviceById(deviceId)) {
        d->map.clear();
        d->announced = true; // nicht sofort wieder anbieten; eine Taste darauf tut es
        applyGrab(d);
    }
    emit devicesChanged();
}

// ---------------------------------------------------------------------------
// Ohne Gerät (Tests, Entwicklung)
// ---------------------------------------------------------------------------

void InputMapper::fakeDevice(const QString &name)
{
    const QString id = QStringLiteral("fake:") + name;
    if (m_devices.contains(id))
        return;
    auto *d = new Device;
    d->id = id;
    d->name = firstUpper(name);
    d->fake = true;
    const QVariantMap map = m_stored.value(id).value("map").toMap();
    for (auto m = map.cbegin(); m != map.cend(); ++m)
        d->map.insert(m.key(), m.value().toString());
    m_devices.insert(id, d);
    emit devicesChanged();
    if (d->map.isEmpty()) {
        d->announced = true;
        emit setupWanted(d->id, d->name);
    }
}

void InputMapper::fakeButton(const QString &name, int code)
{
    Device *d = deviceById(QStringLiteral("fake:") + name);
    if (!d)
        return;
    const QString c = QStringLiteral("k%1").arg(code);
    button(d, c, true);
    // (das Gerät kann in der Zwischenzeit nicht verschwinden: es ist keines)
    button(d, c, false);
}
