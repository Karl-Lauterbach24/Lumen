#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariant>

class QSocketNotifier;

// Eingabegeräte, die weder Tastatur noch Maus sind: Fernbedienungen (USB, Bluetooth, Infrarot,
// HDMI-CEC) und Gamepads. Tastatur und Maus gehen ihren gewohnten Weg über das Fenstersystem und
// brauchen keine Einrichtung. Jedes andere Gerät wird einmal eingerichtet – der Nutzer drückt für
// jede Funktion die Taste, die sie haben soll – und danach direkt gelesen (Linux: evdev) und in
// die Funktionen von Lumen übersetzt.
//
// Ein Gerät ist, was der Kern an einem Anschluss meldet: mehrere Eingabeknoten derselben
// Fernbedienung (Tasten, Lautstärke, Maus-Teil) gehören zusammen. Was als Gerät zählt:
//   - kein Knoten davon ist eine vollständige Tastatur,
//   - zusammen wenigstens drei Tasten oder ein Steuerkreuz,
//   - es hängt an USB, Bluetooth, HDMI-CEC oder ist ein Infrarot-Empfänger (keine Tasten des
//     Rechners selbst: Netzschalter, Deckel, Sondertasten eines Notebooks).
class InputMapper : public QObject
{
    Q_OBJECT
    // [{id, name, mapped, connected}]
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    // so viele Funktionen fragt die Einrichtung in jedem Fall ab
    Q_PROPERTY(int requiredSteps READ requiredSteps CONSTANT)
    Q_PROPERTY(bool wizardActive READ wizardActive NOTIFY wizardChanged)
    Q_PROPERTY(QString wizardDevice READ wizardDevice NOTIFY wizardChanged)
    // [{id, optional, button}] – button: die zugewiesene Taste als Text, leer = noch offen
    Q_PROPERTY(QVariantList wizardSteps READ wizardSteps NOTIFY wizardChanged)
    Q_PROPERTY(int wizardStep READ wizardStep NOTIFY wizardChanged)
    // die zuletzt gedrückte Taste war schon vergeben: an diese Funktion
    Q_PROPERTY(QString wizardTaken READ wizardTaken NOTIFY wizardChanged)

public:
    // enabled: Geräte suchen und lesen (nur als Abspielgerät – auf einem Arbeitsrechner bleibt das
    // Gamepad den anderen Programmen)
    explicit InputMapper(bool enabled, QObject *parent = nullptr);
    ~InputMapper() override;

    // Funktionen in der Reihenfolge der Einrichtung; die ersten kRequired sind Pflicht
    static QStringList actions();
    static constexpr int kRequired = 6;

    bool available() const;
    int requiredSteps() const { return kRequired; }
    QVariantList devices() const;
    bool wizardActive() const { return !m_wizard.isEmpty(); }
    QString wizardDevice() const;
    QVariantList wizardSteps() const;
    int wizardStep() const { return m_step; }
    QString wizardTaken() const { return m_taken; }

    Q_INVOKABLE void startWizard(const QString &deviceId);
    // Wahlfreie Funktion auslassen
    Q_INVOKABLE void skipStep();
    Q_INVOKABLE void cancelWizard();
    Q_INVOKABLE void forget(const QString &deviceId);
    // Suche nach Geräten anstoßen (nach dem Koppeln per Bluetooth)
    Q_INVOKABLE void rescan();

    // LumenOS: Läuft ein Film ohne Fenstersystem direkt auf dem Bildschirm, kommt von der Tastatur
    // nichts mehr über das Fenstersystem an. So lange werden auch Tastaturen hier gelesen, mit fester
    // Belegung: Pfeile, Eingabe, Esc/Rücktaste, Leertaste, Bild auf/ab, M, I, A, S, die Medientasten.
    void setKeyboards(bool on);

    // Für Tests und für die Entwicklung ohne Gerät: ein Gerät mit diesem Namen "anschließen" und
    // Tasten darauf drücken (code: beliebige Zahl je Taste)
    Q_INVOKABLE void fakeDevice(const QString &name);
    Q_INVOKABLE void fakeButton(const QString &name, int code);
    // Wo die Zuordnungen liegen (Tests setzen einen eigenen Ort)
    void setStorePath(const QString &file);

signals:
    void devicesChanged();
    void wizardChanged();
    // Ein Gerät ohne Zuordnung ist da (angeschlossen, gekoppelt, oder eine Taste darauf gedrückt)
    void setupWanted(const QString &deviceId, const QString &name);
    void wizardFinished(const QString &deviceId, bool saved);
    // Eine zugeordnete Taste: up, down, left, right, ok, back, menu, playpause, stop, rewind,
    // forward, prev, next, volup, voldown, mute, info, audio, subtitle
    void action(const QString &name, bool repeat);

private:
    struct Node;
    struct Keyboard;
    struct Device
    {
        QString id;
        QString name;
        QList<Node *> nodes;
        QHash<QString, QString> map; // Taste -> Funktion
        bool fake = false;
        bool announced = false;
    };

    void scan();
    void openNode(const QString &path);
    void closeNode(Node *node);
    void readNode(Node *node);
    void button(Device *device, const QString &code, bool down);
    void wizardButton(Device *device, const QString &code);
    void advance();
    void finish(bool save);
    void applyGrab(Device *device);
    void readKeyboard(Keyboard *keyboard);
    void load();
    void save() const;
    QString storePath() const;
    Device *deviceById(const QString &id) const;

    QHash<QString, Device *> m_devices;       // nach Kennung
    QHash<QString, QVariantMap> m_stored;     // gespeicherte Zuordnungen: Kennung -> {name, map}
    QString m_store;
    QString m_wizard;                         // Kennung des Geräts in der Einrichtung
    int m_step = 0;
    QHash<QString, QString> m_assigned;       // Funktion -> Taste (laufende Einrichtung)
    QString m_taken;
    QTimer m_rescan;
    QTimer m_repeat;                          // gehaltene Taste wiederholen
    QTimer m_idle;                            // Einrichtung ohne Eingabe beenden
    Device *m_heldDevice = nullptr;
    QString m_heldCode;
    QObject *m_watcher = nullptr;
    QList<Keyboard *> m_keyboards;            // nur, während der Film den Bildschirm hat
    bool m_keyboardsOn = false;
    int m_retries = 0;
};
