#pragma once

#include <QObject>
#include <QProcess>

// Disc auf den Speicher kopieren: lässt MakeMKV (makemkvcon, wenn der Nutzer es eingerichtet hat)
// die Titel einer Disc als MKV-Dateien in einen Ordner schreiben und meldet den Fortschritt.
// Lumen selbst liest dabei nichts von der Disc – das Laufwerk gehört so lange MakeMKV.
class RipManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)   // 0..1, -1 = unbekannt
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QString target READ target NOTIFY changed)       // Ordner der laufenden bzw. letzten Kopie

public:
    explicit RipManager(QObject *parent = nullptr);
    ~RipManager() override;

    bool available() const;
    bool running() const { return m_process != nullptr; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString lastError() const { return m_error; }
    QString target() const { return m_target; }

    // device: das Laufwerk (/dev/sr0) oder ein Abbild; folder: Ort in der Mediathek. Die Kopie kommt
    // in einen Unterordner mit dem Namen der Disc. Titel unter zwei Minuten bleiben weg.
    Q_INVOKABLE void start(const QString &device, const QString &folder, const QString &name = QString());
    Q_INVOKABLE void cancel();

    // Eine Zeile aus makemkvcons Ausgabe (-r) auswerten; für Tests offen
    void parseLine(const QString &line);
    // Felder einer solchen Zeile: Zahlen und Texte in Anführungszeichen, durch Kommas getrennt
    static QStringList fields(const QString &text);

signals:
    void availableChanged();
    void changed();
    void finished(bool ok, const QString &folder);

private:
    QString program() const;
    void done(bool ok, const QString &error);

    QProcess *m_process = nullptr;
    QByteArray m_buffer;
    double m_progress = -1;
    QString m_status, m_error, m_target, m_step, m_lastMessage;
    int m_saved = 0;
    bool m_cancelled = false;
};
