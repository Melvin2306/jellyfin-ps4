// Jellyfin API client: session (login, persisted token), library queries,
// playback decisions and progress reporting. All network calls are async
// (net/http.h); callbacks run on the main thread.
//
// Auth (Jellyfin 10.10+/12.x): every request carries
//   Authorization: MediaBrowser Client="..", Device="..", DeviceId="..", Version="..", Token=".."
// (the legacy X-Emby-Token header is rejected by this server version).
#pragma once

#include "json.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace jf {

// An audio or subtitle stream of an item's first media source.
struct Stream {
	int index = -1;          // Jellyfin MediaStream Index (= the file's stream index when embedded)
	std::string type;        // "Audio" or "Subtitle"
	std::string codec;       // e.g. "ac3", "subrip", "ass", "PGSSUB", "dvdsub"
	std::string language;    // ISO 639-2, may be empty
	std::string title;       // DisplayTitle from the server
	bool is_default = false, is_forced = false, is_external = false;
	bool is_text = false;    // text subtitle (SRT/ASS/VTT/mov_text...)
	int channels = 0;
};

struct Item {
	std::string id, name, type; // type: Movie, Series, Episode, Season...
	std::string overview, series_name, series_id, container;
	int year = 0;
	int season = -1, episode = -1;
	int64_t runtime_ticks = 0, position_ticks = 0;
	bool played = false;
	std::string image_tag; // Primary image tag ("" = none)
	std::string media_source_id;
	std::string video_codec, audio_codec;
	int width = 0, height = 0, audio_channels = 0;
	int64_t bitrate = 0;
	std::vector<Stream> streams; // audio + subtitles of the first media source

	std::vector<Stream> audio_streams() const;
	std::vector<Stream> subtitle_streams() const;
	const Stream *stream(int index) const;

	static Item from_json(const Json &j);
	double runtime_seconds() const { return runtime_ticks / 1e7; }
	double position_seconds() const { return position_ticks / 1e7; }
	// "S1 E3 · Title" for episodes, else the name.
	std::string display_name() const;
	// "2019 · 1 h 52 min · HEVC 1920x1080 · AC3 5.1"
	std::string info_line() const;
};

// How a title is going to be played.
struct PlayPlan {
	std::string url;
	std::vector<std::string> headers;
	std::string play_method; // "DirectPlay" or "Transcode"
	std::string play_session_id;
	std::string media_source_id;
	bool transcode = false;
	double start_seconds = 0; // transcode: the server starts the stream here
};

using ItemsCallback = std::function<void(bool ok, const std::string &error, const std::vector<Item> &items)>;
using DoneCallback = std::function<void(bool ok, const std::string &error)>;
using PlanCallback = std::function<void(bool ok, const std::string &error, const PlayPlan &plan)>;

struct ServerInfo {
	std::string url;     // normalised
	std::string name, version;
	bool quick_connect = false;
};
using ServerCallback = std::function<void(bool ok, const std::string &error, const ServerInfo &info)>;
using CodeCallback = std::function<void(bool ok, const std::string &error, const std::string &code)>;
// state: 0 waiting, 1 signed in, -1 failed (error set)
using QuickPollCallback = std::function<void(int state, const std::string &error)>;

class Client {
public:
	// Loads the saved session (if any) from /data/jellyfin/session.json.
	void init();
	bool logged_in() const { return !token_.empty(); }
	int auth_rev = 0;    // bumped on login / logout
	int library_rev = 0; // bumped when playback stops (positions changed)

	const std::string &server() const { return server_; }
	const std::string &user_name() const { return user_name_; }
	// Remembered from the last sign-in (kept after signing out); never the password.
	const std::string &saved_server() const { return server_; }
	const std::string &saved_user() const { return user_name_; }

	// A message for the sign-in screen (e.g. the saved session expired);
	// cleared once read.
	std::string take_notice();

	// Normalises the URL (adds http://), checks /System/Info/Public and
	// whether Quick Connect is enabled. On success the server becomes current.
	void check_server(const std::string &url, ServerCallback done);
	// POST /Users/AuthenticateByName on the checked server.
	void login(const std::string &user, const std::string &password, DoneCallback done);

	// Quick Connect: start() gets a code to show; poll() every few seconds
	// until it reports approval (then the session is created) or an error.
	void quick_connect_start(CodeCallback done);
	void quick_connect_poll(QuickPollCallback done);
	void quick_connect_cancel();

	void logout();

	void movies(ItemsCallback done);
	void series(ItemsCallback done);
	void resume(ItemsCallback done);
	void episodes(const std::string &series_id, ItemsCallback done);
	void item(const std::string &id, ItemsCallback done); // one item, full fields

	// Poster URL (JPEG, scaled by the server). "" if the item has no image.
	std::string image_url(const Item &item, int height) const;

	// Direct play: the original file over HTTP (Range requests), decoded on
	// the PS4 by FFmpeg. Transcode: server-side H.264/AAC in MPEG-TS,
	// streamed progressively (fallback for files the PS4 can't keep up with).
	// `audio_index`: Jellyfin stream index for the transcode (-1 = default).
	void play_plan(const Item &item, bool transcode, double start_seconds, int audio_index, PlanCallback done);

	// Text subtitles as SRT, converted by the server (external files, and
	// embedded text tracks when transcoding).
	std::string subtitle_url(const Item &item, const std::string &media_source_id, int index) const;
	// GET with the session's auth; the body as text.
	void get_text(const std::string &url, std::function<void(bool ok, const std::string &body)> done);

	const std::string &user_id() const { return user_id_; }

	// Session reporting so "Continue watching" and played state work.
	void report_start(const Item &item, const PlayPlan &plan, double position);
	void report_progress(const Item &item, const PlayPlan &plan, double position, bool paused);
	void report_stop(const Item &item, const PlayPlan &plan, double position);

	std::vector<std::string> auth_headers() const;

private:
	std::string auth_value() const;
	std::string url(const std::string &path) const;
	void get_items(const std::string &path, ItemsCallback done);
	void post_json(const std::string &path, const Json &body);
	void save();
	void finish_auth(const Json &result, const std::string &fallback_user);
	// A request with the saved token got 401: the token expired or was revoked.
	void unauthorized();

	std::string server_, user_id_, user_name_, token_, device_id_;
	std::string notice_;
	std::string qc_secret_;
	int qc_generation_ = 0;
};

Client &client();

// Percent-encodes a query parameter value.
std::string url_encode(const std::string &s);

} // namespace jf
