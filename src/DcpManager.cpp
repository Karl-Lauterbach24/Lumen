#include "DcpManager.h"
#include "Tr.h"

#include "DcpStream.h"
#include "DcpIab.h"
#include "DcpSubtitles.h"
#include "DisplayManager.h"
#include "MpvController.h"
#include "OpticalMedia.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QtDebug>

#include <algorithm>
#include <cmath>

namespace {

QString markerLabel(const QString &m)
{
    static const QHash<QString, const char *> names = {
        {"FFOC", QT_TRANSLATE_NOOP("Lumen", "Erstes Bild des Inhalts")}, {"LFOC", QT_TRANSLATE_NOOP("Lumen", "Letztes Bild des Inhalts")},
        {"FFTC", QT_TRANSLATE_NOOP("Lumen", "Titel-Anfang")}, {"LFTC", QT_TRANSLATE_NOOP("Lumen", "Titel-Ende")},
        {"FFOI", QT_TRANSLATE_NOOP("Lumen", "Pause – Anfang")}, {"LFOI", QT_TRANSLATE_NOOP("Lumen", "Pause – Ende")},
        {"FFEC", QT_TRANSLATE_NOOP("Lumen", "Abspann")}, {"LFEC", QT_TRANSLATE_NOOP("Lumen", "Abspann – Ende")},
        {"FFMC", QT_TRANSLATE_NOOP("Lumen", "Laufender Abspann")}, {"LFMC", QT_TRANSLATE_NOOP("Lumen", "Laufender Abspann – Ende")},
        {"FFOB", QT_TRANSLATE_NOOP("Lumen", "Erstes Bild nach Leader")}, {"LFOB", QT_TRANSLATE_NOOP("Lumen", "Letztes Bild vor Tail")},
    };
    return names.contains(m) ? QStringLiteral("%1 (%2)").arg(LTR(names.value(m)), m) : m;
}

// %Länge%-Maskierung für EDL-Einträge
QString edlFile(const QString &s)
{
    return QStringLiteral("%%1%%2").arg(s.toUtf8().size()).arg(s);
}

QString num(double v)
{
    return QString::number(v, 'f', 6);
}

} // namespace

DcpManager::DcpManager(MpvController *player, DisplayManager *displays, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_displays(displays)
{
    QSettings s;
    m_fader = s.value(QStringLiteral("dcp/fader"), 7.0).toDouble();
    m_route = s.value(QStringLiteral("dcp/route"), QStringLiteral("auto")).toString();
    m_decodeMode = s.value(QStringLiteral("dcp/decode"), QStringLiteral("auto")).toString();
    m_iabLayout = s.value(QStringLiteral("dcp/iabLayout"), Dcp::defaultIabLayout()).toString();
    m_identity = DcpCrypto::loadIdentity(configDir());
    loadStoredKdms();
    m_imageClock.setInterval(40);
    connect(&m_imageClock, &QTimer::timeout, this, &DcpManager::updateImageSubtitle);

    if (m_player) {
        m_player->addProtocol([](mpv_handle *mpv) {
            Dcp::attachProtocol(mpv);
            Dcp::attachIabProtocol(mpv);
        });
        // Aktiv = eine DCP-Komposition läuft. Beim Laden ist mpv kurz "idle",
        // daher erst aus, wenn eine andere Quelle geladen wird.
        auto sync = [this] {
            if (m_player->sourceKind() != QLatin1String("dcp") && m_playing >= 0) {
                m_playing = -1;
                m_current.clear();
                m_imageSubs.clear();
                m_imageClock.stop();
                m_player->removeOverlay(58);
            }
            const bool on = m_playing >= 0 && !m_player->idle();
            if (on != m_active) {
                m_active = on;
                emit activeChanged();
                emit packageChanged();
            }
        };
        connect(m_player, &MpvController::mediaChanged, this, sync);
        connect(m_player, &MpvController::idleChanged, this, sync);
        connect(m_player, &MpvController::droppedFramesChanged, this, &DcpManager::onDroppedFrames);
        connect(m_player, &MpvController::osdDimensionsChanged, this, [this] {
            m_imageAlpha = -1; // neu platzieren
            updateImageSubtitle();
        });
    }
}

DcpManager::~DcpManager()
{
    if (m_verifyCancel)
        *m_verifyCancel = true;
    ++m_generation;
}

bool DcpManager::isDcp(const QString &path)
{
    const QFileInfo fi(path);
    return Dcp::isDcp(fi.isDir() ? path : fi.absolutePath());
}

QString DcpManager::configDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/dcp");
}

void DcpManager::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}

// --------------------------------------------------------------------------
// Paket
// --------------------------------------------------------------------------

void DcpManager::open(const QString &path, bool autoplay)
{
    const int gen = ++m_generation;
    m_busy = true;
    emit busyChanged();
    setStatus(LTR("Lese DCP …"));
    QPointer<DcpManager> self(this);
    QThreadPool::globalInstance()->start([self, path, gen, autoplay] {
        Dcp::Package pkg = Dcp::scan(path);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, pkg, gen, autoplay] {
            if (!self || gen != self->m_generation)
                return;
            self->m_package = pkg;
            self->m_busy = false;
            emit self->busyChanged();
            emit self->packageChanged();
            self->m_verifyResult.clear();
            emit self->verifyChanged();
            self->setStatus(pkg.error.isEmpty() ? LTR("%1 Composition Playlist(s)").arg(pkg.cpls.size()) : pkg.error);
            if (autoplay && !pkg.cpls.isEmpty())
                self->play(0);
        }, Qt::QueuedConnection);
    });
}

void DcpManager::close()
{
    ++m_generation;
    m_package = Dcp::Package();
    emit packageChanged();
}

QVariantList DcpManager::cpls() const
{
    QVariantList out;
    int i = 0;
    for (const Dcp::Cpl &c : m_package.cpls) {
        QVariantMap m = c.toVariant();
        m["index"] = i;
        m["playing"] = m_active && m_playing == i;
        const QVariantMap ks = keyStatus(c);
        for (auto it = ks.cbegin(); it != ks.cend(); ++it)
            m.insert(it.key(), it.value());
        out.append(m);
        ++i;
    }
    return out;
}

// Schlüssellage einer CPL: alle Schlüssel vorhanden und jetzt gültig?
QVariantMap DcpManager::keyStatus(const Dcp::Cpl &cpl) const
{
    QVariantMap m;
    if (!cpl.missing.isEmpty()) {
        m["playable"] = false;
        m["keyState"] = QStringLiteral("missing");
        m["keyText"] = LTR("%1 Spurdatei(en) fehlen (Version File ohne Original?)").arg(cpl.missing.size());
        return m;
    }
    if (!cpl.encrypted()) {
        m["playable"] = true;
        m["keyState"] = QStringLiteral("open");
        m["keyText"] = LTR("Unverschlüsselt");
        return m;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    int have = 0, notYet = 0, expired = 0, fromPlugin = 0;
    QDateTime until;
    for (const QString &id : cpl.keyIds()) {
        auto it = m_manualKeys.constFind(id);
        if (it == m_manualKeys.constEnd()) {
            it = m_keys.constFind(id);
            if (it == m_keys.constEnd()) {
                if (!Dcp::providedKey(id).isEmpty()) {
                    ++have;
                    ++fromPlugin;
                }
                continue;
            }
        }
        const DcpCrypto::ContentKey *k = &it.value();
        if (k->notBefore.isValid() && now < k->notBefore)
            ++notYet;
        else if (k->notAfter.isValid() && now > k->notAfter)
            ++expired;
        else {
            ++have;
            if (k->notAfter.isValid() && (!until.isValid() || k->notAfter < until))
                until = k->notAfter;
        }
    }
    const int need = int(cpl.keyIds().size());
    m["keysNeeded"] = need;
    m["keysValid"] = have;
    m["playable"] = have == need;
    if (have == need) {
        m["keyState"] = QStringLiteral("valid");
        m["keyText"] = fromPlugin == need ? LTR("Schlüssel von Plugin")
                       : until.isValid() ? LTR("KDM gültig bis %1").arg(QLocale().toString(until.toLocalTime(), QLocale::ShortFormat))
                                         : LTR("Schlüssel vorhanden");
        m["validUntil"] = until;
    } else if (notYet) {
        m["keyState"] = QStringLiteral("notyet");
        m["keyText"] = LTR("KDM noch nicht gültig");
    } else if (expired) {
        m["keyState"] = QStringLiteral("expired");
        m["keyText"] = LTR("KDM abgelaufen");
    } else {
        m["keyState"] = QStringLiteral("nokdm");
        m["keyText"] = have ? LTR("%1 von %2 Schlüsseln – KDM unvollständig").arg(have).arg(need)
                            : LTR("Verschlüsselt – KDM erforderlich");
    }
    return m;
}

// --------------------------------------------------------------------------
// Schlüssel / Zertifikat
// --------------------------------------------------------------------------

QVariantMap DcpManager::identity() const
{
    QVariantMap m;
    m["valid"] = m_identity.valid;
    m["subject"] = m_identity.subject;
    m["issuer"] = m_identity.issuer;
    m["serial"] = m_identity.serial;
    m["thumbprint"] = m_identity.thumbprint;
    m["dnQualifier"] = m_identity.dnQualifier;
    m["notAfter"] = m_identity.notAfter;
    m["leafFile"] = m_identity.leafFile;
    m["dir"] = configDir();
    return m;
}

QVariantList DcpManager::kdms() const
{
    QVariantList out;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const DcpCrypto::Kdm &k : m_kdms) {
        QString cplTitle = k.title;
        bool known = false;
        for (const Dcp::Cpl &c : m_package.cpls)
            if (c.id == k.cplId)
                cplTitle = c.title, known = true;
        out.append(QVariantMap{
            {"file", QFileInfo(k.file).fileName()},
            {"title", cplTitle.isEmpty() ? k.annotation : cplTitle},
            {"cplId", k.cplId},
            {"cplLoaded", known},
            {"keys", int(k.keys.size())},
            {"notBefore", k.notBefore},
            {"notAfter", k.notAfter},
            {"valid", k.error.isEmpty() && (!k.notBefore.isValid() || now >= k.notBefore) && (!k.notAfter.isValid() || now <= k.notAfter)},
            {"forUs", !m_identity.serial.isEmpty() && k.recipientSerial == m_identity.serial},
            {"signed", k.signed_},
            {"signatureValid", k.signatureValid},
            {"chainValid", k.chainValid},
            {"signer", k.signer},
            {"signatureText", !k.signed_ ? QStringLiteral("unsigniert")
                              : k.signatureValid ? (k.chainValid ? LTR("Signatur geprüft") : LTR("Signatur gültig, Kette unvollständig"))
                                                 : LTR("Signatur ungültig")},
            {"error", k.error},
        });
    }
    return out;
}

void DcpManager::loadStoredKdms()
{
    m_kdms.clear();
    const QDir dir(configDir() + QStringLiteral("/kdm"));
    for (const QString &f : dir.entryList({QStringLiteral("*.xml")}, QDir::Files, QDir::Time))
        m_kdms.append(DcpCrypto::decryptKdm(dir.filePath(f), m_identity.keyFile));
    rebuildKeys();
}

void DcpManager::rebuildKeys()
{
    m_keys.clear();
    for (const DcpCrypto::Kdm &k : std::as_const(m_kdms)) {
        for (DcpCrypto::ContentKey key : k.keys) {
            // KDM-Zeitfenster gilt zusätzlich zu dem im Schlüsselblock
            if (!key.notBefore.isValid())
                key.notBefore = k.notBefore;
            if (!key.notAfter.isValid())
                key.notAfter = k.notAfter;
            const auto it = m_keys.constFind(key.keyId);
            // Mehrere KDMs: den am längsten gültigen nehmen
            if (it == m_keys.constEnd() || (key.notAfter.isValid() && it->notAfter.isValid() && key.notAfter > it->notAfter))
                m_keys.insert(key.keyId, key);
        }
    }
    emit keysChanged();
    emit packageChanged(); // Schlüsselstatus der CPLs
}

void DcpManager::loadKdm(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!m_identity.valid) {
        setStatus(LTR("Zuerst ein Zertifikat für Lumen erzeugen oder importieren – KDMs werden dafür ausgestellt"));
        return;
    }
    DcpCrypto::Kdm kdm = DcpCrypto::decryptKdm(path, m_identity.keyFile);
    if (kdm.keys.isEmpty()) {
        setStatus(kdm.error);
        return;
    }
    // Dauerhaft ablegen (bleibt verschlüsselt; beim Start neu ausgepackt)
    const QString dir = configDir() + QStringLiteral("/kdm");
    QDir().mkpath(dir);
    const QString target = QDir(dir).filePath(QFileInfo(path).fileName());
    if (QFileInfo(path).absoluteFilePath() != QFileInfo(target).absoluteFilePath()) {
        QFile::remove(target);
        QFile::copy(path, target);
    }
    kdm.file = target;
    for (int i = 0; i < m_kdms.size(); ++i)
        if (QFileInfo(m_kdms[i].file).fileName() == QFileInfo(target).fileName())
            m_kdms.removeAt(i--);
    m_kdms.prepend(kdm);
    rebuildKeys();
    setStatus(LTR("KDM geladen: %1 Schlüssel für „%2“%3")
                  .arg(kdm.keys.size()).arg(kdm.title.isEmpty() ? kdm.cplId : kdm.title,
                                            kdm.error.isEmpty() ? QString() : QStringLiteral(" (") + kdm.error + QLatin1Char(')')));
}

void DcpManager::removeKdm(int index)
{
    if (index < 0 || index >= m_kdms.size())
        return;
    QFile::remove(m_kdms[index].file);
    m_kdms.removeAt(index);
    rebuildKeys();
}

bool DcpManager::addKey(const QString &keyId, const QString &keyHex)
{
    const QString id = Dcp::normalizeUuid(keyId);
    const QByteArray key = QByteArray::fromHex(keyHex.trimmed().toLatin1());
    if (DcpCrypto::uuidToBytes(id).isEmpty() || key.size() != 16)
        return false;
    DcpCrypto::ContentKey k;
    k.keyId = id;
    k.key = key;
    k.type = QStringLiteral("manual");
    m_manualKeys.insert(id, k);
    emit keysChanged();
    emit packageChanged();
    return true;
}

int DcpManager::importKeys(const QUrl &file)
{
    QFile f(file.isLocalFile() ? file.toLocalFile() : file.toString());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    int n = 0;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList p = line.split(QRegularExpression(QStringLiteral("[\\s,;=]+")), Qt::SkipEmptyParts);
        if (p.size() >= 2 && addKey(p[0], p[1]))
            ++n;
    }
    setStatus(LTR("%1 Schlüssel übernommen (nur für diese Sitzung)").arg(n));
    return n;
}

bool DcpManager::createIdentity(const QString &organisation)
{
    QString err;
    if (!DcpCrypto::createIdentity(configDir(), organisation, &err)) {
        setStatus(err);
        return false;
    }
    m_identity = DcpCrypto::loadIdentity(configDir());
    emit identityChanged();
    loadStoredKdms(); // alte KDMs passen nicht mehr zum neuen Schlüssel
    setStatus(LTR("Neues Zertifikat erzeugt – Leaf-Zertifikat exportieren und an den KDM-Aussteller senden"));
    return true;
}

bool DcpManager::importIdentity(const QUrl &cert, const QUrl &key)
{
    QString err;
    if (!DcpCrypto::importIdentity(configDir(), cert.toLocalFile(), key.toLocalFile(), &err)) {
        setStatus(err);
        return false;
    }
    m_identity = DcpCrypto::loadIdentity(configDir());
    emit identityChanged();
    loadStoredKdms();
    setStatus(LTR("Zertifikat übernommen"));
    return true;
}

bool DcpManager::exportCertificate(const QUrl &target, bool chain)
{
    if (!m_identity.valid)
        return false;
    const QString dst = target.toLocalFile();
    QFile::remove(dst);
    const bool ok = QFile::copy(chain ? m_identity.chainFile : m_identity.leafFile, dst);
    setStatus(ok ? LTR("Zertifikat gespeichert: %1").arg(QDir::toNativeSeparators(dst))
                 : LTR("Zertifikat konnte nicht gespeichert werden"));
    return ok;
}

// --------------------------------------------------------------------------
// Wiedergabe
// --------------------------------------------------------------------------

double DcpManager::faderToDb(double f)
{
    // Dolby-/Kino-Prozessor-Skala: 7,0 = 0 dB; 4…10: 3,33 dB je Einheit, darunter 20 dB je Einheit
    f = std::clamp(f, 0.0, 10.0);
    if (f >= 4.0)
        return (f - 7.0) * (10.0 / 3.0);
    return -10.0 + (f - 4.0) * 20.0;
}

void DcpManager::setFader(double f)
{
    f = std::round(std::clamp(f, 0.0, 10.0) * 10.0) / 10.0;
    if (qFuzzyCompare(f, m_fader))
        return;
    m_fader = f;
    QSettings().setValue(QStringLiteral("dcp/fader"), f);
    if (m_active)
        m_player->setOption(QStringLiteral("volume-gain"), faderToDb(f));
    emit faderChanged();
}

QString DcpManager::routeFilter(int ch, const QString &route)
{
    // SMPTE 428-12: 1 L, 2 R, 3 C, 4 LFE, 5 Ls, 6 Rs, 7 HI, 8 VI-N, (9/10 Lc/Rc), 11 Lrs, 12 Rrs …
    if (route == QLatin1String("hi") && ch >= 7)
        return QStringLiteral("lavfi=[pan=stereo|FL=c6|FR=c6]");
    if (route == QLatin1String("vi") && ch >= 8)
        return QStringLiteral("lavfi=[pan=stereo|FL=c7|FR=c7]");
    if (ch <= 6)
        return QString();
    const bool want71 = route == QLatin1String("71") || (route == QLatin1String("auto") && ch >= 12);
    if (want71 && ch >= 12)
        return QStringLiteral("lavfi=[pan=7.1|FL=c0|FR=c1|FC=c2|LFE=c3|SL=c4|SR=c5|BL=c10|BR=c11]");
    if (want71 && ch >= 8)
        return QStringLiteral("lavfi=[pan=7.1|FL=c0|FR=c1|FC=c2|LFE=c3|SL=c4|SR=c5|BL=c6|BR=c7]");
    return QStringLiteral("lavfi=[pan=5.1|FL=c0|FR=c1|FC=c2|LFE=c3|SL=c4|SR=c5]");
}

void DcpManager::setAudioRoute(const QString &r)
{
    if (r == m_route)
        return;
    m_route = r;
    QSettings().setValue(QStringLiteral("dcp/route"), r);
    if (m_active && !m_iabActive) // Kanalzuordnung betrifft nur die PCM-Spur
        m_player->setOption(QStringLiteral("af"), routeFilter(m_channels, r));
    emit audioRouteChanged();
}

bool DcpManager::iabAvailable() const
{
    return Dcp::iabAvailable();
}

QVariantList DcpManager::iabLayouts() const
{
    QVariantList out;
    for (const Dcp::IabLayout &l : Dcp::iabLayouts())
        out << QVariantMap{{"value", l.id}, {"text", l.name}};
    return out;
}

void DcpManager::setIabLayout(const QString &layout)
{
    if (layout == m_iabLayout)
        return;
    m_iabLayout = layout;
    QSettings().setValue(QStringLiteral("dcp/iabLayout"), layout);
    emit iabLayoutChanged();
    // Läuft gerade eine IAB-Spur: an gleicher Stelle mit neuem Layout neu aufbauen
    if (m_active && m_iabActive && m_playing >= 0)
        play(m_playing, m_player->position());
}

void DcpManager::setDecodeMode(const QString &m)
{
    if (m == m_decodeMode)
        return;
    m_decodeMode = m;
    QSettings().setValue(QStringLiteral("dcp/decode"), m);
    emit decodeModeChanged();
    if (m_active && m_playing >= 0)
        play(m_playing, m_player->position()); // Decoder neu starten
}

// JPEG 2000 ist in Auflösungsstufen kodiert: bei kleinerer Ausgabe nur die
// benötigten Stufen dekodieren (4K auf 2K/1080p = verlustfrei halbe Arbeit)
int DcpManager::chooseReduction(const Dcp::Cpl &cpl) const
{
    if (m_decodeMode == QLatin1String("full"))
        return 0;
    if (m_decodeMode == QLatin1String("half"))
        return 1;
    if (m_decodeMode == QLatin1String("quarter"))
        return 2;
    int width = 0;
    for (const Dcp::Reel &r : cpl.reels)
        for (const Dcp::ReelAsset &a : r.assets)
            if ((a.kind == Dcp::Kind::Picture || a.kind == Dcp::Kind::StereoPicture) && a.mxf.width)
                width = std::max(width, a.mxf.width);
    int outW = 1920;
    if (m_displays && m_player) {
        const QVariantMap o = m_displays->output(m_player->profile().value("output").toString());
        if (o.value("width").toInt() > 0)
            outW = o.value("width").toInt();
    }
    int k = 0;
    while (width > 0 && k < 3 && width / (1 << (k + 1)) >= outW * 0.9)
        ++k;
    return k;
}

bool DcpManager::play(int index, double start)
{
    if (!m_player || index < 0 || index >= m_package.cpls.size())
        return false;
    const Dcp::Cpl &cpl = m_package.cpls[index];
    const QVariantMap ks = keyStatus(cpl);
    if (!ks.value("playable").toBool()) {
        setStatus(ks.value("keyText").toString());
        return false;
    }

    const QVariantMap profile = m_player->profile();
    const QString stereoOut = profile.value("stereoOut", "none").toString();
    const bool stereo = cpl.stereoscopic();
    const bool want3d = stereo && stereoOut != QLatin1String("none");
    auto keyFor = [this](const Dcp::ReelAsset &a) -> QByteArray {
        if (!a.encrypted())
            return {};
        if (m_manualKeys.contains(a.keyId))
            return m_manualKeys.value(a.keyId).key;
        if (m_keys.contains(a.keyId))
            return m_keys.value(a.keyId).key;
        return Dcp::providedKey(a.keyId); // Plugin
    };

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/lumen-dcp/") + cpl.id;
    QDir().mkpath(dir);
    Dcp::g_keyErrors = 0;
    Dcp::g_missingKeys = 0;

    // --- EDL: Bild (1 oder 2 Augen) und Ton über alle Rollen ---------------
    QStringList pic[2], snd, iab;
    bool anyIab = false;
    static const QHash<QString, QString> iabSilence = {
        {"7.1.4", "7.1.4"}, {"5.1.4", "5.1.4"}, {"7.1", "7.1"}, {"5.1", "5.1(side)"}, {"2.0", "stereo"}};
    QList<Dcp::SubtitleSource> subs, captions;
    QString ffmeta = QStringLiteral(";FFMETADATA1\n");
    struct Chapter { double t; QString title; };
    QList<Chapter> chapters;
    double t = 0;
    int reelNo = 0;
    m_channels = 0;
    int width = 2048, height = 1080;
    for (const Dcp::Reel &reel : cpl.reels) {
        ++reelNo;
        const double len = reel.seconds();
        if (len <= 0)
            continue;
        chapters.append({t, LTR("Rolle %1").arg(reelNo)});
        for (const Dcp::Marker &m : reel.markers)
            chapters.append({t + (reel.editRate.valid() ? m.offset / reel.editRate.value() : 0), markerLabel(m.label)});

        const Dcp::ReelAsset *p = reel.find(Dcp::Kind::Picture);
        if (!p)
            p = reel.find(Dcp::Kind::StereoPicture);
        if (p && p->mxf.width)
            width = p->mxf.width, height = p->mxf.height;
        const double fps = p ? p->frameRate.value() : 24.0;
        for (int eye = 0; eye < (want3d ? 2 : 1); ++eye) {
            QString url;
            if (p) {
                const bool stereoFile = p->kind == Dcp::Kind::StereoPicture || p->mxf.stereoscopic;
                url = Dcp::streamUrl(Dcp::registerStream({p->file, keyFor(*p), stereoFile ? eye + 1 : 0}));
                pic[eye] << QStringLiteral("%1,%2,%3").arg(edlFile(url), num(p->startSeconds()), num(len));
            } else {
                url = QStringLiteral("av://lavfi:color=c=black:s=%1x%2:r=%3").arg(width).arg(height).arg(fps);
                pic[eye] << QStringLiteral("%1,0,%2").arg(edlFile(url), num(len));
            }
        }
        if (const Dcp::ReelAsset *s = reel.find(Dcp::Kind::Sound)) {
            m_channels = std::max(m_channels, s->mxf.channels);
            const QString url = Dcp::streamUrl(Dcp::registerStream({s->file, keyFor(*s), 0}));
            snd << QStringLiteral("%1,%2,%3").arg(edlFile(url), num(s->startSeconds()), num(len));
        } else {
            snd << QStringLiteral("%1,0,%2").arg(edlFile(QStringLiteral("av://lavfi:anullsrc=r=48000:cl=5.1")), num(len));
        }
        // Immersive Audio: nur mit Renderer und (falls verschlüsselt) Schlüssel
        const Dcp::ReelAsset *ia = reel.find(Dcp::Kind::Atmos);
        if (ia && Dcp::iabAvailable() && !ia->file.isEmpty() && (!ia->encrypted() || !keyFor(*ia).isEmpty())) {
            anyIab = true;
            const QString url = Dcp::iabUrl(Dcp::registerIabStream({ia->file, keyFor(*ia), 0}, m_iabLayout));
            iab << QStringLiteral("%1,%2,%3").arg(edlFile(url), num(ia->startSeconds()), num(len));
        } else {
            iab << QStringLiteral("%1,0,%2").arg(edlFile(QStringLiteral("av://lavfi:anullsrc=r=48000:cl=%1")
                                                              .arg(iabSilence.value(m_iabLayout, QStringLiteral("7.1.4")))), num(len));
        }
        if (const Dcp::ReelAsset *st = reel.find(Dcp::Kind::Subtitle)) {
            if (!st->file.isEmpty())
                subs.append({st->file, keyFor(*st), t, st->startSeconds(), len, st->language});
        }
        if (const Dcp::ReelAsset *cc = reel.find(Dcp::Kind::ClosedCaption)) {
            if (!cc->file.isEmpty())
                captions.append({cc->file, keyFor(*cc), t, cc->startSeconds(), len, cc->language});
        }
        t += len;
    }
    if (pic[0].isEmpty()) {
        setStatus(LTR("CPL enthält keine abspielbaren Rollen"));
        return false;
    }

    QString edl = QStringLiteral("# mpv EDL v0\n!no_chapters\n");
    for (int eye = 0; eye < (want3d ? 2 : 1); ++eye) {
        edl += QStringLiteral("!new_stream\n!track_meta,title=%1\n").arg(want3d ? (eye ? LTR("Rechtes Auge") : LTR("Linkes Auge")) : LTR("Bild"));
        edl += pic[eye].join(QLatin1Char('\n')) + QLatin1Char('\n');
    }
    edl += QStringLiteral("!new_stream\n!track_meta,title=DCP-Ton\n") + snd.join(QLatin1Char('\n')) + QLatin1Char('\n');
    if (anyIab)
        edl += QStringLiteral("!new_stream\n!track_meta,title=%1\n").arg(LTR("Immersive Audio (Atmos/IAB) → %1").arg(m_iabLayout))
               + iab.join(QLatin1Char('\n')) + QLatin1Char('\n');
    m_iabActive = anyIab;
    const QString edlPath = QDir(dir).filePath(QStringLiteral("composition.edl"));
    {
        QFile f(edlPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            setStatus(LTR("Temporäre Wiedergabeliste nicht schreibbar"));
            return false;
        }
        f.write(edl.toUtf8());
    }

    // --- Kapitel aus Rollen + Markern --------------------------------------
    std::stable_sort(chapters.begin(), chapters.end(), [](const Chapter &a, const Chapter &b) { return a.t < b.t; });
    QList<Chapter> merged;
    for (const Chapter &c : std::as_const(chapters)) {
        if (!merged.isEmpty() && std::abs(merged.last().t - c.t) < 0.05)
            merged.last().title += QStringLiteral(" · ") + c.title;
        else
            merged.append(c);
    }
    for (int i = 0; i < merged.size(); ++i) {
        const double end = i + 1 < merged.size() ? merged[i + 1].t : t;
        QString title = merged[i].title;
        title.replace(QLatin1Char('='), QStringLiteral("\\=")).replace(QLatin1Char(';'), QStringLiteral("\\;")).replace(QLatin1Char('#'), QStringLiteral("\\#"));
        ffmeta += QStringLiteral("[CHAPTER]\nTIMEBASE=1/1000\nSTART=%1\nEND=%2\ntitle=%3\n")
                      .arg(qRound64(merged[i].t * 1000)).arg(qRound64(end * 1000)).arg(title);
    }
    const QString chapterPath = QDir(dir).filePath(QStringLiteral("chapters.ffmeta"));
    {
        QFile f(chapterPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(ffmeta.toUtf8());
    }

    // --- Optionen ------------------------------------------------------------
    m_reduction = m_forceReduction >= 0 ? m_forceReduction : chooseReduction(cpl);
    m_forceReduction = -1;
    m_adapted = false;
    QVariantMap opts{
        {"force-media-title", cpl.title},
        {"chapters-file", chapterPath},
        {"resume-playback", "no"},
        {"hwdec", "no"},                     // JPEG 2000: keine GPU-Dekodierung
        {"vd-lavc-threads", "0"},
        {"framedrop", "decoder+vo"},
        {"demuxer-readahead-secs", "6"},
        {"demuxer-max-bytes", "768MiB"},
        {"volume-gain", faderToDb(m_fader)},
        // nur für diese selbst erzeugte EDL: mpv lässt eigene Protokolle
        // (lumendcp://) in EDL-Quellen sonst nicht zu
        {"load-unsafe-playlists", "yes"},
    };
    if (m_reduction > 0)
        opts["vd-lavc-o"] = QStringLiteral("lowres=%1").arg(m_reduction);
    if (m_iabActive) {
        opts["aid"] = "2"; // Immersive-Audio-Spur statt der PCM-Fassung
        opts["af"] = QString();
    } else {
        const QString af = routeFilter(m_channels, m_route);
        if (!af.isEmpty())
            opts["af"] = af;
    }
    if (want3d)
        opts["lavfi-complex"] = QStringLiteral("[vid1][vid2]hstack[vo]");
    if (start > 0)
        opts["start"] = QString::number(start, 'f', 3);

    // Untertitel (offene Einblendung, per Voreinstellung an) und Closed Captions
    // (als zweite, wählbare Spur); Bilduntertitel laufen als Overlay
    QString subInfo;
    QStringList subFiles;
    m_imageSubs.clear();
    m_imageShown = -1;
    m_player->removeOverlay(58);
    if (!subs.isEmpty()) {
        const Dcp::SubtitleResult sr = Dcp::buildSubtitles(subs, dir, QStringLiteral("Untertitel ") + subs.first().language);
        if (!sr.assFile.isEmpty()) {
            subFiles << sr.assFile;
            opts["sub-fonts-dir"] = sr.fontsDir;
            opts["sid"] = "1";
            subInfo = LTR(" · Untertitel %1").arg(sr.language);
        } else if (!sr.error.isEmpty() && sr.imageEvents.isEmpty()) {
            subInfo = QStringLiteral(" · ") + sr.error;
        }
        m_imageSubs = sr.imageEvents;
        if (!m_imageSubs.isEmpty())
            subInfo += LTR(" · %1 Bilduntertitel").arg(m_imageSubs.size());
    }
    if (!captions.isEmpty()) {
        const Dcp::SubtitleResult cr = Dcp::buildSubtitles(captions, dir, LTR("Closed Captions ") + captions.first().language);
        if (!cr.assFile.isEmpty()) {
            subFiles << cr.assFile;
            if (!opts.contains("sub-fonts-dir"))
                opts["sub-fonts-dir"] = cr.fontsDir;
            subInfo += LTR(" · Closed Captions");
        }
    }
    if (!subFiles.isEmpty()) {
        opts["sub-files"] = subFiles;
        opts["sub-ass-override"] = "no"; // Positionen/Stile der DCP-Untertitel beibehalten
    }
    m_pictureSize = QSize(width, height);
    m_imageAlpha = -1;
    if (m_imageSubs.isEmpty())
        m_imageClock.stop();
    else
        m_imageClock.start();

    m_playing = index;
    m_current = cpl.toVariant();
    m_current["index"] = index;
    m_current["reduction"] = m_reduction;
    m_current["channelsUsed"] = m_channels;
    m_current["iab"] = m_iabActive;
    m_player->openPrepared(Optical::edlUrl(edl), opts, QStringLiteral("dcp"), cpl.root, want3d ? QStringLiteral("sbsl") : QStringLiteral("none"));
    m_active = true;
    m_playClock.start();
    emit activeChanged();
    emit packageChanged();
    QStringList parts{cpl.title};
    if (cpl.encrypted())
        parts << LTR("entschlüsselt");
    if (m_reduction)
        parts << LTR("J2K 1/%1 Auflösung").arg(1 << m_reduction);
    if (stereo)
        parts << (want3d ? QStringLiteral("3D") : LTR("3D-DCP als 2D"));
    if (m_iabActive)
        parts << LTR("Atmos/IAB → %1").arg(m_iabLayout);
    setStatus(parts.join(QStringLiteral(" · ")) + subInfo);
    return true;
}

// Bilduntertitel zeitgenau einblenden (mit Ein-/Ausblendung), relativ zur Bildfläche
void DcpManager::updateImageSubtitle()
{
    if (!m_player)
        return;
    if (!m_active || m_imageSubs.isEmpty()) {
        if (m_imageShown >= 0) {
            m_player->removeOverlay(58);
            m_imageShown = -1;
        }
        return; // Uhr läuft weiter: "aktiv" kann beim Laden kurz wechseln
    }
    const double t = m_player->position();
    int found = -1;
    for (int i = 0; i < m_imageSubs.size(); ++i)
        if (t >= m_imageSubs[i].start && t < m_imageSubs[i].end) {
            found = i;
            break;
        }
    if (found < 0) {
        if (m_imageShown >= 0)
            m_player->removeOverlay(58);
        m_imageShown = -1;
        return;
    }
    const Dcp::ImageSub &s = m_imageSubs[found];
    double a = 1.0;
    if (s.fadeIn > 0 && t < s.start + s.fadeIn)
        a = (t - s.start) / s.fadeIn;
    if (s.fadeOut > 0 && t > s.end - s.fadeOut)
        a = std::min(a, (s.end - t) / s.fadeOut);
    const int alpha = int(std::clamp(a, 0.0, 1.0) * 255);
    if (found == m_imageShown && std::abs(alpha - m_imageAlpha) < 8)
        return;
    if (found != m_imageShown)
        m_imageCache = QImage::fromData(s.png).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_imageShown = found;
    m_imageAlpha = alpha;
    if (m_imageCache.isNull())
        return;
    QImage img = m_imageCache;
    if (alpha < 255) {
        img = QImage(m_imageCache.size(), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setOpacity(alpha / 255.0);
        p.drawImage(0, 0, m_imageCache);
    }
    const QVariantMap o = m_player->osdDimensions();
    const double ow = o.value("w").toDouble(), oh = o.value("h").toDouble();
    const double ml = o.value("ml").toDouble(), mt = o.value("mt").toDouble();
    const double vw = ow - ml - o.value("mr").toDouble(), vh = oh - mt - o.value("mb").toDouble();
    if (vw <= 0 || vh <= 0)
        return;
    // PNG-Pixel beziehen sich auf die Bildgröße des DCP
    const double scale = vw / std::max(1, m_pictureSize.width());
    const double dw = img.width() * scale, dh = img.height() * scale;
    double x = ml + vw * (0.5 + s.hpos) - dw / 2;
    if (s.halign == QLatin1String("left"))
        x = ml + vw * s.hpos;
    else if (s.halign == QLatin1String("right"))
        x = ml + vw * (1.0 - s.hpos) - dw;
    double y = mt + vh * (1.0 - s.vpos) - dh;
    if (s.valign == QLatin1String("top"))
        y = mt + vh * s.vpos;
    else if (s.valign == QLatin1String("center"))
        y = mt + vh * (0.5 + s.vpos) - dh / 2;
    m_player->setOverlay(58, img, qRound(x), qRound(y), qRound(dw), qRound(dh));
}

// Automatik: Schafft die CPU volle Auflösung nicht in Echtzeit, auf die nächste
// Auflösungsstufe wechseln (einmal je Wiedergabe)
void DcpManager::onDroppedFrames()
{
    if (!m_active || m_adapted || m_decodeMode != QLatin1String("auto") || m_reduction >= 2 || m_playing < 0)
        return;
    if (m_player->droppedFrames() < 24 || m_playClock.elapsed() > 30000 || m_player->paused())
        return;
    const int next = m_reduction + 1;
    m_forceReduction = next;
    play(m_playing, m_player->position());
    m_adapted = true;
    m_player->showText(LTR("JPEG 2000: CPU zu langsam – dekodiere mit 1/%1 Auflösung").arg(1 << next), 4000);
}

// --------------------------------------------------------------------------
// Prüfung (SHA-1 gegen die Packing List)
// --------------------------------------------------------------------------

void DcpManager::verify(int index)
{
    if (index < 0 || index >= m_package.cpls.size() || m_verifying)
        return;
    struct Item { QString name, path, hash; qint64 size; };
    QList<Item> items;
    QSet<QString> seen;
    const Dcp::Cpl &cpl = m_package.cpls[index];
    auto add = [&](const QString &id, const QString &path) {
        if (path.isEmpty() || seen.contains(path))
            return;
        seen.insert(path);
        const Dcp::Asset a = m_package.assets.value(id);
        items.append({QFileInfo(path).fileName(), path, a.hash, a.size});
    };
    add(cpl.id, cpl.file);
    for (const Dcp::Reel &r : cpl.reels)
        for (const Dcp::ReelAsset &a : r.assets)
            add(a.id, a.file);

    m_verifying = true;
    m_verifyProgress = 0;
    m_verifyResult.clear();
    m_verifyCancel = std::make_shared<std::atomic_bool>(false);
    emit verifyChanged();
    QPointer<DcpManager> self(this);
    auto cancel = m_verifyCancel;
    QThreadPool::globalInstance()->start([self, items, cancel] {
        qint64 total = 0, done = 0;
        for (const Item &i : items)
            total += std::max<qint64>(0, QFileInfo(i.path).size());
        QStringList bad, noHash;
        int ok = 0;
        QByteArray buf(4 * 1024 * 1024, '\0');
        for (const Item &i : items) {
            if (*cancel)
                break;
            QFile f(i.path);
            if (!f.open(QIODevice::ReadOnly)) {
                bad << i.name + LTR(" (nicht lesbar)");
                continue;
            }
            if (i.size >= 0 && f.size() != i.size) {
                bad << i.name + LTR(" (Größe)");
                done += f.size();
                continue;
            }
            QCryptographicHash h(QCryptographicHash::Sha1);
            while (!f.atEnd() && !*cancel) {
                const qint64 n = f.read(buf.data(), buf.size());
                if (n <= 0)
                    break;
                h.addData(QByteArrayView(buf.constData(), n));
                done += n;
                const double p = total ? double(done) / double(total) : 1.0;
                QMetaObject::invokeMethod(QCoreApplication::instance(), [self, p] {
                    if (self && std::abs(self->m_verifyProgress - p) > 0.004) {
                        self->m_verifyProgress = p;
                        emit self->verifyChanged();
                    }
                }, Qt::QueuedConnection);
            }
            if (i.hash.isEmpty())
                noHash << i.name;
            else if (QString::fromLatin1(h.result().toBase64()) == i.hash)
                ++ok;
            else
                bad << i.name + LTR(" (Prüfsumme)");
        }
        const QVariantMap result{{"ok", bad.isEmpty() && !*cancel}, {"checked", ok}, {"failed", bad},
                                 {"noHash", noHash}, {"cancelled", bool(*cancel)}};
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, result] {
            if (!self)
                return;
            self->m_verifying = false;
            self->m_verifyProgress = 1;
            self->m_verifyResult = result;
            emit self->verifyChanged();
            self->setStatus(result.value("cancelled").toBool() ? LTR("Prüfung abgebrochen")
                            : result.value("ok").toBool() ? LTR("DCP geprüft: %1 Dateien in Ordnung").arg(result.value("checked").toInt())
                                                          : LTR("DCP beschädigt: %1").arg(result.value("failed").toStringList().join(QStringLiteral(", "))));
        }, Qt::QueuedConnection);
    });
}

void DcpManager::cancelVerify()
{
    if (m_verifyCancel)
        *m_verifyCancel = true;
}
