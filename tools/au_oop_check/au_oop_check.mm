// Out-of-process AU render check (macOS only; built and run by scripts/validate.sh).
//
// Logic Pro 12 hosts AUs out of process (AUHostingService). There, a render fails with
// kAudioUnitErr_NoConnection when an input bus has no connection, before the unit's own Render
// runs, so an instrument with an unconnected input is silent. In-process hosts (auval, pluginval)
// never see this. This check hosts the unit the way Logic does and requires sound.
//
// usage: au_oop_check <type> <subtype> <manufacturer> [--tone]
//   Instantiates the unit with kAudioComponentInstantiation_LoadOutOfProcess, connects no input
//   (or, with --tone, a render callback feeding a 220 Hz tone to input 0), plays C4/E4/G4 and
//   renders 2 s. Passes when every render returns noErr and the peak is above -40 dBFS.
//   Exit codes: 0 pass, 1 fail, 0 with "SKIP" when out-of-process hosting is unavailable here
//   (Apple's DLSMusicDevice cannot be hosted out of process either).
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>
#include <alloca.h>
#include <dispatch/dispatch.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr double kSampleRate = 48000;
constexpr UInt32 kFrames = 512;
constexpr double kSeconds = 2;
constexpr float kMinPeak = 0.01f;  // -40 dBFS

OSType fourCC(const char* s) {
  if (std::strlen(s) != 4) return 0;
  return (OSType(uint8_t(s[0])) << 24) | (OSType(uint8_t(s[1])) << 16) | (OSType(uint8_t(s[2])) << 8) |
         OSType(uint8_t(s[3]));
}

double gPhase = 0;
OSStatus toneInput(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames,
                   AudioBufferList* io) {
  for (UInt32 i = 0; i < frames; ++i) {
    const float v = 0.5f * float(std::sin(gPhase));
    gPhase += 2 * M_PI * 220.0 / kSampleRate;
    for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
      if (io->mBuffers[b].mData) static_cast<float*>(io->mBuffers[b].mData)[i] = v;
  }
  return noErr;
}

// Instantiates out of process; spins the main run loop while waiting (10 s timeout). Apple does not
// say which queue the completion handler runs on, so it hands its result over with a semaphore: the
// signal orders its writes before the main thread's reads.
OSStatus instantiateOutOfProcess(AudioComponent comp, AudioUnit* out) {
  __block AudioUnit made = nullptr;
  __block OSStatus status = kAudioUnitErr_FailedInitialization;
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  AudioComponentInstantiate(comp, kAudioComponentInstantiation_LoadOutOfProcess,
                            ^(AudioComponentInstance inst, OSStatus err) {
                              made = inst;
                              status = err;
                              dispatch_semaphore_signal(done);
                            });
  NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:10];
  bool finished = false;
  while (!(finished = dispatch_semaphore_wait(done, DISPATCH_TIME_NOW) == 0) && [deadline timeIntervalSinceNow] > 0)
    [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
  if (!finished) return kAudioUnitErr_FailedInitialization;
  *out = made;
  return status;
}

// Hosting control: Apple's DLS synth must load out of process, or this machine cannot do it.
bool outOfProcessAvailable(OSStatus* why) {
  AudioComponentDescription d = {kAudioUnitType_MusicDevice, kAudioUnitSubType_DLSSynth,
                                 kAudioUnitManufacturer_Apple, 0, 0};
  AudioComponent comp = AudioComponentFindNext(nullptr, &d);
  if (!comp) {
    *why = kAudioComponentErr_InstanceInvalidated;
    return false;
  }
  AudioUnit au = nullptr;
  *why = instantiateOutOfProcess(comp, &au);
  if (*why != noErr || !au) return false;
  AudioComponentInstanceDispose(au);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc < 4) {
      std::fprintf(stderr, "usage: au_oop_check <type> <subtype> <manufacturer> [--tone]\n");
      return 2;
    }
    const bool tone = argc > 4 && std::strcmp(argv[4], "--tone") == 0;
    char label[64];
    std::snprintf(label, sizeof label, "%s %s %s", argv[1], argv[2], argv[3]);

    AudioComponentDescription d = {fourCC(argv[1]), fourCC(argv[2]), fourCC(argv[3]), 0, 0};
    AudioComponent comp = AudioComponentFindNext(nullptr, &d);
    if (!comp) {
      std::printf("FAIL %s: component not found (is it installed?)\n", label);
      return 1;
    }
    OSStatus why = noErr;
    if (!outOfProcessAvailable(&why)) {
      std::printf("SKIP %s: out-of-process AU hosting is unavailable here (DLSMusicDevice: %d)\n",
                  label, int(why));
      return 0;
    }

    AudioUnit au = nullptr;
    OSStatus err = instantiateOutOfProcess(comp, &au);
    if (err != noErr || !au) {
      std::printf("FAIL %s: out-of-process instantiate %d\n", label, int(err));
      return 1;
    }

    AudioStreamBasicDescription fmt = {};
    fmt.mSampleRate = kSampleRate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    fmt.mBytesPerPacket = 4;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 4;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 32;
    UInt32 nOut = 0, nIn = 0, sz = sizeof(UInt32);
    AudioUnitGetProperty(au, kAudioUnitProperty_ElementCount, kAudioUnitScope_Output, 0, &nOut, &sz);
    sz = sizeof(UInt32);
    AudioUnitGetProperty(au, kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, 0, &nIn, &sz);
    for (UInt32 b = 0; b < nOut; ++b)
      AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, b, &fmt, sizeof fmt);
    for (UInt32 b = 0; b < nIn; ++b)
      AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, b, &fmt, sizeof fmt);
    UInt32 maxFrames = kFrames;
    AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames,
                         sizeof maxFrames);
    if (tone) {
      if (nIn == 0) {
        std::printf("FAIL %s: --tone given but the unit has no input bus\n", label);
        return 1;
      }
      AURenderCallbackStruct cb = {toneInput, nullptr};
      err = AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
      if (err != noErr) {
        std::printf("FAIL %s: input render callback %d\n", label, int(err));
        return 1;
      }
    }
    err = AudioUnitInitialize(au);
    if (err != noErr) {
      std::printf("FAIL %s: initialize %d\n", label, int(err));
      return 1;
    }

    std::vector<float> L(kFrames), R(kFrames);
    const int blocks = int(kSeconds * kSampleRate / kFrames);
    float peak = 0;
    int failedRenders = 0;
    OSStatus firstError = noErr;
    double sampleTime = 0;
    for (int b = 0; b < blocks; ++b) {
      if (b == 2)
        for (int key : {60, 64, 67}) MusicDeviceMIDIEvent(au, 0x90, UInt32(key), 100, 0);
      AudioBufferList* abl = static_cast<AudioBufferList*>(alloca(sizeof(AudioBufferList) + sizeof(AudioBuffer)));
      abl->mNumberBuffers = 2;
      abl->mBuffers[0] = {1, kFrames * 4, L.data()};
      abl->mBuffers[1] = {1, kFrames * 4, R.data()};
      AudioTimeStamp ts = {};
      ts.mSampleTime = sampleTime;
      ts.mFlags = kAudioTimeStampSampleTimeValid;
      AudioUnitRenderActionFlags flags = 0;
      const OSStatus r = AudioUnitRender(au, &flags, &ts, 0, kFrames, abl);
      sampleTime += kFrames;
      if (r != noErr) {
        if (failedRenders++ == 0) firstError = r;
        continue;
      }
      for (UInt32 c = 0; c < 2; ++c) {
        const float* p = static_cast<const float*>(abl->mBuffers[c].mData);
        for (UInt32 i = 0; i < kFrames; ++i)
          peak = std::isfinite(p[i]) ? std::max(peak, std::fabs(p[i])) : 1e9f;
      }
    }
    AudioUnitUninitialize(au);
    AudioComponentInstanceDispose(au);

    const double peakDb = peak > 0 ? 20 * std::log10(peak) : -999;
    const bool ok = failedRenders == 0 && peak > kMinPeak && peak < 1e9f;
    std::printf("%s %s: out of process, %u input bus(es)%s, %d/%d renders failed (first %d), peak %.1f dBFS%s\n",
                ok ? "PASS" : "FAIL", label, unsigned(nIn), tone ? " fed a tone" : " unconnected", failedRenders,
                blocks, int(firstError), peakDb, peak >= 1e9f ? " (non-finite output)" : "");
    return ok ? 0 : 1;
  }
}
