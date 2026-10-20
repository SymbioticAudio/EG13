// entry_impl.cpp
#include "entry_impl.h"

#include <clap/clap.h>
#include <clapwrapper/auv2.h>

#include <cstring>

#include "descriptors.h"
#include "eg13_plugin.h"

namespace {

uint32_t factoryCount(const clap_plugin_factory_t*) { return 2; }

const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*,
                                                  uint32_t index) {
  switch (index) {
    case 0: return &eg13::kInstrumentDescriptor;
    case 1: return &eg13::kEffectDescriptor;
    default: return nullptr;
  }
}

const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host,
                                   const char* pluginId) {
  if (!clap_version_is_compatible(host->clap_version)) return nullptr;
  if (std::strcmp(pluginId, eg13::kInstrumentDescriptor.id) == 0) {
    return eg13::createPlugin(host, eg13::PluginKind::Instrument, &eg13::kInstrumentDescriptor);
  }
  if (std::strcmp(pluginId, eg13::kEffectDescriptor.id) == 0) {
    return eg13::createPlugin(host, eg13::PluginKind::Effect, &eg13::kEffectDescriptor);
  }
  return nullptr;
}

const clap_plugin_factory_t kFactory = {factoryCount, factoryDescriptor, factoryCreate};

// AU identities. These are permanent once released.
bool auv2Info(const clap_plugin_factory_as_auv2_t*, uint32_t index,
              clap_plugin_info_as_auv2_t* info) {
  switch (index) {
    case 0:
      std::strncpy(info->au_type, "aumu", 5);
      std::strncpy(info->au_subt, "EG13", 5);
      return true;
    case 1:
      std::strncpy(info->au_type, "aumf", 5);
      std::strncpy(info->au_subt, "EGfx", 5);
      return true;
    default:
      return false;
  }
}

const clap_plugin_factory_as_auv2_t kAuv2Factory = {"Symb", "SymbioticAudio", auv2Info};

}  // namespace

bool eg13_entry_init(const char*) { return true; }

void eg13_entry_deinit() {}

const void* eg13_entry_get_factory(const char* factoryId) {
  if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) return &kFactory;
  if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_INFO_AUV2) == 0) return &kAuv2Factory;
  return nullptr;
}
