// In-app update check against this project's GitHub tags. Source-only: it
// never downloads or installs anything, it only reports that a newer tagged
// version exists so the user can update themselves.
#pragma once

#include <string>

namespace update {

// Repo this build belongs to (owner/name).
inline const char *REPO_OWNER = "Melvin2306";
inline const char *REPO_NAME = "jellyfin-ps4";
inline const char *REPO_URL = "https://github.com/Melvin2306/jellyfin-ps4";

// Starts the check once (GitHub tags API over HTTPS). Safe to call anytime.
void start();
// The newest version tag seen if it is newer than this build, else "".
std::string available();

} // namespace update
