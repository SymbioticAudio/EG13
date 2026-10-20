/* Functions the EG13UI Swift package exports with @_cdecl. Main thread only. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "eg13/core_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Proves the Swift runtime, SwiftUI and every package dependency are linked and usable:
 * builds a hosting view that uses each package module and returns 1. */
int32_t eg13_ui_link_check(void);

/* kind: 0 = EG13 (instrument), 1 = EG13 FX. api must outlive the editor. Returns a handle. */
void* eg13_ui_create(const EG13CoreAPI* api, int32_t kind, double zoom);
/* Ends open gestures, releases held notes, stops the display timer, detaches and frees. */
void eg13_ui_destroy(void* handle);
/* Adds the editor to parent (an NSView*) and makes it visible. */
bool eg13_ui_attach(void* handle, void* parent);
/* Logical points; the editor scales its 1200 x 760 layout to fit. */
void eg13_ui_set_size(void* handle, uint32_t width, uint32_t height);
void eg13_ui_set_visible(void* handle, bool visible);

#ifdef __cplusplus
}
#endif
