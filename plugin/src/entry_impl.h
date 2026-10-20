// entry_impl.h
#pragma once

bool eg13_entry_init(const char* pluginPath);
void eg13_entry_deinit();
const void* eg13_entry_get_factory(const char* factoryId);
