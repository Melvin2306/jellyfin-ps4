#include "settings.h"

#include "../platform/platform.h"

namespace settings {

namespace {

const char *FILE_NAME = "settings.json";
Json g_root;
bool g_loaded = false;

Json &root() {
	if (!g_loaded) {
		g_loaded = true;
		std::string text;
		if (platform::read_file(platform::data_dir() + FILE_NAME, text)) g_root = Json::parse(text);
		if (!g_root.is_object()) g_root = Json::object();
	}
	return g_root;
}

void save() {
	if (!platform::write_file(platform::data_dir() + FILE_NAME, root().dump())) platform::log("settings: couldn't save");
}

} // namespace

std::string get_string(const std::string &key, const std::string &fallback) {
	return root()[key].as_string(fallback);
}

int64_t get_int(const std::string &key, int64_t fallback) {
	return root()[key].as_int(fallback);
}

bool get_bool(const std::string &key, bool fallback) {
	return root()[key].as_bool(fallback);
}

void set(const std::string &key, const Json &value) {
	root().set(key, value);
	save();
}

std::string user_string(const std::string &user_id, const std::string &key, const std::string &fallback) {
	const Json &u = root()["users"][user_id];
	if (u.has(key)) return u[key].as_string(fallback);
	return root()[key].as_string(fallback);
}

void set_user(const std::string &user_id, const std::string &key, const Json &value) {
	Json users = root()["users"];
	if (!users.is_object()) users = Json::object();
	Json u = users[user_id];
	if (!u.is_object()) u = Json::object();
	u.set(key, value);
	users.set(user_id, u);
	root().set("users", users);
	save();
}

} // namespace settings
