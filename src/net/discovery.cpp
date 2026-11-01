#include "discovery.h"

#include "../core/json.h"
#include "../platform/platform.h"
#include "../platform/threads.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>

namespace discovery {

namespace {

const int PORT = 7359;
const double LISTEN_SECONDS = 3.0;
const char *PROBE = "who is JellyfinServer?";

threads::Mutex g_mutex;
std::vector<Server> g_results;
std::string g_status;
std::atomic<bool> g_running{ false };

void set_status(const std::string &s) {
	threads::Lock lock(g_mutex);
	g_status = s;
}

void *search(void *) {
	const int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		platform::log("discovery: socket failed");
		set_status("Couldn't search the network");
		g_running = false;
		return nullptr;
	}
	const int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
#if defined(STREMIO_PS4)
	setsockopt(fd, SOL_SOCKET, 0x1200 /* SCE_NET_SO_NBIO */, &one, sizeof(one));
#endif
	sockaddr_in to;
	memset(&to, 0, sizeof(to));
#if defined(STREMIO_PS4)
	to.sin_len = sizeof(to);
#endif
	to.sin_family = AF_INET;
	to.sin_port = htons(PORT);
	to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	int sent = 0;
	for (int i = 0; i < 2; i++) { // twice: UDP may drop one
		if (sendto(fd, PROBE, strlen(PROBE), 0, (const sockaddr *)&to, sizeof(to)) > 0) sent++;
	}
	platform::log("discovery: probe sent %d time(s) to 255.255.255.255:%d", sent, PORT);
	if (sent == 0) {
		close(fd);
		set_status("Couldn't send the discovery broadcast");
		g_running = false;
		return nullptr;
	}
	const double until = platform::now() + LISTEN_SECONDS;
	char buf[2048];
	int answers = 0;
	while (platform::now() < until) {
		sockaddr_in from;
		socklen_t len = sizeof(from);
		const long n = (long)recvfrom(fd, buf, sizeof(buf) - 1, 0, (sockaddr *)&from, &len);
		if (n <= 0) {
			usleep(50 * 1000);
			continue;
		}
		buf[n] = 0;
		answers++;
		const Json j = Json::parse(std::string(buf, (size_t)n));
		Server s;
		s.id = j["Id"].as_string("");
		s.name = j["Name"].as_string("Jellyfin");
		s.address = j["Address"].as_string("");
		const uint8_t *ip = (const uint8_t *)&from.sin_addr.s_addr;
		platform::log("discovery: answer from %u.%u.%u.%u (%ld bytes)", ip[0], ip[1], ip[2], ip[3], n);
		if (s.address.empty()) continue;
		threads::Lock lock(g_mutex);
		bool dup = false;
		for (const Server &e : g_results)
			if (e.id == s.id && e.address == s.address) dup = true;
		if (!dup) g_results.push_back(s);
	}
	close(fd);
	{
		threads::Lock lock(g_mutex);
		g_status = g_results.empty() ? "No servers answered. Enter the address manually." : "";
		platform::log("discovery: done, %d answer(s), %d server(s)", answers, (int)g_results.size());
	}
	g_running = false;
	return nullptr;
}

} // namespace

void start() {
	if (g_running.exchange(true)) return;
	{
		threads::Lock lock(g_mutex);
		g_results.clear();
		g_status = "Searching the network\xE2\x80\xA6";
	}
	if (!threads::start(search, nullptr, 256 * 1024, "jf_discovery")) {
		g_running = false;
		set_status("Couldn't search the network");
	}
}

bool running() {
	return g_running;
}

std::vector<Server> results() {
	threads::Lock lock(g_mutex);
	return g_results;
}

std::string status() {
	threads::Lock lock(g_mutex);
	return g_status;
}

} // namespace discovery
