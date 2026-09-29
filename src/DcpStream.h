#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <atomic>

struct mpv_handle;

// MXF-Spurdateien eines DCP als mpv-Stream ("lumendcp://<n>").
//
// Verschlüsselte KLV-Triplets (SMPTE ST 429-6) werden beim Lesen entschlüsselt
// und größengleich ersetzt:   [Triplet .......................]
//                          -> [Essenz-KLV (Klartext)][KLV-Fill]
// Alle Byte-Offsets der Datei – Index-Tabellen, Partitionen – bleiben dadurch
// gültig; FFmpeg's MXF-Demuxer spult und liest wie bei einer offenen Datei.
//
// Stereoskopische Spurdateien (SMPTE ST 429-10, linkes und rechtes Bild je
// Edit Unit) lassen sich auf ein Auge filtern: das andere Element wird zu Fill.
namespace Dcp {

struct StreamSpec
{
    QString file;
    QByteArray key;   // leer = unverschlüsselt bzw. kein Schlüssel
    int eye = 0;      // 0 = alle Elemente, 1 = linkes, 2 = rechtes Auge
};

// Prozessweite Registrierung (überlebt Neustarts der mpv-Instanz)
int registerStream(const StreamSpec &spec);
QString streamUrl(int id);
void clearStreams();
// mpv-Protokoll "lumendcp" anmelden (je mpv-Instanz)
void attachProtocol(mpv_handle *mpv);

// Diagnose: Triplets, deren Prüfwert nach dem Entschlüsseln nicht stimmt
// (falscher Schlüssel) bzw. ohne Schlüssel
extern std::atomic<int> g_keyErrors;
extern std::atomic<int> g_missingKeys;

// Timed-Text-XML (SMPTE-Untertitel) und eingebettete Ressourcen (Fonts, PNG)
// aus einer MXF-Datei holen, ggf. entschlüsseln
struct TimedText
{
    QByteArray xml;
    QList<QByteArray> resources;
};
TimedText readTimedText(const QString &file, const QByteArray &key);

// Für Tests: eine MXF-Datei vollständig durch den Leser schicken
QByteArray readAllTransformed(const StreamSpec &spec, qint64 maxBytes = -1);

} // namespace Dcp
