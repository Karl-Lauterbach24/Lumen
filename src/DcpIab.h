#pragma once

#include "DcpStream.h"

#include <QList>
#include <QString>
#include <QStringList>

struct mpv_handle;

// Immersive Audio (SMPTE ST 2098-2 IAB, auch Dolby-Atmos-DCP-Spuren nach ST 429-18):
// Frames aus der (ggf. verschlüsselten) Spurdatei lesen, mit dem offenen
// DTS-IAB-Renderer (BSD-3) auf ein Lautsprecherlayout rendern und als
// WAV-Stream ("lumeniab://<n>", 32-Bit-Float, Kanalmaske) an mpv geben.
namespace Dcp {

struct IabLayout
{
    QString id;   // "7.1.4", "5.1.4", "7.1", "5.1", "2.0"
    QString name; // Anzeige
};

bool iabAvailable();              // mit IAB-Renderer gebaut
QList<IabLayout> iabLayouts();
QString defaultIabLayout();       // "7.1.4"

class IabDecoder
{
public:
    IabDecoder(const StreamSpec &spec, const QString &layout);
    ~IabDecoder();
    IabDecoder(const IabDecoder &) = delete;
    IabDecoder &operator=(const IabDecoder &) = delete;

    bool ok() const;
    QString error() const;
    int frames() const;
    int channels() const;
    quint32 channelMask() const;   // WAVE_FORMAT_EXTENSIBLE
    QStringList channelNames() const;
    int sampleRate() const;
    int frameSamples() const;
    // Frame -> frameSamples() × channels() Float-Samples (verschachtelt); false = Stille
    bool render(int frame, float *interleaved);
    // Frame-Fehler seit Beginn (Parser/Renderer)
    int errors() const;

private:
    struct Impl;
    Impl *d;
};

int registerIabStream(const StreamSpec &spec, const QString &layout);
QString iabUrl(int id);
void attachIabProtocol(mpv_handle *mpv);

} // namespace Dcp
