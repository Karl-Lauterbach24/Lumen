#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QVariant>

struct mpv_handle;

// Weitere optische Formate neben Blu-ray und DVD:
//
//   Video-CD / Super Video-CD  – MPEG-1/2 in Mode-2-Form-2-Sektoren. libcdio liest
//                                Laufwerke und Abbilder (CUE/BIN, NRG, TOC) sektorgenau;
//                                Einsprungpunkte (ENTRIES.VCD/SVD) werden zu Kapiteln.
//                                Ohne libcdio: MPEGAV/*.DAT bzw. MPEG2/*.MPG über das
//                                Dateisystem.
//   Audio-CD (CD-DA)           – Laufwerke und Abbilder über libcdio (Protokoll "lumencdda",
//                                Kapitel = Tracks, Namen aus CD-Text); ohne libcdio mpv "cdda://"
//   HD DVD                     – HVDVD_TS/*.EVO; Titel/Kapitel aus den Advanced-Content-
//                                Playlists (ADV_OBJ/*.XPL), sonst aus den EVO-Dateien.
//                                AACS-geschützte HD DVDs werden nicht entschlüsselt.
namespace Optical {

bool cdioAvailable();
// EDL-Text -> "edl://…"-URL (Zeilen durch ; getrennt)
QString edlUrl(const QString &edl);

// "vcd", "svcd", "cdda", "hddvd" oder leer
QString detect(const QString &path);
bool isImage(const QString &path); // .cue/.bin/.nrg/.toc

// Titel/Tracks für die Anzeige: {kind, discName, titles:[{index, label, duration, chapters, main}], error}
QVariantMap scan(const QString &path, const QString &kind);

// MusicBrainz-Disc-ID einer Audio-CD aus dem Inhaltsverzeichnis: Nummer des ersten und
// letzten Tracks, Lead-out und Trackanfänge als Sektoren inkl. 150 Sektoren Vorlauf
// (https://musicbrainz.org/doc/Disc_ID_Calculation). Nur eine Kennung – der Abruf von
// Titeln ist Sache eines Plugins.
QString musicBrainzDiscId(int firstTrack, int lastTrack, int leadout, const QList<int> &offsets);

// mpv-Protokolle "lumenvcd" (Mode-2-Form-2-Nutzdaten eines Tracks) und "lumencdda"
// (Audio-Sektoren als WAV)
void attachProtocol(mpv_handle *mpv);

struct Prepared
{
    QString url;
    QVariantMap options;
    QString error;
};
// title < 0: alles bzw. Hauptfilm
Prepared prepare(const QString &path, const QString &kind, int title);

// Video-CD-Wiedergabesteuerung (PBC): Rohdaten der Steuerdateien
struct VcdDisc
{
    bool ok = false;
    bool svcd = false;
    QByteArray info, lot, psd;                 // INFO/LOT/PSD (.VCD bzw. .SVD)
    QList<QPair<int, int>> entries;            // Einsprungpunkt -> (Track, LSN)
    QHash<int, QPair<int, int>> tracks;        // Track -> (erster, letzter LSN)
};
VcdDisc readVcdDisc(const QString &path);
// Beliebiger Sektorbereich (Mode 2 Form 2) als "lumenvcd://<n>"
QString sectorUrl(const QString &path, int firstLsn, int lastLsn);

} // namespace Optical
