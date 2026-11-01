#include "jellyfin.h"

#include "../net/http.h"
#include "../platform/platform.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace jf {

namespace {

const char *CLIENT_NAME = "Jellyfin PS4";
const char *DEVICE_NAME = "PlayStation 4";
const char *SESSION_FILE = "session.json";
// Direct play / transcode limits. The Pi can't transcode much, so direct
// play is always preferred; the transcode fallback asks for 1080p H.264.
const int TRANSCODE_VIDEO_BITRATE = 8000000;
const int TRANSCODE_AUDIO_BITRATE = 192000;

std::string strip_slash(std::string s) {
	while (!s.empty() && (s.back() == '/' || s.back() == ' ')) s.pop_back();
	while (!s.empty() && s.front() == ' ') s.erase(0, 1);
	return s;
}

std::string random_hex(int n) {
	static const char *hex = "0123456789abcdef";
	std::string out;
	uint64_t seed = (uint64_t)(platform::now() * 1e6) ^ ((uint64_t)platform::unix_time() << 20);
	for (int i = 0; i < n; i++) {
		seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
		out += hex[(seed >> 59) & 15];
	}
	return out;
}

std::string quote_safe(const std::string &s) {
	std::string out;
	for (char c : s)
		if (c != '"' && c != '\\' && c != '\r' && c != '\n') out += c;
	return out;
}

std::string describe_failure(const http::Response &res) {
	if (!res.error.empty()) return res.error;
	if (res.status == 401) return "Not authorised (HTTP 401)";
	char buf[64];
	snprintf(buf, sizeof(buf), "Server error (HTTP %d)", res.status);
	return buf;
}

} // namespace

std::string url_encode(const std::string &s) {
	static const char *hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : s) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += (char)c;
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

Item Item::from_json(const Json &j) {
	Item it;
	it.id = j["Id"].as_string("");
	it.name = j["Name"].as_string("");
	it.type = j["Type"].as_string("");
	it.overview = j["Overview"].as_string("");
	it.series_name = j["SeriesName"].as_string("");
	it.series_id = j["SeriesId"].as_string("");
	it.container = j["Container"].as_string("");
	it.year = (int)j["ProductionYear"].as_int(0);
	if (j.has("ParentIndexNumber")) it.season = (int)j["ParentIndexNumber"].as_int(-1);
	if (j.has("IndexNumber")) it.episode = (int)j["IndexNumber"].as_int(-1);
	it.runtime_ticks = j["RunTimeTicks"].as_int(0);
	const Json &ud = j["UserData"];
	it.position_ticks = ud["PlaybackPositionTicks"].as_int(0);
	it.played = ud["Played"].as_bool(false);
	it.image_tag = j["ImageTags"]["Primary"].as_string("");
	const Json &sources = j["MediaSources"];
	if (sources.is_array() && sources.size() > 0) {
		const Json &ms = sources[0];
		it.media_source_id = ms["Id"].as_string("");
		if (it.container.empty()) it.container = ms["Container"].as_string("");
		it.bitrate = ms["Bitrate"].as_int(0);
		const Json &streams = ms["MediaStreams"];
		for (size_t i = 0; i < streams.size(); i++) {
			const Json &s = streams[i];
			const std::string t = s["Type"].as_string("");
			if (t == "Audio" || t == "Subtitle") {
				Stream st;
				st.index = (int)s["Index"].as_int(-1);
				st.type = t;
				st.codec = s["Codec"].as_string("");
				st.language = s["Language"].as_string("");
				st.title = s["DisplayTitle"].as_string(s["Title"].as_string(st.codec));
				st.is_default = s["IsDefault"].as_bool(false);
				st.is_forced = s["IsForced"].as_bool(false);
				st.is_external = s["IsExternal"].as_bool(false);
				st.is_text = s["IsTextSubtitleStream"].as_bool(false);
				st.channels = (int)s["Channels"].as_int(0);
				it.streams.push_back(st);
			}
			if (t == "Video" && it.video_codec.empty()) {
				it.video_codec = s["Codec"].as_string("");
				it.width = (int)s["Width"].as_int(0);
				it.height = (int)s["Height"].as_int(0);
			} else if (t == "Audio" && (it.audio_codec.empty() || s["IsDefault"].as_bool(false))) {
				it.audio_codec = s["Codec"].as_string("");
				it.audio_channels = (int)s["Channels"].as_int(0);
			}
		}
	}
	if (it.media_source_id.empty()) it.media_source_id = it.id;
	return it;
}

std::vector<Stream> Item::audio_streams() const {
	std::vector<Stream> out;
	for (const Stream &s : streams)
		if (s.type == "Audio") out.push_back(s);
	return out;
}

std::vector<Stream> Item::subtitle_streams() const {
	std::vector<Stream> out;
	for (const Stream &s : streams)
		if (s.type == "Subtitle") out.push_back(s);
	return out;
}

const Stream *Item::stream(int index) const {
	for (const Stream &s : streams)
		if (s.index == index) return &s;
	return nullptr;
}

std::string Item::display_name() const {
	if (type == "Episode" && season >= 0 && episode >= 0) {
		char buf[32];
		snprintf(buf, sizeof(buf), "S%d E%d \xC2\xB7 ", season, episode);
		return buf + name;
	}
	return name;
}

std::string Item::info_line() const {
	std::vector<std::string> parts;
	if (type == "Episode" && !series_name.empty()) parts.push_back(series_name);
	if (year > 0) parts.push_back(std::to_string(year));
	if (runtime_ticks > 0) {
		const int minutes = (int)(runtime_seconds() / 60 + 0.5);
		char buf[32];
		if (minutes >= 60) snprintf(buf, sizeof(buf), "%d h %d min", minutes / 60, minutes % 60);
		else snprintf(buf, sizeof(buf), "%d min", minutes);
		parts.push_back(buf);
	}
	if (!video_codec.empty()) {
		std::string v = video_codec;
		for (char &c : v) c = (char)toupper((unsigned char)c);
		if (width > 0) v += " " + std::to_string(width) + "x" + std::to_string(height);
		parts.push_back(v);
	}
	if (!audio_codec.empty()) {
		std::string a = audio_codec;
		for (char &c : a) c = (char)toupper((unsigned char)c);
		if (audio_channels == 6) a += " 5.1";
		else if (audio_channels == 8) a += " 7.1";
		else if (audio_channels == 2) a += " stereo";
		parts.push_back(a);
	}
	if (!container.empty()) parts.push_back(container);
	std::string out;
	for (size_t i = 0; i < parts.size(); i++) out += (i ? " \xC2\xB7 " : "") + parts[i];
	return out;
}

Client &client() {
	static Client c;
	return c;
}

void Client::init() {
	std::string text;
	if (platform::read_file(platform::data_dir() + SESSION_FILE, text)) {
		Json j = Json::parse(text);
		server_ = j["server"].as_string("");
		user_id_ = j["user_id"].as_string("");
		user_name_ = j["user_name"].as_string("");
		token_ = j["token"].as_string("");
		device_id_ = j["device_id"].as_string("");
	}
	if (device_id_.empty()) {
		device_id_ = "ps4-" + random_hex(16);
		save();
	}
	platform::log("jellyfin: session %s, server %s, user %s", token_.empty() ? "none" : "loaded", server_.c_str(),
			user_name_.c_str());
}

void Client::save() {
	Json j = Json::object();
	j.set("server", server_);
	j.set("user_id", user_id_);
	j.set("user_name", user_name_);
	j.set("token", token_);
	j.set("device_id", device_id_);
	if (!platform::write_file(platform::data_dir() + SESSION_FILE, j.dump())) platform::log("jellyfin: couldn't save session");
}

std::string Client::auth_value() const {
	std::string v = std::string("MediaBrowser Client=\"") + CLIENT_NAME + "\", Device=\"" + DEVICE_NAME + "\", DeviceId=\"" +
			quote_safe(device_id_) + "\", Version=\"" + APP_VERSION + "\"";
	if (!token_.empty()) v += ", Token=\"" + quote_safe(token_) + "\"";
	return v;
}

std::vector<std::string> Client::auth_headers() const {
	return { "Authorization: " + auth_value() };
}

std::string Client::url(const std::string &path) const {
	return server_ + path;
}

std::string Client::take_notice() {
	std::string n = notice_;
	notice_.clear();
	return n;
}

void Client::unauthorized() {
	if (token_.empty()) return;
	platform::log("jellyfin: saved session rejected (HTTP 401); back to sign-in");
	token_.clear();
	user_id_.clear();
	save(); // server URL and username stay remembered
	notice_ = "Your session has expired or was signed out on the server. Please sign in again.";
	auth_rev++;
}

void Client::check_server(const std::string &url_in, ServerCallback done) {
	std::string server = strip_slash(url_in);
	if (server.empty()) {
		done(false, "Enter your server's address, e.g. http://192.168.1.10:8096", ServerInfo());
		return;
	}
	if (server.find("://") == std::string::npos) server = "http://" + server;
	platform::log("jellyfin: checking %s", server.c_str());
	http::Request check;
	check.url = server + "/System/Info/Public";
	http::send(check, [this, server, done](http::Response &res) {
		if (!res.ok()) {
			platform::log("jellyfin: server check failed: %s", describe_failure(res).c_str());
			done(false, "Can't reach a Jellyfin server there: " + describe_failure(res), ServerInfo());
			return;
		}
		Json j = Json::parse(res.body);
		ServerInfo info;
		info.url = server;
		info.name = j["ServerName"].as_string("");
		info.version = j["Version"].as_string("");
		if (info.version.empty() && info.name.empty()) {
			done(false, "That address answered, but it isn't a Jellyfin server.", ServerInfo());
			return;
		}
		platform::log("jellyfin: server version %s", info.version.c_str());
		if (server != server_) {
			server_ = server;
			save();
		}
		http::Request qc;
		qc.url = server + "/QuickConnect/Enabled";
		http::send(qc, [info, done](http::Response &res2) mutable {
			info.quick_connect = res2.ok() && res2.body.find("true") != std::string::npos;
			platform::log("jellyfin: Quick Connect %s", info.quick_connect ? "enabled" : "disabled");
			done(true, "", info);
		});
	});
}

void Client::finish_auth(const Json &j, const std::string &fallback_user) {
	token_ = j["AccessToken"].as_string("");
	user_id_ = j["User"]["Id"].as_string("");
	user_name_ = j["User"]["Name"].as_string(fallback_user);
	platform::log("jellyfin: signed in");
	save();
	auth_rev++;
}

void Client::login(const std::string &user, const std::string &password, DoneCallback done) {
	quick_connect_cancel();
	http::Request req;
	req.method = "POST";
	req.url = server_ + "/Users/AuthenticateByName";
	req.headers = { "Authorization: " + auth_value(), "Content-Type: application/json" };
	Json body = Json::object();
	body.set("Username", user);
	body.set("Pw", password);
	req.body = body.dump();
	user_name_ = user; // remembered for next time, even if this attempt fails
	save();
	http::send(req, [this, user, done](http::Response &res) {
		if (!res.ok()) {
			platform::log("jellyfin: login failed: HTTP %d %s", res.status, res.error.c_str());
			done(false, (res.status == 401 || res.status == 400) ? "Wrong username or password" : describe_failure(res));
			return;
		}
		Json j = Json::parse(res.body);
		if (j["AccessToken"].as_string("").empty()) {
			done(false, "Unexpected login response");
			return;
		}
		finish_auth(j, user);
		done(true, "");
	});
}

void Client::quick_connect_start(CodeCallback done) {
	quick_connect_cancel();
	const int gen = qc_generation_;
	http::Request req;
	req.method = "POST";
	req.url = server_ + "/QuickConnect/Initiate";
	req.headers = { "Authorization: " + auth_value() };
	http::send(req, [this, gen, done](http::Response &res) {
		if (gen != qc_generation_) return;
		if (!res.ok()) {
			platform::log("jellyfin: Quick Connect initiate failed: %s", describe_failure(res).c_str());
			done(false, res.status == 401 ? "Quick Connect is disabled on this server" : describe_failure(res), "");
			return;
		}
		Json j = Json::parse(res.body);
		qc_secret_ = j["Secret"].as_string("");
		const std::string code = j["Code"].as_string("");
		if (qc_secret_.empty() || code.empty()) {
			done(false, "Unexpected Quick Connect response", "");
			return;
		}
		platform::log("jellyfin: Quick Connect code issued");
		done(true, "", code);
	});
}

void Client::quick_connect_poll(QuickPollCallback done) {
	if (qc_secret_.empty()) {
		done(-1, "Quick Connect not started");
		return;
	}
	const int gen = qc_generation_;
	const std::string secret = qc_secret_;
	http::Request req;
	req.url = server_ + "/QuickConnect/Connect?secret=" + url_encode(secret);
	req.headers = { "Authorization: " + auth_value() };
	http::send(req, [this, gen, secret, done](http::Response &res) {
		if (gen != qc_generation_) return;
		if (!res.ok()) {
			platform::log("jellyfin: Quick Connect poll failed: %s", describe_failure(res).c_str());
			// 404: the request expired on the server.
			done(-1, res.status == 404 ? "The code expired. Start Quick Connect again." : describe_failure(res));
			return;
		}
		if (!Json::parse(res.body)["Authenticated"].as_bool(false)) {
			done(0, "");
			return;
		}
		http::Request auth;
		auth.method = "POST";
		auth.url = server_ + "/Users/AuthenticateWithQuickConnect";
		auth.headers = { "Authorization: " + auth_value(), "Content-Type: application/json" };
		Json body = Json::object();
		body.set("Secret", secret);
		auth.body = body.dump();
		http::send(auth, [this, gen, done](http::Response &res2) {
			if (gen != qc_generation_) return;
			if (!res2.ok()) {
				platform::log("jellyfin: Quick Connect sign-in failed: %s", describe_failure(res2).c_str());
				done(-1, "Quick Connect sign-in failed: " + describe_failure(res2));
				return;
			}
			Json j = Json::parse(res2.body);
			if (j["AccessToken"].as_string("").empty()) {
				done(-1, "Unexpected Quick Connect response");
				return;
			}
			qc_secret_.clear();
			platform::log("jellyfin: Quick Connect approved");
			finish_auth(j, "");
			done(1, "");
		});
	});
}

void Client::quick_connect_cancel() {
	qc_secret_.clear();
	qc_generation_++;
}

void Client::logout() {
	if (!token_.empty()) post_json("/Sessions/Logout", Json::object());
	token_.clear();
	user_id_.clear();
	save(); // server and username stay remembered
	auth_rev++;
}

void Client::get_items(const std::string &path, ItemsCallback done) {
	http::Request req;
	req.url = url(path);
	req.headers = auth_headers();
	http::send(req, [this, path, done](http::Response &res) {
		if (!res.ok()) {
			platform::log("jellyfin: GET %s failed: %s", path.substr(0, path.find('?')).c_str(), describe_failure(res).c_str());
			if (res.status == 401) unauthorized(); // token expired / revoked
			done(false, describe_failure(res), {});
			return;
		}
		Json j = Json::parse(res.body);
		std::vector<Item> items;
		const Json &arr = j.is_array() ? j : j["Items"];
		if (arr.is_array()) {
			for (size_t i = 0; i < arr.size(); i++) items.push_back(Item::from_json(arr[i]));
		} else if (j.is_object() && j.has("Id")) {
			items.push_back(Item::from_json(j));
		}
		platform::log("jellyfin: GET %s -> %d items", path.substr(0, path.find('?')).c_str(), (int)items.size());
		done(true, "", items);
	});
}

static const char *LIST_FIELDS = "Fields=Overview,MediaSources,ProductionYear&EnableImageTypes=Primary&ImageTypeLimit=1&EnableUserData=true";

void Client::movies(ItemsCallback done) {
	get_items("/Items?userId=" + user_id_ + "&IncludeItemTypes=Movie&Recursive=true&SortBy=SortName&SortOrder=Ascending&Limit=1000&" + LIST_FIELDS, done);
}

void Client::series(ItemsCallback done) {
	get_items("/Items?userId=" + user_id_ + "&IncludeItemTypes=Series&Recursive=true&SortBy=SortName&SortOrder=Ascending&Limit=500&" + LIST_FIELDS, done);
}

void Client::resume(ItemsCallback done) {
	get_items("/UserItems/Resume?userId=" + user_id_ + "&MediaTypes=Video&Limit=30&" + LIST_FIELDS, done);
}

void Client::next_up(ItemsCallback done) {
	get_items("/Shows/NextUp?userId=" + user_id_ + "&Limit=30&EnableResumable=false&" + LIST_FIELDS, done);
}

void Client::next_episode(const Item &episode, ItemsCallback done) {
	if (episode.series_id.empty()) {
		done(true, "", {});
		return;
	}
	const std::string id = episode.id;
	get_items("/Shows/" + episode.series_id + "/Episodes?userId=" + user_id_ + "&adjacentTo=" + id + "&" + LIST_FIELDS,
			[id, done](bool ok, const std::string &error, const std::vector<Item> &items) {
				std::vector<Item> next;
				for (size_t i = 0; i + 1 < items.size(); i++) {
					if (items[i].id == id) {
						next.push_back(items[i + 1]);
						break;
					}
				}
				done(ok, error, next);
			});
}

void Client::segments(const std::string &item_id, SegmentsCallback done) {
	http::Request req;
	req.url = url("/MediaSegments/" + item_id);
	req.headers = auth_headers();
	http::send(req, [done](http::Response &res) {
		std::vector<Segment> out;
		if (res.ok()) {
			const Json items = Json::parse(res.body)["Items"];
			for (size_t i = 0; i < items.size(); i++) {
				Segment s;
				s.type = items[i]["Type"].as_string("");
				s.start = items[i]["StartTicks"].as_int(0) / 1e7;
				s.end = items[i]["EndTicks"].as_int(0) / 1e7;
				if (s.end > s.start && s.type != "Unknown") out.push_back(s);
			}
		}
		platform::log("jellyfin: %d media segments (HTTP %d)", (int)out.size(), res.status);
		done(out);
	});
}

void Client::episodes(const std::string &series_id, ItemsCallback done) {
	get_items("/Shows/" + series_id + "/Episodes?userId=" + user_id_ + "&" + LIST_FIELDS, done);
}

void Client::item(const std::string &id, ItemsCallback done) {
	get_items("/Items/" + id + "?userId=" + user_id_, done);
}

std::string Client::image_url(const Item &item, int height) const {
	if (item.image_tag.empty()) return "";
	return url("/Items/" + item.id + "/Images/Primary?fillHeight=" + std::to_string(height) +
			"&quality=85&format=Jpg&tag=" + item.image_tag);
}

void Client::play_plan(const Item &item, bool transcode, double start_seconds, int audio_index, PlanCallback done) {
	PlayPlan plan;
	plan.headers = auth_headers();
	plan.media_source_id = item.media_source_id;
	plan.transcode = transcode;
	// Ask the server for a play session id (and register the playback), with
	// a device profile describing what this client can do.
	http::Request req;
	req.method = "POST";
	req.url = url("/Items/" + item.id + "/PlaybackInfo?userId=" + user_id_);
	req.headers = auth_headers();
	req.headers.push_back("Content-Type: application/json");
	Json body = Json::object();
	body.set("UserId", user_id_);
	body.set("MediaSourceId", item.media_source_id);
	body.set("EnableDirectPlay", !transcode);
	body.set("EnableDirectStream", !transcode);
	body.set("EnableTranscoding", true);
	body.set("AutoOpenLiveStream", true);
	body.set("MaxStreamingBitrate", 120000000);
	Json profile = Json::object();
	profile.set("Name", "Jellyfin PS4");
	profile.set("MaxStreamingBitrate", 120000000);
	profile.set("MaxStaticBitrate", 120000000);
	Json direct = Json::array();
	{
		Json d = Json::object();
		d.set("Type", "Video");
		d.set("Container", "mp4,m4v,mkv,mov,ts,avi,webm");
		d.set("VideoCodec", "h264,hevc,mpeg4,mpeg2video,vp9,av1");
		d.set("AudioCodec", "aac,ac3,eac3,mp3,mp2,opus,vorbis,flac,dts,truehd");
		direct.push(d);
	}
	profile.set("DirectPlayProfiles", direct);
	Json tps = Json::array();
	{
		Json t = Json::object();
		t.set("Type", "Video");
		t.set("Container", "ts");
		t.set("Protocol", "http");
		t.set("VideoCodec", "h264");
		t.set("AudioCodec", "aac");
		t.set("Context", "Streaming");
		t.set("MaxAudioChannels", "2");
		tps.push(t);
	}
	profile.set("TranscodingProfiles", tps);
	profile.set("CodecProfiles", Json::array());
	profile.set("ContainerProfiles", Json::array());
	profile.set("SubtitleProfiles", Json::array());
	body.set("DeviceProfile", profile);
	req.body = body.dump();
	const std::string id = item.id;
	http::send(req, [this, id, transcode, start_seconds, audio_index, plan, done](http::Response &res) mutable {
		if (res.ok()) {
			Json j = Json::parse(res.body);
			plan.play_session_id = j["PlaySessionId"].as_string("");
			const Json &ms = j["MediaSources"];
			if (ms.is_array() && ms.size() > 0) {
				if (plan.media_source_id.empty()) plan.media_source_id = ms[0]["Id"].as_string("");
				platform::log("jellyfin: PlaybackInfo: direct play %d, direct stream %d, transcode url %s",
						ms[0]["SupportsDirectPlay"].as_bool(), ms[0]["SupportsDirectStream"].as_bool(),
						ms[0]["TranscodingUrl"].is_string() ? "yes" : "no");
			}
		} else {
			platform::log("jellyfin: PlaybackInfo failed: %s", describe_failure(res).c_str());
			if (res.status == 401) {
				done(false, "Your session has expired", plan);
				unauthorized();
				return;
			}
			// Otherwise not fatal: direct play works without a play session.
		}
		if (plan.play_session_id.empty()) plan.play_session_id = random_hex(32);
		std::string q = "mediaSourceId=" + url_encode(plan.media_source_id) + "&playSessionId=" + plan.play_session_id +
				"&deviceId=" + url_encode(device_id_);
		if (!transcode) {
			plan.play_method = "DirectPlay";
			plan.url = url("/Videos/" + id + "/stream?static=true&" + q);
			plan.start_seconds = 0;
		} else {
			plan.play_method = "Transcode";
			char extra[512];
			snprintf(extra, sizeof(extra),
					"&VideoCodec=h264&AudioCodec=aac&MaxAudioChannels=2&AudioBitrate=%d&VideoBitrate=%d&MaxHeight=1080"
					"&MaxFramerate=30&TranscodingMaxAudioChannels=2&AllowAudioStreamCopy=false&BreakOnNonKeyFrames=false"
					"&StartTimeTicks=%lld",
					TRANSCODE_AUDIO_BITRATE, TRANSCODE_VIDEO_BITRATE, (long long)(start_seconds * 1e7));
			plan.url = url("/Videos/" + id + "/stream.ts?static=false&" + q + extra);
			// Never SubtitleStreamIndex: that would make the server burn subtitles in.
			if (audio_index >= 0) plan.url += "&AudioStreamIndex=" + std::to_string(audio_index);
			plan.start_seconds = start_seconds;
		}
		platform::log("jellyfin: play %s via %s", id.c_str(), plan.play_method.c_str());
		done(true, "", plan);
	});
}

std::string Client::subtitle_url(const Item &item, const std::string &media_source_id, int index) const {
	return url("/Videos/" + item.id + "/" + (media_source_id.empty() ? item.id : media_source_id) + "/Subtitles/" +
			std::to_string(index) + "/Stream.srt");
}

void Client::get_text(const std::string &u, std::function<void(bool ok, const std::string &body)> done) {
	http::Request req;
	req.url = u;
	req.headers = auth_headers();
	http::send(req, [this, done](http::Response &res) {
		if (!res.ok()) {
			platform::log("jellyfin: GET text failed: %s", describe_failure(res).c_str());
			if (res.status == 401) unauthorized();
		}
		done(res.ok(), res.body);
	});
}

void Client::post_json(const std::string &path, const Json &body) {
	http::Request req;
	req.method = "POST";
	req.url = url(path);
	req.headers = auth_headers();
	req.headers.push_back("Content-Type: application/json");
	req.body = body.dump();
	http::send(req, [path](http::Response &res) {
		if (!res.ok()) platform::log("jellyfin: POST %s failed: HTTP %d %s", path.c_str(), res.status, res.error.c_str());
	});
}

static Json playback_body(const Item &item, const PlayPlan &plan, double position) {
	Json b = Json::object();
	b.set("ItemId", item.id);
	b.set("MediaSourceId", plan.media_source_id);
	b.set("PlaySessionId", plan.play_session_id);
	b.set("PositionTicks", (int64_t)(position * 1e7));
	return b;
}

void Client::report_start(const Item &item, const PlayPlan &plan, double position) {
	Json b = playback_body(item, plan, position);
	b.set("CanSeek", true);
	b.set("PlayMethod", plan.play_method);
	b.set("IsPaused", false);
	post_json("/Sessions/Playing", b);
}

void Client::report_progress(const Item &item, const PlayPlan &plan, double position, bool paused) {
	Json b = playback_body(item, plan, position);
	b.set("CanSeek", true);
	b.set("PlayMethod", plan.play_method);
	b.set("IsPaused", paused);
	b.set("EventName", "timeupdate");
	post_json("/Sessions/Playing/Progress", b);
}

void Client::report_stop(const Item &item, const PlayPlan &plan, double position) {
	http::Request req;
	req.method = "POST";
	req.url = url("/Sessions/Playing/Stopped");
	req.headers = auth_headers();
	req.headers.push_back("Content-Type: application/json");
	req.body = playback_body(item, plan, position).dump();
	platform::log("jellyfin: report stop at %.0f s", position);
	http::send(req, [this](http::Response &res) {
		if (!res.ok()) platform::log("jellyfin: stop report failed: HTTP %d %s", res.status, res.error.c_str());
		library_rev++; // reload lists (Continue Watching) now that the server has the position
	});
}

} // namespace jf
