// descriptors.cpp
#include "descriptors.h"

namespace eg13 {

namespace {

const char* const kInstrumentFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                           CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                           CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const char* const kEffectFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
                                       CLAP_PLUGIN_FEATURE_FILTER,
                                       CLAP_PLUGIN_FEATURE_STEREO, nullptr};

constexpr const char* kUrl = "https://github.com/SymbioticAudio/EG13";
constexpr const char* kDescription =
    "Bi-timbral synthesizer built on Emilie Gillet's open-source Eurorack DSP.";

}  // namespace

const clap_plugin_descriptor_t kInstrumentDescriptor = {
    CLAP_VERSION_INIT, "com.symbioticaudio.eg13", EG13_DISPLAY_NAME, "SymbioticAudio",
    kUrl,              kUrl,                      kUrl,              EG13_VERSION,
    kDescription,      kInstrumentFeatures};

const clap_plugin_descriptor_t kEffectDescriptor = {
    CLAP_VERSION_INIT, "com.symbioticaudio.eg13.fx", EG13_DISPLAY_NAME " FX", "SymbioticAudio",
    kUrl,              kUrl,                         kUrl,                     EG13_VERSION,
    kDescription,      kEffectFeatures};

}  // namespace eg13
