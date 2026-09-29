#pragma once

#include <QObject>
#include <QVariant>

// Ausgabeprofile: ein Profil beschreibt ein Zielgerät (z. B. "1080p DLP 3D-Beamer",
// "4K HDR LED-Projektor") und alle dafür optimalen Wiedergabe-Einstellungen.
// Eingebaute Vorlagen kommen aus :/qt/qml/Lumen/profiles/presets.json,
// eigene Profile liegen als JSON im Konfigurationsordner.
class ProfileManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString currentId READ currentId WRITE setCurrentId NOTIFY currentProfileChanged)
    Q_PROPERTY(QVariantMap current READ currentProfile NOTIFY currentProfileChanged)

public:
    explicit ProfileManager(QObject *parent = nullptr);

    QVariantList profiles() const { return m_profiles; }
    QString currentId() const { return m_currentId; }
    void setCurrentId(const QString &id);
    QVariantMap currentProfile() const;

    Q_INVOKABLE QVariantMap profileById(const QString &id) const;
    Q_INVOKABLE QVariantMap defaults() const;
    // Speichert (neu oder ersetzt). Gibt die ID zurück.
    Q_INVOKABLE QString saveProfile(const QVariantMap &profile);
    Q_INVOKABLE QString duplicateProfile(const QString &id);
    Q_INVOKABLE void deleteProfile(const QString &id);
    Q_INVOKABLE void resetBuiltin(const QString &id);
    // Die daraus resultierenden mpv-Optionen (für die "Effektiv"-Ansicht)
    Q_INVOKABLE QVariantMap mpvOptions(const QVariantMap &profile) const;
    Q_INVOKABLE QString configPath() const;

    static QVariantMap toMpvOptions(const QVariantMap &profile);

signals:
    void profilesChanged();
    void currentProfileChanged();

private:
    void load();
    void persist() const;

    QVariantList m_builtin;
    QVariantList m_profiles;
    QString m_currentId;
};
