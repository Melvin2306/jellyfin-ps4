// App settings in /data/jellyfin/settings.json: global values plus per-user
// values (keyed by the Jellyfin user id), e.g. preferred audio and subtitle
// languages. Values are read lazily and written on every change.
#pragma once

#include "json.h"

#include <string>

namespace settings {

// Global settings.
std::string get_string(const std::string &key, const std::string &fallback = "");
int64_t get_int(const std::string &key, int64_t fallback = 0);
bool get_bool(const std::string &key, bool fallback = false);
void set(const std::string &key, const Json &value);

// Settings of one user (falls back to the global value, then `fallback`).
std::string user_string(const std::string &user_id, const std::string &key, const std::string &fallback = "");
void set_user(const std::string &user_id, const std::string &key, const Json &value);

} // namespace settings
