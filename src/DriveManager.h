#pragma once

#include <QObject>
#include <QTimer>
#include <QVariant>

// Erkennt optische Laufwerke (inkl. LibreDrive-fähiger BD/UHD-Laufwerke) und
// eingehängte Blu-ray-Strukturen (Discs, gemountete ISOs, BDMV-Ordner).
//
// Lumen liest die Disc ausschließlich über libbluray. Eine eventuell nötige
// AACS/BD+-Entschlüsselung findet NICHT in Lumen statt, sondern in der vom
// Nutzer installierten Umgebung (LibreDrive-Firmware + externe Bibliothek).
class DriveManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList drives READ drives NOTIFY drivesChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)

public:
    explicit DriveManager(QObject *parent = nullptr);
    ~DriveManager() override;

    QVariantList drives() const { return m_drives; }
    bool scanning() const { return m_scanning; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool eject(const QString &device);
    // Prüft einen beliebigen Ordner/ISO auf Blu-ray-Struktur
    Q_INVOKABLE QVariantMap inspect(const QString &path) const;

signals:
    void drivesChanged();
    void scanningChanged();
    void discInserted(const QVariantMap &drive);

private:
    static QVariantList scan();
    void applyScan(const QVariantList &list);

    QTimer m_poll;
    QVariantList m_drives;
    bool m_scanning = false;
};
