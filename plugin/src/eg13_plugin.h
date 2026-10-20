#pragma once

#include <clap/clap.h>

#include "synth/synth.h"

namespace eg13 {

const clap_plugin_t* createPlugin(const clap_host_t* host, PluginKind kind,
                                  const clap_plugin_descriptor_t* descriptor);

}  // namespace eg13
