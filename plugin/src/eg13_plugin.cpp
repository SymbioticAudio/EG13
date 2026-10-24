#include "eg13_plugin.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "state/state.h"
#include "ui_bridge.h"
#include "util/denormal_guard.h"
#include "synth/param_table.h"

namespace eg13 {

namespace {

struct Plugin {
  Plugin(const clap_host_t* h, PluginKind k) : host(h), kind(k), synth(k) {}
  clap_plugin_t clap{};
  const clap_host_t* host;
  const clap_host_params_t* hostParams = nullptr;
  PluginKind kind;
  const clap_host_latency_t* hostLatency = nullptr;
  uint32_t reportedLatency = 0;
  std::atomic<bool> active{false};
  std::atomic<bool> restartRequested{false};  // cleared by activate
  Synth synth;
  state::StateStore store;
  // Set by the editor (Task 6); until then null. Drained by process and params flush.
  std::atomic<UiBridge*> uiBridge{nullptr};
};

Plugin* self(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

// The Latency Mode applies at the next activate (it rebuilds every converter and changes the
// latency): when the parameter differs from the running mode, ask the host once to restart the
// plugin (deactivate, activate). request_restart is thread-safe (clap/host.h). CLAP and VST3
// hosts restart at once (the VST3 wrapper turns the request into
// restartComponent(kIoChanged | kLatencyChanged): third_party/clap-wrapper wrapasvst3.cpp:1352-1355
// and :1509-1515); clap-wrapper's AU restartPlugin() is empty, so an AU host applies it
// when it next initialises the plugin (docs/manual/latency.md).
void requestRestartIfModeChanged(Plugin* plugin) {
  if (!plugin->active.load(std::memory_order_acquire)) return;  // the next activate prepares it
  if (plugin->synth.requestedQuality() == plugin->synth.preparedQuality()) return;
  if (!plugin->restartRequested.exchange(true, std::memory_order_acq_rel)) {
    plugin->host->request_restart(plugin->host);
  }
}

// The host's transport (CLAP beat times are fixed point) for the beat clock.
Transport transportFrom(const clap_event_transport_t& t) {
  Transport out;
  out.playing = (t.flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
  out.hasTempo = (t.flags & CLAP_TRANSPORT_HAS_TEMPO) != 0;
  out.tempo = t.tempo;
  out.hasBeats = (t.flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
  out.beats = double(t.song_pos_beats) / double(CLAP_BEATTIME_FACTOR);
  out.barStart = double(t.bar_start) / double(CLAP_BEATTIME_FACTOR);
  out.hasTimeSignature = (t.flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) != 0;
  out.numerator = t.tsig_num;
  out.denominator = t.tsig_denom;
  return out;
}

// ---- events -----------------------------------------------------------------

void handleEvent(Plugin* plugin, const clap_event_header_t* hdr) {
  if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
  switch (hdr->type) {
    case CLAP_EVENT_NOTE_ON: {
      const auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
      if (ev->key >= 0 && ev->key <= 127) {
        plugin->synth.noteOn(ev->channel < 0 ? 0 : ev->channel, ev->key, float(ev->velocity));
      }
      break;
    }
    case CLAP_EVENT_NOTE_OFF:
    case CLAP_EVENT_NOTE_CHOKE: {
      const auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
      if (ev->key < 0) {
        plugin->synth.allNotesOff();
      } else if (hdr->type == CLAP_EVENT_NOTE_OFF) {
        plugin->synth.noteOff(ev->channel, ev->key, float(ev->velocity));  // release velocity
      } else {
        plugin->synth.noteOff(ev->channel, ev->key);
      }
      break;
    }
    case CLAP_EVENT_NOTE_EXPRESSION: {
      // Provisional ruling 12: tuning, pressure and brightness; the others are not used yet.
      const auto* ev = reinterpret_cast<const clap_event_note_expression_t*>(hdr);
      if (ev->key > 127) break;  // key -1 (a wildcard): every note, as Part::noteExpression reads it
      if (ev->expression_id == CLAP_NOTE_EXPRESSION_TUNING) {
        plugin->synth.noteExpression(ev->channel, ev->key, Expression::Bend, float(ev->value));
      } else if (ev->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE) {
        plugin->synth.noteExpression(ev->channel, ev->key, Expression::Pressure, float(ev->value));
      } else if (ev->expression_id == CLAP_NOTE_EXPRESSION_BRIGHTNESS) {
        plugin->synth.noteExpression(ev->channel, ev->key, Expression::Slide, float(ev->value));
      }
      break;
    }
    case CLAP_EVENT_TRANSPORT: {
      plugin->synth.setTransport(transportFrom(*reinterpret_cast<const clap_event_transport_t*>(hdr)));
      break;
    }
    case CLAP_EVENT_PARAM_VALUE: {
      const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(hdr);
      plugin->synth.setParam(ev->param_id, ev->value);
      break;
    }
    case CLAP_EVENT_MIDI: {
      const auto* ev = reinterpret_cast<const clap_event_midi_t*>(hdr);
      const uint8_t status = ev->data[0] & 0xF0;
      const int channel = ev->data[0] & 0x0F;
      const int d1 = ev->data[1] & 0x7F;
      const int d2 = ev->data[2] & 0x7F;
      if (status == 0x90 && d2 > 0) {
        plugin->synth.noteOn(channel, d1, float(d2) / 127.0f);
      } else if (status == 0x80) {
        plugin->synth.noteOff(channel, d1, float(d2) / 127.0f);  // with release velocity
      } else if (status == 0x90) {
        plugin->synth.noteOff(channel, d1);  // velocity 0: no release velocity (64)
      } else if (status == 0xA0) {
        plugin->synth.polyPressure(channel, d1, float(d2) / 127.0f);
      } else if (status == 0xB0) {
        plugin->synth.controlChange(channel, d1, d2);
      } else if (status == 0xD0) {
        plugin->synth.channelPressure(channel, float(d1) / 127.0f);
      } else if (status == 0xE0) {
        plugin->synth.pitchBend(channel, float(((d2 << 7) | d1) - 8192) / 8192.0f);
      }
      break;
    }
    default:
      break;
  }
}

// ---- audio ports --------------------------------------------------------------

uint32_t audioPortsCount(const clap_plugin_t* p, bool isInput) {
  const bool instrument = self(p)->kind == PluginKind::Instrument;
  if (isInput) return 1;                 // instrument: sidechain; FX: main input
  return instrument ? 2 : 1;             // instrument: main + Bus 2
}

bool audioPortsGet(const clap_plugin_t* p, uint32_t index, bool isInput,
                   clap_audio_port_info_t* info) {
  if (index >= audioPortsCount(p, isInput)) return false;
  const bool instrument = self(p)->kind == PluginKind::Instrument;
  const char* name = "Output";
  uint32_t flags = CLAP_AUDIO_PORT_IS_MAIN;
  clap_id id = 1;
  if (isInput) {
    id = 0;
    name = instrument ? "Sidechain" : "Input";
    flags = instrument ? 0u : uint32_t(CLAP_AUDIO_PORT_IS_MAIN);
  } else if (index == 1) {
    id = 2;
    name = "Bus 2";
    flags = 0;
  }
  info->id = id;
  std::snprintf(info->name, sizeof(info->name), "%s", name);
  info->flags = flags;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}

const clap_plugin_audio_ports_t kAudioPorts = {audioPortsCount, audioPortsGet};

// ---- note ports ----------------------------------------------------------------

uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
                  clap_note_port_info_t* info) {
  if (!isInput || index != 0) return false;
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  std::snprintf(info->name, sizeof(info->name), "%s", "Notes");
  return true;
}

const clap_plugin_note_ports_t kNotePorts = {notePortsCount, notePortsGet};

// ---- params ----------------------------------------------------------------------

uint32_t paramsCount(const clap_plugin_t*) { return uint32_t(kParamCount); }

bool paramsGetInfo(const clap_plugin_t* p, uint32_t index, clap_param_info_t* info) {
  if (index >= kParamCount) return false;
  const ParamSpec& spec = paramSpec(index);
  std::memset(info, 0, sizeof(*info));
  info->id = spec.id;
  // The Latency Mode is a setting, not a sound control: it applies at the next activate.
  const bool automatable = index != masterParamIndex(MasterParam::LatencyMode);
  info->flags = (automatable ? CLAP_PARAM_IS_AUTOMATABLE : 0u) | (spec.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
  info->cookie = nullptr;
  std::snprintf(info->name, sizeof(info->name), "%s", self(p)->synth.paramName(index));
  std::snprintf(info->module, sizeof(info->module), "%s", spec.module);
  info->min_value = double(spec.minValue);
  info->max_value = double(spec.maxValue);
  info->default_value = double(paramDefault(index, self(p)->kind));
  return true;
}

bool paramsGetValue(const clap_plugin_t* p, clap_id id, double* value) {
  if (paramIndex(id) < 0) return false;
  *value = self(p)->synth.param(id);
  return true;
}

bool paramsValueToText(const clap_plugin_t* p, clap_id id, double value, char* display,
                       uint32_t size) {
  const int index = paramIndex(id);
  if (index < 0) return false;
  self(p)->synth.formatParam(size_t(index), value, display, size);
  return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char*, double*) { return false; }

// Editor edits become setParam calls and gesture/value events for the host.
void drainUiBridge(Plugin* plugin, const clap_output_events_t* out) {
  if (UiBridge* bridge = plugin->uiBridge.load(std::memory_order_acquire)) bridge->drain(out);
}

void sendHostUpdates(Plugin* plugin, const clap_output_events_t* out, uint32_t time) {
  HostUpdate updates[kMaxHostUpdates];
  const int count = plugin->synth.takeHostUpdates(updates, int(kMaxHostUpdates));
  for (int i = 0; i < count; ++i) {
    clap_event_param_value_t ev{};
    ev.header.size = sizeof(ev);
    ev.header.time = time;
    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type = CLAP_EVENT_PARAM_VALUE;
    // Derived from the selector change, not a user edit: recording it would fight the stash.
    ev.header.flags = CLAP_EVENT_DONT_RECORD;
    ev.param_id = updates[i].id;
    ev.cookie = nullptr;
    ev.note_id = -1;
    ev.port_index = -1;
    ev.channel = -1;
    ev.key = -1;
    ev.value = double(updates[i].value);
    out->try_push(out, &ev.header);
  }
  if (count > 0) plugin->host->request_callback(plugin->host);
}

void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in,
                 const clap_output_events_t* out) {
  self(p)->synth.beginBlock();
  drainUiBridge(self(p), out);
  const uint32_t count = in->size(in);
  for (uint32_t i = 0; i < count; ++i) {
    const clap_event_header_t* hdr = in->get(in, i);
    if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID && hdr->type == CLAP_EVENT_PARAM_VALUE) {
      handleEvent(self(p), hdr);
    }
  }
  requestRestartIfModeChanged(self(p));
  sendHostUpdates(self(p), out, 0);
}

const clap_plugin_params_t kParams = {paramsCount,       paramsGetInfo, paramsGetValue,
                                      paramsValueToText, paramsTextToValue, paramsFlush};

// ---- state --------------------------------------------------------------------------

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* stream) {
  try {
    const std::string data = self(p)->store.save(self(p)->synth);
    const char* ptr = data.data();
    int64_t remaining = int64_t(data.size());
    while (remaining > 0) {
      const int64_t written = stream->write(stream, ptr, uint64_t(remaining));
      if (written <= 0) return false;
      ptr += written;
      remaining -= written;
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool stateLoad(const clap_plugin_t* p, const clap_istream_t* stream) {
  try {
    std::string data;
    char buffer[4096];
    for (;;) {
      const int64_t read = stream->read(stream, buffer, sizeof(buffer));
      if (read < 0) return false;
      if (read == 0) break;
      data.append(buffer, size_t(read));
    }
    Plugin* plugin = self(p);
    if (plugin->store.load(plugin->synth, data) == state::LoadResult::Invalid) return false;
    // Engines may have changed too: names and value text follow them.
    if (plugin->hostParams) {
      plugin->hostParams->rescan(plugin->host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT |
                                                   CLAP_PARAM_RESCAN_INFO);
    }
    // While inactive the load applies at the next activate. While active, requestedQuality()
    // changes at the next audio block, so if the host stops processing the request waits for it.
    requestRestartIfModeChanged(plugin);
    return true;
  } catch (...) {
    return false;
  }
}

const clap_plugin_state_t kState = {stateSave, stateLoad};

// ---- latency ----------------------------------------------------------------------

uint32_t latencyGet(const clap_plugin_t* p) { return uint32_t(self(p)->synth.latencySamples()); }

const clap_plugin_latency_t kLatency = {latencyGet};

// ---- plugin -------------------------------------------------------------------------

bool pluginInit(const clap_plugin_t* p) {
  Plugin* plugin = self(p);
  plugin->hostParams = static_cast<const clap_host_params_t*>(
      plugin->host->get_extension(plugin->host, CLAP_EXT_PARAMS));
  plugin->hostLatency = static_cast<const clap_host_latency_t*>(
      plugin->host->get_extension(plugin->host, CLAP_EXT_LATENCY));
  return true;
}

void pluginDestroy(const clap_plugin_t* p) { delete self(p); }

bool pluginActivate(const clap_plugin_t* p, double sampleRate, uint32_t, uint32_t maxFrames) {
  Plugin* plugin = self(p);
  plugin->synth.prepare(sampleRate, int(maxFrames));  // in the Latency Mode the parameter holds
  plugin->restartRequested.store(false, std::memory_order_release);
  const uint32_t latency = uint32_t(plugin->synth.latencySamples());
  if (latency != plugin->reportedLatency) {
    plugin->reportedLatency = latency;
    if (plugin->hostLatency) plugin->hostLatency->changed(plugin->host);
  }
  plugin->synth.setActive(true);
  plugin->active.store(true, std::memory_order_release);
  return true;
}

void pluginDeactivate(const clap_plugin_t* p) {
  self(p)->active.store(false, std::memory_order_release);
  self(p)->synth.setActive(false);
}
bool pluginStartProcessing(const clap_plugin_t*) { return true; }
void pluginStopProcessing(const clap_plugin_t*) {}
void pluginReset(const clap_plugin_t* p) { self(p)->synth.reset(); }

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* proc) {
  eg13::ScopedFlushDenormals flushDenormals;
  Plugin* plugin = self(p);
  plugin->synth.beginBlock();
  drainUiBridge(plugin, proc->out_events);
  if (proc->transport != nullptr) plugin->synth.setTransport(transportFrom(*proc->transport));
  const uint32_t frames = proc->frames_count;
  const uint32_t numEvents = proc->in_events->size(proc->in_events);

  if (proc->audio_outputs_count < 1 || proc->audio_outputs[0].channel_count < 1 ||
      proc->audio_outputs[0].data32 == nullptr) {
    for (uint32_t i = 0; i < numEvents; ++i) {
      handleEvent(plugin, proc->in_events->get(proc->in_events, i));
    }
    requestRestartIfModeChanged(plugin);
    sendHostUpdates(plugin, proc->out_events, frames > 0 ? frames - 1 : 0);
    return CLAP_PROCESS_CONTINUE;
  }

  const clap_audio_buffer_t& outBuf = proc->audio_outputs[0];
  float* out[2] = {outBuf.data32[0],
                   outBuf.channel_count > 1 ? outBuf.data32[1] : outBuf.data32[0]};

  // Bus 2 (instrument only); absent or unconnected buses fall back to main.
  float* bus2[2] = {nullptr, nullptr};
  const bool hasBus2 = plugin->kind == PluginKind::Instrument && proc->audio_outputs_count > 1 &&
                       proc->audio_outputs[1].data32 != nullptr &&
                       proc->audio_outputs[1].channel_count > 0;
  if (hasBus2) {
    const clap_audio_buffer_t& b = proc->audio_outputs[1];
    bus2[0] = b.data32[0];
    bus2[1] = b.channel_count > 1 ? b.data32[1] : b.data32[0];
  }

  // Input: the FX's main input or the instrument's sidechain (port 0 for both).
  const float* in[2] = {nullptr, nullptr};
  const bool hasInput = proc->audio_inputs_count > 0 && proc->audio_inputs[0].data32 != nullptr &&
                        proc->audio_inputs[0].channel_count > 0;
  if (hasInput) {
    const clap_audio_buffer_t& inBuf = proc->audio_inputs[0];
    in[0] = inBuf.data32[0];
    in[1] = inBuf.channel_count > 1 ? inBuf.data32[1] : inBuf.data32[0];
  }

  uint32_t eventIndex = 0;
  uint32_t frame = 0;
  while (frame < frames) {
    uint32_t next = frames;
    while (eventIndex < numEvents) {
      const clap_event_header_t* hdr = proc->in_events->get(proc->in_events, eventIndex);
      if (hdr->time > frame) {
        next = std::min(hdr->time, frames);
        break;
      }
      handleEvent(plugin, hdr);
      ++eventIndex;
    }
    const int count = int(next - frame);
    float* outSlice[2] = {out[0] + frame, out[1] + frame};
    float* bus2Slice[2] = {hasBus2 ? bus2[0] + frame : nullptr, hasBus2 ? bus2[1] + frame : nullptr};
    const float* inSlice[2] = {hasInput ? in[0] + frame : nullptr, hasInput ? in[1] + frame : nullptr};
    plugin->synth.process(hasInput ? inSlice : nullptr, outSlice, hasBus2 ? bus2Slice : nullptr,
                          count);
    frame = next;
  }
  while (eventIndex < numEvents) {
    handleEvent(plugin, proc->in_events->get(proc->in_events, eventIndex++));
  }
  requestRestartIfModeChanged(plugin);
  sendHostUpdates(plugin, proc->out_events, frames > 0 ? frames - 1 : 0);
  return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension(const clap_plugin_t*, const char* id) {
  if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPorts;
  if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &kNotePorts;
  if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kParams;
  if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &kState;
  if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &kLatency;
  return nullptr;
}

void pluginOnMainThread(const clap_plugin_t* p) {
  Plugin* plugin = self(p);
  if (plugin->synth.takeEngineChanged() && plugin->hostParams) {
    // VALUES too: recovers any swapped value whose try_push failed.
    plugin->hostParams->rescan(plugin->host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT |
                                                 CLAP_PARAM_RESCAN_INFO);
  }
}

}  // namespace

const clap_plugin_t* createPlugin(const clap_host_t* host, PluginKind kind,
                                  const clap_plugin_descriptor_t* descriptor) {
  Plugin* plugin = nullptr;
  try {
    plugin = new Plugin(host, kind);
  } catch (...) {
    return nullptr;
  }
  plugin->clap.desc = descriptor;
  plugin->clap.plugin_data = plugin;
  plugin->clap.init = pluginInit;
  plugin->clap.destroy = pluginDestroy;
  plugin->clap.activate = pluginActivate;
  plugin->clap.deactivate = pluginDeactivate;
  plugin->clap.start_processing = pluginStartProcessing;
  plugin->clap.stop_processing = pluginStopProcessing;
  plugin->clap.reset = pluginReset;
  plugin->clap.process = pluginProcess;
  plugin->clap.get_extension = pluginGetExtension;
  plugin->clap.on_main_thread = pluginOnMainThread;
  return &plugin->clap;
}

}  // namespace eg13
