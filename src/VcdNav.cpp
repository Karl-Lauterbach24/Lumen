#include "VcdNav.h"

namespace {

constexpr int kDisabled = 0xffff;
constexpr int kMultiDefault = 0xfffe;       // Default je nach gespieltem Einsprungpunkt
constexpr int kMultiDefaultNoNum = 0xfffd;
constexpr int kSegmentSectors = 150;        // Segment-Play-Items: 150 Sektoren je Segment

enum Type { PlayList = 0x10, Selection = 0x18, ExtSelection = 0x1a, EndList = 0x1f, CommandList = 0x20 };

int be16(const QByteArray &b, int o)
{
    return o + 1 < b.size() ? (uchar(b[o]) << 8) | uchar(b[o + 1]) : kDisabled;
}

int bcd(uchar v)
{
    return (v >> 4) * 10 + (v & 0x0f);
}

} // namespace

VcdNav::VcdNav(QObject *parent)
    : QObject(parent)
{
    m_waitTimer.setSingleShot(true);
    m_digitTimer.setSingleShot(true);
    m_digitTimer.setInterval(1200);
    connect(&m_digitTimer, &QTimer::timeout, this, [this] { key(QStringLiteral("commit")); });
}

bool VcdNav::hasPbc(const QString &source)
{
    const Optical::VcdDisc d = Optical::readVcdDisc(source);
    return d.ok && be16(d.lot, 2) != kDisabled;
}

// Wartezeit-Kodierung (wtime/atime/totime): 1–60 s direkt, 61–254 in 10-s-Schritten, 255 = unendlich
int VcdNav::waitSeconds(int code)
{
    if (code >= 255)
        return -1;
    if (code <= 60)
        return code;
    return (code - 60) * 10 + 60;
}

bool VcdNav::start(const QString &source)
{
    stop();
    m_disc = Optical::readVcdDisc(source);
    if (!m_disc.ok)
        return false;
    m_source = source;
    m_mult = m_disc.info.size() > 51 && uchar(m_disc.info[51]) ? uchar(m_disc.info[51]) : 8;
    m_active = true;
    // LOT: 2 Byte reserviert, dann Offset je List-ID ab 1
    if (!go(be16(m_disc.lot, 2))) {
        m_active = false;
        return false;
    }
    return true;
}

void VcdNav::stop()
{
    m_waitTimer.stop();
    m_digitTimer.stop();
    m_digits.clear();
    if (!m_active)
        return;
    m_active = false;
    m_waiting = false;
    emit stateChanged();
    emit stopped();
}

bool VcdNav::selection() const
{
    return m_active && (m_list.type == Selection || m_list.type == ExtSelection);
}

bool VcdNav::parse(int value, List *l) const
{
    if (value >= kMultiDefaultNoNum)
        return false;
    const QByteArray &p = m_disc.psd;
    const int o = value * m_mult;
    if (o < 0 || o >= p.size())
        return false;
    l->type = uchar(p[o]);
    switch (l->type) {
    case PlayList: {
        const int noi = o + 1 < p.size() ? uchar(p[o + 1]) : 0;
        l->lid = be16(p, o + 2) & 0x7fff;
        l->prev = be16(p, o + 4);
        l->next = be16(p, o + 6);
        l->ret = be16(p, o + 8);
        l->wait = o + 12 < p.size() ? waitSeconds(uchar(p[o + 12])) : 0;
        for (int i = 0; i < noi; ++i)
            l->items << be16(p, o + 14 + 2 * i);
        return true;
    }
    case Selection:
    case ExtSelection: {
        if (o + 20 > p.size())
            return false;
        const int nos = uchar(p[o + 2]);
        l->bsn = uchar(p[o + 3]);
        l->lid = be16(p, o + 4) & 0x7fff;
        l->prev = be16(p, o + 6);
        l->next = be16(p, o + 8);
        l->ret = be16(p, o + 10);
        l->def = be16(p, o + 12);
        l->timeout = be16(p, o + 14);
        l->wait = waitSeconds(uchar(p[o + 16]));
        l->loop = uchar(p[o + 17]) & 0x7f;
        const int item = be16(p, o + 18);
        if (item >= 2)
            l->items << item;
        for (int i = 0; i < nos; ++i)
            l->choices << be16(p, o + 20 + 2 * i);
        return true;
    }
    case EndList:
        l->changePic = be16(p, o + 2);
        return true;
    default: // Command List u. a.: nicht unterstützt -> wie End List
        l->type = EndList;
        return true;
    }
}

bool VcdNav::go(int value)
{
    if (!m_active)
        return false;
    if (value == kMultiDefault || value == kMultiDefaultNoNum)
        value = m_list.choices.value(0, kDisabled);
    List l;
    if (value == kDisabled || !parse(value, &l))
        return false;
    m_list = l;
    m_lid = l.lid;
    enter();
    return true;
}

void VcdNav::enter()
{
    m_waitTimer.stop();
    m_waiting = false;
    m_item = 0;
    m_loops = m_list.loop;
    if (m_list.type == EndList) {
        stop();
        return;
    }
    emit stateChanged();
    if (m_list.items.isEmpty())
        listDone();
    else
        playItem(m_list.items.first());
}

void VcdNav::playItem(int itemId)
{
    bool still = false;
    const QString url = itemUrl(itemId, &still);
    m_still = still;
    if (url.isEmpty()) {
        itemFinished(); // nicht abspielbar (z. B. Element 0/1): überspringen
        return;
    }
    emit playRequested(url, still);
}

void VcdNav::itemFinished()
{
    if (!m_active || m_waiting)
        return;
    if (m_list.type == PlayList) {
        if (++m_item < m_list.items.size()) {
            playItem(m_list.items.at(m_item));
            return;
        }
        listDone();
        return;
    }
    // Selection: bewegte Menüs wiederholen (loop, 0 = endlos), Standbilder stehen lassen
    if (!m_still && !m_list.items.isEmpty() && (m_loops == 0 || --m_loops > 0)) {
        playItem(m_list.items.first());
        return;
    }
    listDone();
}

void VcdNav::listDone()
{
    m_waiting = true;
    emit stateChanged();
    const int target = m_list.type == PlayList ? m_list.next : m_list.timeout;
    if (target == kDisabled) {
        if (m_list.type == PlayList)
            stop(); // Ende der Steuerung
        return;     // Auswahl: auf Eingabe warten
    }
    if (m_list.wait < 0)
        return; // unendlich warten
    m_waitTimer.disconnect();
    connect(&m_waitTimer, &QTimer::timeout, this, [this, target] { go(target); });
    m_waitTimer.start(m_list.wait * 1000);
}

bool VcdNav::key(const QString &name)
{
    if (!m_active)
        return false;
    const bool sel = selection();
    if (name.size() == 1 && name.at(0).isDigit()) {
        if (!sel)
            return false;
        m_digits += name;
        const int maxNumber = m_list.bsn + int(m_list.choices.size()) - 1;
        if (m_digits.size() >= 2 || maxNumber < 10 || m_digits.toInt() * 10 > maxNumber)
            return key(QStringLiteral("commit"));
        m_digitTimer.start();
        return true;
    }
    if (name == QLatin1String("commit")) {
        m_digitTimer.stop();
        const int index = m_digits.toInt() - m_list.bsn;
        m_digits.clear();
        return sel && index >= 0 && index < m_list.choices.size() && go(m_list.choices.at(index));
    }
    if (name == QLatin1String("enter"))
        return sel && go(m_list.def);
    if (name == QLatin1String("next") || name == QLatin1String("right"))
        return go(m_list.next);
    if (name == QLatin1String("prev") || name == QLatin1String("left"))
        return go(m_list.prev);
    if (name == QLatin1String("return") || name == QLatin1String("menu"))
        return go(m_list.ret);
    return false;
}

QString VcdNav::itemUrl(int itemId, bool *still) const
{
    *still = false;
    const auto &d = m_disc;
    if (itemId >= 2 && itemId <= 99) { // Track
        if (!d.tracks.contains(itemId))
            return {};
        const auto t = d.tracks.value(itemId);
        return Optical::sectorUrl(m_source, t.first, t.second);
    }
    if (itemId >= 100 && itemId <= 599) { // Einsprungpunkt bis Trackende
        const int index = itemId - 100;
        if (index >= d.entries.size())
            return {};
        const auto e = d.entries.at(index);
        if (!d.tracks.contains(e.first))
            return {};
        return Optical::sectorUrl(m_source, e.second, d.tracks.value(e.first).second);
    }
    if (itemId >= 1000 && itemId <= 2979) { // Segment (ggf. mehrere zusammenhängende)
        const int k = itemId - 1000;
        const QByteArray &info = d.info;
        if (info.size() < 56 + k + 1)
            return {};
        const auto *m = reinterpret_cast<const uchar *>(info.constData()) + 48;
        const int firstSeg = (bcd(m[0]) * 60 + bcd(m[1])) * 75 + bcd(m[2]) - 150;
        const auto spi = [&](int i) { return uchar(info[56 + i]); };
        int end = k + 1;
        while (56 + end < info.size() && end < 1980 && (spi(end) & 0x20)) // Fortsetzung
            ++end;
        const int video = (spi(k) >> 2) & 7;
        *still = video == 1 || video == 2 || video == 5 || video == 6;
        return Optical::sectorUrl(m_source, firstSeg + k * kSegmentSectors, firstSeg + end * kSegmentSectors - 1);
    }
    return {};
}

QVariantMap VcdNav::state() const
{
    QVariantMap m;
    m["lid"] = m_lid;
    m["active"] = m_active;
    m["type"] = m_list.type == PlayList ? QStringLiteral("play") : selection() ? QStringLiteral("selection") : QStringLiteral("end");
    QVariantList choices;
    for (int i = 0; i < m_list.choices.size(); ++i)
        if (m_list.choices.at(i) != kDisabled)
            choices << m_list.bsn + i;
    m["choices"] = choices;
    m["prev"] = m_list.prev != kDisabled;
    m["next"] = m_list.next != kDisabled;
    m["return"] = m_list.ret != kDisabled;
    m["default"] = selection() && m_list.def != kDisabled;
    m["still"] = m_still;
    m["waiting"] = m_waiting;
    return m;
}
