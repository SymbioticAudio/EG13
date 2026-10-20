#include "eg13_plugin.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "state/state.h"
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
  Synth synth;
};

Plugin* self(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

// ---- events -----------------------------------------------------------------

void handleEvent(Plugin* plugin, const clap_event_header_t* hdr) {
  if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
  switch (hdr->type) {
    case CLAP_EVENT_NOTE_ON: {
      const auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
      if (ev->key >= 0 && ev->key <= 127) plugin->synth.noteOn(ev->key, float(ev->velocity));
      break;
    }
    case CLAP_EVENT_NOTE_OFF:
    case CLAP_EVENT_NOTE_CHOKE: {
      const auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
      if (ev->key < 0) {
        plugin->synth.allNotesOff();
      } else {
        plugin->synth.noteOff(ev->key);
      }
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
      const int key = ev->data[1] & 0x7F;
      const int velocity = ev->data[2] & 0x7F;
      if (status == 0x90 && velocity > 0) {
        plugin->synth.noteOn(key, float(velocity) / 127.0f);
      } else if (status == 0x80 || status == 0x90) {
        plugin->synth.noteOff(key);
      } else if (status == 0xB0 && (key == 120 || key == 123)) {
        plugin->synth.allNotesOff();
      }
      break;
    }
    default:
      break;
  }
}

// ---- audio ports --------------------------------------------------------------

uint32_t audioPortsCount(const clap_plugin_t* p, bool isInput) {
  if (isInput) return self(p)->kind == PluginKind::Effect ? 1 : 0;
  return 1;
}

bool audioPortsGet(const clap_plugin_t* p, uint32_t index, bool isInput,
                   clap_audio_port_info_t* info) {
  if (index >= audioPortsCount(p, isInput)) return false;
  info->id = isInput ? 0 : 1;
  std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Input" : "Output");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
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
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
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
  info->flags = CLAP_PARAM_IS_AUTOMATABLE | (spec.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
  info->cookie = nullptr;
  std::snprintf(info->name, sizeof(info->name), "%s", spec.name);
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

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display,
                       uint32_t size) {
  const int index = paramIndex(id);
  if (index < 0) return false;
  formatParam(size_t(index), value, display, size);
  return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char*, double*) { return false; }

void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in,
                 const clap_output_events_t*) {
  const uint32_t count = in->size(in);
  for (uint32_t i = 0; i < count; ++i) {
    const clap_event_header_t* hdr = in->get(in, i);
    if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID && hdr->type == CLAP_EVENT_PARAM_VALUE) {
      handleEvent(self(p), hdr);
    }
  }
}

const clap_plugin_params_t kParams = {paramsCount,       paramsGetInfo, paramsGetValue,
                                      paramsValueToText, paramsTextToValue, paramsFlush};

// ---- state --------------------------------------------------------------------------

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* stream) {
  try {
  const std::string data = state::save(self(p)->synth);
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
  if (state::load(plugin->synth, data) == state::LoadResult::Invalid) return false;
  if (plugin->hostParams) plugin->hostParams->rescan(plugin->host, CLAP_PARAM_RESCAN_VALUES);
  return true;
  } catch (...) {
    return false;
  }
}

const clap_plugin_state_t kState = {stateSave, stateLoad};

// ---- plugin -------------------------------------------------------------------------

bool pluginInit(const clap_plugin_t* p) {
  Plugin* plugin = self(p);
  plugin->hostParams = static_cast<const clap_host_params_t*>(
      plugin->host->get_extension(plugin->host, CLAP_EXT_PARAMS));
  return true;
}

void pluginDestroy(const clap_plugin_t* p) { delete self(p); }

bool pluginActivate(const clap_plugin_t* p, double sampleRate, uint32_t, uint32_t maxFrames) {
  self(p)->synth.prepare(sampleRate, int(maxFrames));
  return true;
}

void pluginDeactivate(const clap_plugin_t*) {}
bool pluginStartProcessing(const clap_plugin_t*) { return true; }
void pluginStopProcessing(const clap_plugin_t*) {}
void pluginReset(const clap_plugin_t* p) { self(p)->synth.reset(); }

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* proc) {
  eg13::ScopedFlushDenormals flushDenormals;
  Plugin* plugin = self(p);
  const uint32_t frames = proc->frames_count;
  const uint32_t numEvents = proc->in_events->size(proc->in_events);

  if (proc->audio_outputs_count < 1 || proc->audio_outputs[0].channel_count < 1 ||
      proc->audio_outputs[0].data32 == nullptr) {
    for (uint32_t i = 0; i < numEvents; ++i) {
      handleEvent(plugin, proc->in_events->get(proc->in_events, i));
    }
    return CLAP_PROCESS_CONTINUE;
  }

  const clap_audio_buffer_t& outBuf = proc->audio_outputs[0];
  float* out[2] = {outBuf.data32[0],
                   outBuf.channel_count > 1 ? outBuf.data32[1] : outBuf.data32[0]};

  const float* in[2] = {nullptr, nullptr};
  const bool hasInput = plugin->kind == PluginKind::Effect && proc->audio_inputs_count > 0 &&
                        proc->audio_inputs[0].data32 != nullptr &&
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
    float* outSlice[2] = {out[0] + frame, out[1] + frame};
    if (hasInput) {
      const float* inSlice[2] = {in[0] + frame, in[1] + frame};
      plugin->synth.process(inSlice, outSlice, int(next - frame));
    } else {
      plugin->synth.process(nullptr, outSlice, int(next - frame));
    }
    frame = next;
  }
  while (eventIndex < numEvents) {
    handleEvent(plugin, proc->in_events->get(proc->in_events, eventIndex++));
  }
  return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension(const clap_plugin_t*, const char* id) {
  if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPorts;
  if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &kNotePorts;
  if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kParams;
  if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &kState;
  return nullptr;
}

void pluginOnMainThread(const clap_plugin_t*) {}

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
