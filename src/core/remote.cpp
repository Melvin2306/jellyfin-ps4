#include "remote.h"

#include "../net/websocket.h"
#include "../platform/platform.h"
#include "../platform/threads.h"
#include "jellyfin.h"
#include "json.h"

#include <unistd.h>

#include <atomic>
#include <deque>

namespace remote {

namespace {

threads::Mutex g_mutex;
std::deque<Command> g_playstate;
std::deque<Command> g_play;
std::string g_message;
std::atomic<bool> g_running{ false };
std::atomic<bool> g_stop{ false };

void push_playstate(const Command &c) {
	threads::Lock lock(g_mutex);
	g_playstate.push_back(c);
}
void push_play(const Command &c) {
	threads::Lock lock(g_mutex);
	g_play.push_back(c);
}

// Handles one Jellyfin WebSocket message.
void handle(const std::string &text, ws::Socket &sock) {
	Json j = Json::parse(text);
	const std::string type = j["MessageType"].as_string("");
	if (type == "ForceKeepAlive" || type == "KeepAlive") {
		Json out = Json::object();
		out.set("MessageType", "KeepAlive");
		sock.send_text(out.dump());
		return;
	}
	if (type == "Playstate") {
		const Json &d = j["Data"];
		const std::string cmd = d["Command"].as_string("");
		Command c;
		if (cmd == "Stop") c.type = Command::STOP;
		else if (cmd == "Pause") c.type = Command::PAUSE;
		else if (cmd == "Unpause") c.type = Command::UNPAUSE;
		else if (cmd == "PlayPause") c.type = Command::PLAYPAUSE;
		else if (cmd == "NextTrack") c.type = Command::NEXT;
		else if (cmd == "PreviousTrack") c.type = Command::PREV;
		else if (cmd == "Seek") {
			c.type = Command::SEEK;
			c.seek_seconds = d["SeekPositionTicks"].as_int(0) / 1e7;
		} else {
			return;
		}
		platform::log("remote: playstate %s", cmd.c_str());
		push_playstate(c);
	} else if (type == "Play") {
		const Json &d = j["Data"];
		const Json &ids = d["ItemIds"];
		if (ids.is_array() && ids.size() > 0) {
			Command c;
			c.type = Command::PLAY;
			c.item_id = ids[0].as_string("");
			c.start_seconds = d["StartPositionTicks"].as_int(0) / 1e7;
			platform::log("remote: play %s", c.item_id.c_str());
			if (!c.item_id.empty()) push_play(c);
		}
	} else if (type == "GeneralCommand") {
		const Json &d = j["Data"];
		if (d["Name"].as_string("") == "DisplayMessage") {
			const std::string header = d["Arguments"]["Header"].as_string("");
			const std::string body = d["Arguments"]["Text"].as_string("");
			threads::Lock lock(g_mutex);
			g_message = header.empty() ? body : header + ": " + body;
		}
	}
}

void *loop(void *) {
	while (!g_stop) {
		std::string host, path;
		int port = 0;
		if (!jf::client().websocket_target(host, port, path)) {
			for (int i = 0; i < 20 && !g_stop; i++) usleep(100 * 1000);
			continue;
		}
		jf::client().register_capabilities();
		ws::Socket sock;
		std::string error;
		if (!sock.connect(host, port, path, error)) {
			platform::log("remote: connect failed: %s", error.c_str());
			// Back off before retrying.
			for (int i = 0; i < 50 && !g_stop; i++) usleep(100 * 1000);
			continue;
		}
		double last_keepalive = platform::now();
		while (!g_stop) {
			std::string msg;
			const int r = sock.recv_text(msg);
			if (r < 0) {
				platform::log("remote: connection closed");
				break;
			}
			if (r == 1) handle(msg, sock);
			// Periodic keep-alive.
			if (platform::now() - last_keepalive > 30) {
				Json out = Json::object();
				out.set("MessageType", "KeepAlive");
				if (!sock.send_text(out.dump())) break;
				last_keepalive = platform::now();
			}
		}
		sock.close();
	}
	g_running = false;
	return nullptr;
}

} // namespace

void start() {
	if (g_running.exchange(true)) return;
	g_stop = false;
	if (!threads::start(loop, nullptr, 256 * 1024, "jf_remote")) {
		g_running = false;
		platform::log("remote: couldn't start thread");
	}
}

void stop() {
	g_stop = true;
}

bool poll_playstate(Command &out) {
	threads::Lock lock(g_mutex);
	if (g_playstate.empty()) return false;
	out = g_playstate.front();
	g_playstate.pop_front();
	return true;
}

bool poll_play(Command &out) {
	threads::Lock lock(g_mutex);
	if (g_play.empty()) return false;
	out = g_play.front();
	g_play.pop_front();
	return true;
}

std::string take_message() {
	threads::Lock lock(g_mutex);
	std::string m = g_message;
	g_message.clear();
	return m;
}

} // namespace remote
