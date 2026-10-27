# Known issues (internal)

Small findings from the M6b reviews that are not fixed. None changes the sound or loses data.
Items that users can see are in [manual/limitations.md](manual/limitations.md).

## UI bridge and modulation publishing
- **No two-thread or TSan test for UiBridge.** The queue and backlog logic is covered by single-thread tests and by the argument in the comments of `plugin/src/ui_bridge.cpp`. A TSan run of a producer and a drain thread would need a TSan preset for the plugin target; there is none yet.
- **Unused P's publish finite values.** The modulation values of the P's an engine does not use are published as finite numbers; the editor never shows them (`destinationHidden`), so nothing is visible. Publishing NaN for them (as the `UiSnapshot` comment promises for an Off slot) is a change inside `Part::refreshModValues` that needs its own test.

## Routes
- **No test for an open destination menu during an engine change.** The menu rebuilds from the model on the next tick; this is by construction, not tested.

## Generated UI tables
- **Value strings go through libm on x86_64.** `tools/ui_tables` formats values with `snprintf` on doubles that came from libm calls, so the last digit of a rarely used string could differ between arm64 and x86_64. The check `ui_tables_up_to_date` runs in CI on macOS (arm64) and on Linux x86_64, so a difference would show as a failing diff there.
