// IAB-Testmaterial (SMPTE ST 2098-2) mit dem Packer des DTS-IAB-Renderers:
//
//   iab_testgen <frames.bin> [frames=48]
//
// 24 fps, 48 kHz. Inhalt: 5.1-Bett mit 50-Hz-Ton nur im LFE, dazu ein Objekt mit
// 1-kHz-Ton – in der ersten Hälfte vorne links auf Ohrhöhe (x=0, y=0, z=0), in
// der zweiten oben vorne rechts (x=1, y=0, z=1). frames.bin: je Frame
// [uint32 LE Länge][IA-Bitstream-Frame (Preamble + IAFrame)], wie ein
// MXF-Essenz-Element; tools/make_test_dcp.py --iab verpackt es als DCP-Spur.
#include "IABElementsAPI.h"
#include "IABPackerAPI.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

using namespace SMPTE::ImmersiveAudioBitstream;

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: iab_testgen <frames.bin> [frames]\n");
        return 2;
    }
    const int frames = argc > 2 ? std::atoi(argv[2]) : 48;
    const int samples = 2000; // 48000 / 24
    const double pi = 3.14159265358979;

    IABPackerInterface *packer = IABPackerInterface::Create();
    if (packer->SetFrameRate(kIABFrameRate_24FPS) != kIABNoError || packer->SetSampleRate(kIABSampleRate_48000Hz) != kIABNoError)
        return 1;

    // Objekt: Metadaten-ID 1, Audio-ID 1
    IABAudioDataIDType audioId = 1;
    const IABMetadataIDType objectId = 1;
    const IABAudioDataIDType objectAudio = audioId++;
    IABObjectDefinitionInterface *object = nullptr;
    if (packer->AddObjectDefinition(objectId, objectAudio, object) != kIABNoError)
        return 1;

    // 5.1-Bett: Metadaten-ID 2
    IABBedMappingInfo bed(kIABUseCase_5_1, 2);
    std::vector<IABAudioDataIDType> bedAudio;
    for (IABChannelIDType ch : {kIABChannelID_Left, kIABChannelID_Center, kIABChannelID_Right, kIABChannelID_LeftSurround,
                                kIABChannelID_RightSurround, kIABChannelID_LFE}) {
        bed.lookupMap_[ch] = audioId;
        bedAudio.push_back(audioId++);
    }
    const IABAudioDataIDType lfeAudio = bedAudio.back();
    IABBedDefinitionInterface *bedDef = nullptr;
    if (packer->AddBedDefinition(bed.getMetadataID(), bed.getUseCase(), bed, bedDef) != kIABNoError)
        return 1;

    std::vector<IABAudioDataIDType> all{objectAudio};
    all.insert(all.end(), bedAudio.begin(), bedAudio.end());
    if (packer->AddDLCElements(all) != kIABNoError)
        return 1;
    packer->AddAuthoringToolInfo("Lumen iab_testgen");

    FILE *out = std::fopen(argv[1], "wb");
    if (!out)
        return 1;
    std::map<IABAudioDataIDType, std::vector<int32_t>> pcm;
    for (IABAudioDataIDType id : all)
        pcm[id].assign(samples, 0);

    for (int f = 0; f < frames; ++f) {
        // Ton: 24-Bit-Werte, linksbündig in int32
        for (int i = 0; i < samples; ++i) {
            const double t = double(f * samples + i) / 48000.0;
            pcm[objectAudio][i] = int32_t(std::lround(0.25 * std::sin(2 * pi * 1000 * t) * 8388607.0)) * 256;
            pcm[lfeAudio][i] = int32_t(std::lround(0.25 * std::sin(2 * pi * 50 * t) * 8388607.0)) * 256;
        }
        std::map<IABAudioDataIDType, int32_t *> sources;
        for (auto &[id, v] : pcm)
            sources[id] = v.data();
        if (packer->UpdateAudioSamples(sources) != kIABNoError)
            return 1;

        IABObjectPanningParameters pan;
        pan.panInfoExists_ = 1;
        const bool secondHalf = f >= frames / 2;
        pan.position_.setIABObjectPosition(secondHalf ? 1.0f : 0.0f, 0.0f, secondHalf ? 1.0f : 0.0f);
        pan.spread_.setIABObjectSpread(kIABSpreadMode_None, 0.0f, 0.0f, 0.0f);
        pan.objectGain_.setIABGain(1.0f);
        if (packer->UpdateObjectMetaData(objectId, std::vector<IABObjectPanningParameters>(8, pan)) != kIABNoError)
            return 1;

        if (packer->PackIABFrame() != kIABNoError)
            return 1;
        std::vector<char> buffer;
        uint32_t length = 0;
        if (packer->GetPackedBuffer(buffer, length) != kIABNoError || length == 0)
            return 1;
        const unsigned char len[4] = {uint8_t(length), uint8_t(length >> 8), uint8_t(length >> 16), uint8_t(length >> 24)};
        std::fwrite(len, 1, 4, out);
        std::fwrite(buffer.data(), 1, length, out);
    }
    std::fclose(out);
    IABPackerInterface::Delete(packer);
    std::printf("%d IAB-Frames geschrieben: %s\n", frames, argv[1]);
    return 0;
}
