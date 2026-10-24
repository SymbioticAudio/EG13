/* Functions the EG13UI Swift package exports with @_cdecl. Main thread only. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "eg13/core_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* kind: 0 = EG13 (instrument), 1 = EG13 FX. api must outlive the editor. Returns a handle. */
void* eg13_ui_create(const EG13CoreAPI* api, int32_t kind, double zoom);
/* Ends open gestures, releases held notes, stops the display timer, detaches and frees. */
void eg13_ui_destroy(void* handle);
/* Adds the editor to parent (an NSView*) and makes it visible. */
bool eg13_ui_attach(void* handle, void* parent);
/* Logical points; the editor scales its 1200 x 760 layout to fit. */
void eg13_ui_set_size(void* handle, uint32_t width, uint32_t height);
void eg13_ui_set_visible(void* handle, bool visible);

/* Editor objects (controllers and views) still alive; 0 once every editor is destroyed.
 * For tests: they prove create/destroy leaks nothing, a running display timer included. */
int32_t eg13_ui_live_count(void);

/* Proves the Swift runtime, SwiftUI and the package dependencies are linked and usable:
 * builds a hosting view that uses each package module and returns 1. Used by tests only. */
int32_t eg13_ui_link_check(void);

#ifdef __cplusplus
}
#endif
