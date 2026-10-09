#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Der Klang der Oberfläche von LumenOS: kurze Töne für das, was man tut, und eine ruhige Klangfläche in
// den Menüs. Beides sind Dateien (resources/sounds; eigene in /etc/lumenos/sounds gehen vor), hier
// gelesen, gemischt und ausgegeben.
//
// Ausgegeben wird über ALSA auf demselben Ausgang, den der Film bekommt – und nur, solange kein Film
// läuft: Der Ausgang am Bildschirm lässt sich nur einmal öffnen, und der Film braucht ihn für sich
// (suspend() gibt ihn frei und kehrt erst zurück, wenn er frei ist).
// Ohne ALSA gebaut (macOS, Windows) tut diese Klasse nichts.
class UiAudio : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool effects READ effects WRITE setEffects NOTIFY changed)
    Q_PROPERTY(bool music READ music WRITE setMusic NOTIFY changed)
    Q_PROPERTY(qreal effectsVolume READ effectsVolume WRITE setEffectsVolume NOTIFY changed)
    Q_PROPERTY(qreal musicVolume READ musicVolume WRITE setMusicVolume NOTIFY changed)

public:
    explicit UiAudio(bool enabled, QObject *parent = nullptr);
    ~UiAudio() override;

    bool available() const { return m_enabled; }
    bool effects() const { return m_effects; }
    bool music() const { return m_music; }
    qreal effectsVolume() const { return m_effectsVolume; }
    qreal musicVolume() const { return m_musicVolume; }
    void setEffects(bool on);
    void setMusic(bool on);
    void setEffectsVolume(qreal volume);
    void setMusicVolume(qreal volume);

    // "move", "key", "select", "back", "on", "off", "error", "done", "start"
    Q_INVOKABLE void play(const QString &name);

    // der Ausgang, wie mpv ihn nennt ("alsa/hdmi:CARD=PCH,DEV=0"; "auto" und alles andere: der des Systems)
    void setDevice(const QString &mpvDevice);
    // Ein Film beginnt: ausblenden und den Ausgang schließen (kehrt zurück, wenn er zu ist)
    void suspend();
    void resume();

    // Eine Tondatei, gelesen und auf 48 kHz Stereo gebracht (links und rechts abwechselnd); leer, wenn es nicht ging
    static std::vector<float> decodeFile(const QString &path);
    static QString findFile(const QString &name);

signals:
    void changed();

private:
    struct Clip
    {
        std::vector<float> samples;
    };
    struct Voice
    {
        std::shared_ptr<const Clip> clip;
        size_t at = 0;
        float gain = 1;
    };
    std::shared_ptr<const Clip> clip(const QString &name);
    void run();
    void wake();

    const bool m_enabled;
    bool m_effects = true;
    bool m_music = true;
    qreal m_effectsVolume = 0.7;
    qreal m_musicVolume = 0.5;
    QHash<QString, std::shared_ptr<const Clip>> m_clips;

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_closed;
    // (unter m_mutex)
    std::vector<Voice> m_pending;
    std::shared_ptr<const Clip> m_musicClip;
    QByteArray m_device = "default";
    bool m_musicOn = false;
    bool m_suspended = false;
    bool m_open = false;
    bool m_quit = false;
    std::atomic<float> m_musicLevel{0.25f};
};
