#include <clap/clap.h>

#include "entry_impl.h"

extern "C" {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif
const CLAP_EXPORT struct clap_plugin_entry clap_entry = {
    CLAP_VERSION, eg13_entry_init, eg13_entry_deinit, eg13_entry_get_factory};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
