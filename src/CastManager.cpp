#include "CastManager.h"

#include "CastTargets.h"
#include "CastOutput.h"
#include "Tr.h"

#include <QDesktopServices>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace {
const int kSegmentsBeforeStart = 3; // HLS-Empfänger beginnen drei Segmente hinter dem Ende
}

CastManager::CastManager(CastOutput *player, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_server(&m_stream)
{
    QSettings s;
    m_height = s.value(QStringLiteral("cast/height"), 1080).toInt() == 720 ? 720 : 1080;
    m_fps = s.value(QStringLiteral("cast/fps"), 30).toInt() == 60 ? 60 : 30;
    m_bitrate = qBound(1500, s.value(QStringLiteral("cast/bitrate"), 8000).toInt(), 20000);
    m_alwaysListen = s.value(QStringLiteral("cast/alwaysListen"), false).toBool();

    connect(&m_discovery, &CastDiscovery::devicesChanged, this, &CastManager::devicesChanged);
    connect(&m_server, &CastServer::clientsChanged, this, &CastManager::devicesChanged);
    connect(&m_server, &CastServer::keyPressed, this, &CastManager::remoteKey);
    connect(&m_stream, &CastStream::segmentClosed, this, [this](qint64 count) {
        // genug Vorlauf: jetzt dem Empfänger die Adresse geben
        if (m_state != QLatin1String("starting") || m_targetStarted || count < kSegmentsBeforeStart || !m_target)
            return;
        m_targetStarted = true;
        setState(QStringLiteral("connecting"));
        m_target->start(m_server.hlsUrl(m_device.address), m_server.tsUrl(m_device.address), m_player->castTitle());
    });

    m_scanTimer.setInterval(12000);
    connect(&m_scanTimer, &QTimer::timeout, &m_discovery, &CastDiscovery::scan);
    m_startTimeout.setSingleShot(true);
    m_startTimeout.setInterval(25000);
    connect(&m_startTimeout, &QTimer::timeout, this, [this] {
        if (m_state == QLatin1String("starting") || m_state == QLatin1String("connecting")) {
            const QString name = m_deviceName;
            teardown(true);
            setState(QStringLiteral("error"), LTR("„%1“ hat die Wiedergabe nicht begonnen.").arg(name));
        }
    });
    if (m_alwaysListen)
        ensureListening();
}

CastManager::~CastManager()
{
    shutdown();
}

bool CastManager::available() const
{
    return !CastEncoder::videoEncoderName().isEmpty() && castTapAvailable();
}

QVariantList CastManager::devices() const
{
    QVariantList out;
    QList<CastDevice> list = m_discovery.devices();
    std::sort(list.begin(), list.end(), [](const CastDevice &a, const CastDevice &b) {
        return a.type != b.type ? a.type < b.type : a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    // TV-Apps und Browser, die sich bei Lumen angemeldet haben, zuerst
    for (const QVariant &v : m_server.clients()) {
        const QVariantMap c = v.toMap();
        out << QVariantMap{{"id", QStringLiteral("tv:") + c.value("id").toString()}, {"type", "tv"}, {"name", c.value("name")},
                           {"model", c.value("platform")}, {"address", c.value("address")}, {"manual", false}, {"needsPairing", false}};
    }
    for (const CastDevice &d : std::as_const(list)) {
        out << QVariantMap{{"id", d.id}, {"type", d.type}, {"name", d.name}, {"model", d.model},
                           {"address", d.address.toString()}, {"manual", d.manual}, {"needsPairing", d.needsPairing}};
    }
    return out;
}

QStringList CastManager::addresses() const
{
    QStringList out;
    if (!m_server.isListening())
        return out;
    for (const QString &ip : CastServer::localAddresses())
        out << QStringLiteral("http://%1:%2").arg(ip).arg(m_server.port());
    return out;
}

void CastManager::setHeight(int h)
{
    h = h == 720 ? 720 : 1080;
    if (h == m_height)
        return;
    m_height = h;
    QSettings().setValue(QStringLiteral("cast/height"), h);
    emit settingsChanged();
}

void CastManager::setFps(int f)
{
    f = f == 60 ? 60 : 30;
    if (f == m_fps)
        return;
    m_fps = f;
    QSettings().setValue(QStringLiteral("cast/fps"), f);
    emit settingsChanged();
}

void CastManager::setBitrate(int kbps)
{
    kbps = qBound(1500, kbps, 20000);
    if (kbps == m_bitrate)
        return;
    m_bitrate = kbps;
    QSettings().setValue(QStringLiteral("cast/bitrate"), kbps);
    emit settingsChanged();
}

void CastManager::setAlwaysListen(bool on)
{
    if (on == m_alwaysListen)
        return;
    m_alwaysListen = on;
    QSettings().setValue(QStringLiteral("cast/alwaysListen"), on);
    if (on)
        ensureListening();
    else if (!m_dialogOpen && !active()) {
        m_server.close();
        emit listeningChanged();
    }
    emit settingsChanged();
}

void CastManager::ensureListening()
{
    if (m_server.isListening())
        return;
    m_server.listen();
    emit listeningChanged();
}

void CastManager::openDialog()
{
    m_dialogOpen = true;
    ensureListening();
    m_discovery.scan();
    m_scanTimer.start();
}

void CastManager::closeDialog()
{
    m_dialogOpen = false;
    m_scanTimer.stop();
    if (!m_alwaysListen && !active() && m_server.isListening()) {
        m_server.close();
        emit listeningChanged();
    }
}

void CastManager::refresh()
{
    m_discovery.scan();
}

void CastManager::setState(const QString &state, const QString &message)
{
    if (state == m_state && message == m_message)
        return;
    m_state = state;
    m_message = message;
    emit stateChanged();
}

void CastManager::start(const QString &deviceId)
{
    if (active())
        teardown(false);
    CastDevice device;
    if (deviceId.startsWith(QLatin1String("tv:"))) {
        if (!m_server.hasClient(deviceId.mid(3)))
            return;
        device.id = deviceId;
        device.type = QStringLiteral("tv");
        for (const QVariant &v : m_server.clients()) {
            if (v.toMap().value("id").toString() == deviceId.mid(3)) {
                device.name = v.toMap().value("name").toString();
                device.address = QHostAddress(v.toMap().value("address").toString());
            }
        }
    } else {
        device = m_discovery.device(deviceId);
    }
    if (device.id.isEmpty())
        return;
    if (!available()) {
        setState(QStringLiteral("error"), LTR("Dieser Build kann nicht übertragen (H.264-/AAC-Encoder oder Lumens libmpv fehlt)."));
        return;
    }
    ensureListening();
    if (!m_server.isListening() || !m_tap.open()) {
        setState(QStringLiteral("error"), LTR("Der Sendestrom lässt sich nicht bereitstellen (Netzwerk-Port oder Ton-Pipe)."));
        return;
    }

    m_device = device;
    m_deviceId = device.id;
    m_deviceName = device.name;
    m_targetStarted = false;
    m_stream.reset();
    m_server.newSession();

    CastEncoder::Settings settings;
    settings.height = m_height;
    settings.width = m_height == 720 ? 1280 : 1920;
    // Entwickler/Tests: LUMEN_CAST_SIZE=<breite>x<höhe> (z. B. für Software-OpenGL ohne Grafikkarte)
    const QStringList size = qEnvironmentVariable("LUMEN_CAST_SIZE").split(QLatin1Char('x'));
    if (size.size() == 2 && size[0].toInt() >= 160 && size[1].toInt() >= 90) {
        settings.width = size[0].toInt() & ~1;
        settings.height = size[1].toInt() & ~1;
    }
    settings.fpsLimit = m_fps;
    settings.videoKbps = m_bitrate;
    m_encoder = new CastEncoder(&m_stream, &m_tap, settings, this);
    connect(m_encoder, &CastEncoder::failed, this, [this](const QString &message) {
        teardown(true);
        setState(QStringLiteral("error"), message);
    });
    m_target = CastTarget::create(device, &m_server, this);
    connect(m_target, &CastTarget::stateChanged, this, [this](const QString &state, const QString &message) {
        if (state == QLatin1String("error")) {
            teardown(true);
            setState(QStringLiteral("error"), message);
        } else if (state == QLatin1String("playing")) {
            m_startTimeout.stop();
            setState(QStringLiteral("playing"));
        }
    });

    setState(QStringLiteral("starting"));
    m_encoder->begin();
    m_player->setCastOutput(m_encoder, m_tap.path());
    m_startTimeout.start();
}

void CastManager::stop()
{
    if (m_state == QLatin1String("idle"))
        return;
    teardown(true);
    setState(QStringLiteral("idle"));
}

void CastManager::teardown(bool restorePlayer)
{
    m_startTimeout.stop();
    if (m_target) {
        m_target->disconnect(this);
        if (m_targetStarted)
            m_target->stop();
        m_target->deleteLater();
        m_target = nullptr;
    }
    // erst den Player vom Encoder lösen (der Renderer schreibt hinein), dann den Encoder beenden
    if (restorePlayer)
        m_player->setCastOutput(nullptr);
    if (m_encoder) {
        m_encoder->disconnect(this);
        m_encoder->end();
        m_encoder->deleteLater();
        m_encoder = nullptr;
    }
    m_server.endSession();
    m_tap.close();
    m_deviceId.clear();
    m_deviceName.clear();
    if (!m_alwaysListen && !m_dialogOpen && m_server.isListening()) {
        m_server.close();
        emit listeningChanged();
    }
}

void CastManager::shutdown()
{
    if (m_encoder || m_target)
        teardown(false);
    m_server.close();
}

bool CastManager::addDevice(const QString &type, const QString &address)
{
    QString spec = address.trimmed();
    if (spec.isEmpty())
        return false;
    return m_discovery.addManual(type + QLatin1Char(':') + spec);
}

void CastManager::removeDevice(const QString &deviceId)
{
    m_discovery.removeManual(deviceId);
}

bool CastManager::hasSystemDisplays() const
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return true;
#else
    return !QStandardPaths::findExecutable(QStringLiteral("gnome-network-displays")).isEmpty();
#endif
}

bool CastManager::openSystemDisplays()
{
#if defined(Q_OS_WIN)
    // „Mit drahtlosem Anzeigegerät verbinden“ (Miracast); der Empfänger erscheint danach als Bildschirm
    return QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings-connectabledevices:devicediscovery")));
#elif defined(Q_OS_MACOS)
    // Monitore: AirPlay-Empfänger als Bildschirm hinzufügen
    return QDesktopServices::openUrl(QUrl(QStringLiteral("x-apple.systempreferences:com.apple.Displays-Settings.extension")));
#else
    return QProcess::startDetached(QStringLiteral("gnome-network-displays"), {});
#endif
}
