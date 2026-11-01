// Jellyfin server auto-discovery: a UDP broadcast of "who is JellyfinServer?"
// to port 7359; servers answer with JSON {Address, Id, Name}. Runs on its
// own thread for a few seconds. Servers in Docker bridge networking usually
// don't receive the broadcast, so manual entry stays the fallback.
#pragma once

#include <string>
#include <vector>

namespace discovery {

struct Server {
	std::string id, name, address;
};

// Starts a search (ignored while one is running).
void start();
bool running();
// Servers found so far (main thread; copies).
std::vector<Server> results();
// A short description of the last search ("", "no servers answered"...).
std::string status();

} // namespace discovery
