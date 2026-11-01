#include "update.h"

#include "../core/json.h"
#include "../platform/platform.h"
#include "http.h"

#include <cstdlib>

namespace update {

namespace {

std::string g_available;
bool g_started = false;

// Parses "1.6.1" / "v1.6.1" into a comparable number (major*1e6+minor*1e3+patch).
long parse_version(const std::string &v) {
	const char *s = v.c_str();
	if (*s == 'v' || *s == 'V') s++;
	long major = 0, minor = 0, patch = 0;
	major = strtol(s, (char **)&s, 10);
	if (*s == '.') minor = strtol(s + 1, (char **)&s, 10);
	if (*s == '.') patch = strtol(s + 1, (char **)&s, 10);
	return major * 1000000 + minor * 1000 + patch;
}

} // namespace

void start() {
	if (g_started) return;
	g_started = true;
	http::Request req;
	req.url = std::string("https://api.github.com/repos/") + REPO_OWNER + "/" + REPO_NAME + "/tags?per_page=20";
	req.headers = { "Accept: application/vnd.github+json" };
	http::send(req, [](http::Response &res) {
		if (!res.ok()) {
			platform::log("update: check failed: %s (HTTP %d)", res.error.c_str(), res.status);
			return;
		}
		Json tags = Json::parse(res.body);
		const long current = parse_version(APP_VERSION);
		long best = current;
		std::string best_tag;
		for (size_t i = 0; i < tags.size(); i++) {
			const std::string name = tags[i]["name"].as_string("");
			if (name.empty()) continue;
			const long v = parse_version(name);
			if (v > best) {
				best = v;
				best_tag = name;
			}
		}
		if (!best_tag.empty()) {
			g_available = best_tag;
			platform::log("update: newer version available: %s (this build %s)", best_tag.c_str(), APP_VERSION);
		} else {
			platform::log("update: up to date (%s)", APP_VERSION);
		}
	});
}

std::string available() {
	return g_available;
}

} // namespace update
