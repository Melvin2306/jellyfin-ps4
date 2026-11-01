#include "player_screen.h"

#include "../core/settings.h"
#include "../core/tracks.h"
#include "../platform/platform.h"
#include "theme.h"

#include <algorithm>
#include <cstdio>

namespace {
const double REPORT_INTERVAL = 10;
const double SEEK_DELAY = 0.7;
const double OVERLAY_SECONDS = 4;

std::string clock_text(double seconds) {
	if (seconds < 0) seconds = 0;
	const int s = (int)seconds;
	char buf[32];
	if (s >= 3600) snprintf(buf, sizeof(buf), "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
	else snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
	return buf;
}

const char *state_name(VideoPlayer::State s) {
	switch (s) {
		case VideoPlayer::IDLE: return "idle";
		case VideoPlayer::OPENING: return "opening";
		case VideoPlayer::BUFFERING: return "buffering";
		case VideoPlayer::PLAYING: return "playing";
		case VideoPlayer::PAUSED: return "paused";
		case VideoPlayer::ENDED: return "ended";
		case VideoPlayer::FAILED: return "failed";
	}
	return "?";
}
} // namespace

PlayerScreen::PlayerScreen(Fonts &fonts, Navigator &nav, const Args &args) : fonts_(fonts), nav_(nav), args_(args) {
	audio_ = args.audio != -2 ? args.audio : tracks::default_audio(args.item);
	subtitle_ = args.subtitle != -2 ? args.subtitle : tracks::default_subtitle(args.item, args.transcode);
	platform::log("player: tracks audio %d, subtitle %d", audio_, subtitle_);
	start(args.transcode, args.start);
	std::weak_ptr<int> alive = alive_;
	jf::client().segments(args.item.id, [this, alive](const std::vector<jf::Segment> &segs) {
		if (!alive.expired()) segments_ = segs;
	});
	if (args.item.type == "Episode") {
		jf::client().next_episode(args.item, [this, alive](bool ok, const std::string &, const std::vector<jf::Item> &items) {
			if (alive.expired() || !ok || items.empty()) return;
			next_ = items[0];
			has_next_ = true;
			platform::log("player: next episode is %s", next_.display_name().c_str());
		});
	}
}

const jf::Segment *PlayerScreen::current_segment() const {
	const double pos = position();
	for (const jf::Segment &s : segments_) {
		// Not in the last 2 s of a segment (the skip would be pointless).
		if (pos >= s.start && pos < s.end - 2) return &s;
	}
	return nullptr;
}

void PlayerScreen::skip_segment(const jf::Segment &s) {
	platform::log("player: skip %s %.0f -> %.0f s", s.type.c_str(), s.start, s.end);
	const bool credits = s.type == "Outro";
	// Credits that run to (almost) the end: on to the next episode.
	if (credits && has_next_ && (duration() <= 0 || s.end >= duration() - 30)) {
		last_position_ = std::max(last_position_, s.start);
		begin_countdown();
		return;
	}
	const double target = s.end;
	if (transcode_) {
		last_position_ = target;
		start(true, target);
	} else if (player_) {
		player_->seek(target);
	}
}

void PlayerScreen::begin_countdown() {
	const bool autoplay = settings::get_bool("autoplay_next", true);
	platform::log("player: episode over; next %s, autoplay %d", has_next_ ? "available" : "none", autoplay);
	if (!has_next_) {
		exit();
		return;
	}
	if (player_) player_->pause();
	countdown_ = autoplay ? 10.0 : 1e9; // without autoplay: wait for X
}

void PlayerScreen::play_next() {
	platform::log("player: playing next episode");
	// Mark this one watched: report the stop at the end.
	if (duration() > 0) last_position_ = duration();
	stop_reporting();
	player_.reset();
	PlayerScreen::Args args;
	args.item = next_;
	nav_.pop();
	nav_.push(std::unique_ptr<Screen>(new PlayerScreen(fonts_, nav_, args)));
}

PlayerScreen::~PlayerScreen() {
	stop_reporting();
	player_.reset(); // stops the decoder threads
}

void PlayerScreen::start(bool transcode, double at) {
	stop_reporting();
	player_.reset();
	have_plan_ = false;
	transcode_ = transcode;
	last_state_ = VideoPlayer::IDLE;
	error_.clear();
	last_position_ = at;
	tracks_applied_ = false;
	std::weak_ptr<int> alive = alive_;
	platform::log("player: start '%s' (%s) %s at %.0f s", args_.item.name.c_str(), args_.item.id.c_str(),
			transcode ? "transcode" : "direct", at);
	jf::client().play_plan(args_.item, transcode, at, transcode ? audio_ : -1, [this, alive, transcode, at](bool ok, const std::string &error,
			const jf::PlayPlan &plan) {
		if (alive.expired()) return;
		if (!ok) {
			error_ = error;
			return;
		}
		plan_ = plan;
		have_plan_ = true;
		player_ = VideoPlayer::create();
		// Direct play seeks inside the file; a transcode starts at `at` already.
		player_->open(plan_.url, plan_.headers, transcode ? 0 : at);
	});
}

// FFmpeg's stream id for a Jellyfin stream index. For embedded streams they
// are the same (Jellyfin reports the file's ffprobe indexes); otherwise
// match by order among the streams of that kind.
int PlayerScreen::ffmpeg_track(int index, bool audio) const {
	if (!player_ || index < 0) return -1;
	const std::vector<VideoPlayer::Track> ts = audio ? player_->audio_tracks() : player_->subtitle_tracks();
	for (const VideoPlayer::Track &t : ts)
		if (t.id == index) return index;
	int ordinal = 0;
	for (const jf::Stream &s : args_.item.streams) {
		if (s.is_external || s.type != (audio ? "Audio" : "Subtitle")) continue;
		if (s.index == index) return ordinal < (int)ts.size() ? ts[ordinal].id : -1;
		ordinal++;
	}
	return -1;
}

void PlayerScreen::apply_tracks() {
	tracks_applied_ = true;
	if (!transcode_ && audio_ >= 0) {
		const int id = ffmpeg_track(audio_, true);
		if (id >= 0 && id != player_->audio_track()) {
			platform::log("player: audio track %d (ffmpeg %d)", audio_, id);
			player_->set_audio_track(id);
		}
	}
	apply_subtitle();
}

void PlayerScreen::apply_subtitle() {
	ext_generation_++;
	ext_cues_.clear();
	ext_active_ = false;
	ext_loading_ = false;
	if (!player_) return;
	const jf::Stream *s = subtitle_ >= 0 ? args_.item.stream(subtitle_) : nullptr;
	if (!s) {
		player_->set_subtitle_track(-1);
		return;
	}
	if (!transcode_ && !s->is_external) {
		const int id = ffmpeg_track(subtitle_, false);
		platform::log("player: embedded subtitle %d (ffmpeg %d, %s)", subtitle_, id, s->codec.c_str());
		if (id >= 0) {
			player_->set_subtitle_track(id);
			return;
		}
		if (!s->is_text) {
			toast_.show("This subtitle can't be shown", true);
			return;
		}
	}
	player_->set_subtitle_track(-1);
	if (!s->is_text) {
		toast_.show("Image subtitles need direct play", true);
		return;
	}
	// External file, or text subtitles while transcoding: the server's SRT.
	ext_active_ = true;
	ext_loading_ = true;
	const int gen = ext_generation_;
	std::weak_ptr<int> alive = alive_;
	const std::string url = jf::client().subtitle_url(args_.item, plan_.media_source_id, subtitle_);
	platform::log("player: fetching subtitle %d as SRT", subtitle_);
	jf::client().get_text(url, [this, alive, gen](bool ok, const std::string &body) {
		if (alive.expired() || gen != ext_generation_) return;
		ext_loading_ = false;
		std::vector<SubtitleCue> cues;
		if (!ok || !parse_subtitles(body, cues)) {
			platform::log("player: subtitle download/parse failed (%d bytes)", (int)body.size());
			toast_.show("Couldn't load the subtitles", true);
			ext_active_ = false;
			return;
		}
		platform::log("player: %d subtitle cues", (int)cues.size());
		ext_cues_ = std::move(cues);
	});
}

double PlayerScreen::position() const {
	if (!player_ || !have_plan_) return last_position_;
	const VideoPlayer::State s = player_->state();
	if (s == VideoPlayer::OPENING || s == VideoPlayer::IDLE) return last_position_;
	return (transcode_ ? plan_.start_seconds : 0) + player_->position();
}

double PlayerScreen::duration() const {
	if (args_.item.runtime_ticks > 0) return args_.item.runtime_seconds();
	return player_ ? player_->duration() : 0;
}

void PlayerScreen::stop_reporting() {
	// Also for a transcode that never started playing: the stop report makes
	// the server end its ffmpeg job (matched by PlaySessionId / DeviceId).
	if (reported_start_ || (have_plan_ && transcode_)) {
		jf::client().report_stop(args_.item, plan_, last_position_);
	}
	reported_start_ = false;
	have_plan_ = false;
}

void PlayerScreen::exit() {
	platform::log("player: exit at %.0f s", last_position_);
	stop_reporting();
	player_.reset();
	nav_.pop();
}

void PlayerScreen::update(double dt, uint32_t pressed) {
	toast_.update(dt);
	if (player_) player_->update();
	const VideoPlayer::State state = player_ ? player_->state() : VideoPlayer::OPENING;
	if (state != last_state_) {
		platform::log("player: state %s -> %s", state_name(last_state_), state_name(state));
		if (state == VideoPlayer::FAILED) platform::log("player: error: %s", player_->error().c_str());
		last_state_ = state;
	}
	const double pos = position();
	if (state == VideoPlayer::PLAYING || state == VideoPlayer::PAUSED) last_position_ = pos;

	if ((state == VideoPlayer::PLAYING || state == VideoPlayer::PAUSED) && !tracks_applied_) apply_tracks();

	if (menu_.visible()) {
		const TrackMenu::Result res = menu_.update(dt, pressed);
		if (res == TrackMenu::AUDIO_CHANGED) {
			audio_ = menu_.audio();
			if (transcode_) {
				// The server picks the audio of a transcode: restart it here.
				start(true, pos);
				return;
			}
			const int id = ffmpeg_track(audio_, true);
			if (id >= 0) player_->set_audio_track(id);
		} else if (res == TrackMenu::SUBTITLE_CHANGED) {
			subtitle_ = menu_.subtitle();
			apply_subtitle();
		}
		pressed = 0; // the menu has the controller
	} else if ((pressed & platform::BUTTON_TRIANGLE) && player_ && have_plan_) {
		menu_.open(args_.item, transcode_, audio_, subtitle_, 1);
		pressed = 0;
	}

	if (state == VideoPlayer::PLAYING) {
		ever_played_ = true;
		if (!reported_start_) {
			jf::client().report_start(args_.item, plan_, pos);
			reported_start_ = true;
			report_timer_ = 0;
		}
	}
	if (reported_start_) {
		report_timer_ += dt;
		if (report_timer_ >= REPORT_INTERVAL) {
			report_timer_ = 0;
			jf::client().report_progress(args_.item, plan_, pos, state == VideoPlayer::PAUSED);
		}
	}
	// Periodic playback health line in the log (readable over FTP).
	stat_timer_ += dt;
	if (stat_timer_ >= 30 && state == VideoPlayer::PLAYING) {
		stat_timer_ = 0;
		platform::log("player: playing at %s / %s", clock_text(pos).c_str(), clock_text(duration()).c_str());
	}

	// Direct play failed before anything played: try a server transcode once.
	if (state == VideoPlayer::FAILED && !transcode_ && !ever_played_ && !fell_back_) {
		fell_back_ = true;
		platform::log("player: direct play failed (%s); falling back to transcode", player_->error().c_str());
		toast_.show("Direct play failed \xE2\x80\x94 trying a server transcode");
		start(true, last_position_);
		return;
	}
	if (countdown_ >= 0) {
		countdown_ -= dt;
		if ((pressed & platform::BUTTON_ACCEPT) || countdown_ <= 0) {
			play_next();
		} else if (pressed & platform::BUTTON_BACK) {
			countdown_ = -1;
			if (duration() > 0) last_position_ = duration(); // counts as watched
			exit();
		}
		return;
	}
	if (state == VideoPlayer::ENDED) {
		platform::log("player: ended");
		last_position_ = duration() > 0 ? duration() : last_position_;
		if (has_next_) {
			begin_countdown();
			return;
		}
		exit();
		return;
	}

	if (pressed) {
		overlay_timer_ = OVERLAY_SECONDS;
	}
	overlay_timer_ = std::max(0.0, overlay_timer_ - dt);
	const bool show = overlay_timer_ > 0 || state != VideoPlayer::PLAYING;
	overlay_ = ui::approach(overlay_, show ? 1.0f : 0.0f, dt);

	if (pressed & platform::BUTTON_BACK) {
		exit();
		return;
	}
	if (!player_) return;
	const jf::Segment *seg = state == VideoPlayer::PLAYING ? current_segment() : nullptr;
	skip_focus_ = ui::approach(skip_focus_, seg ? 1.0f : 0.0f, dt);
	if (seg && (pressed & platform::BUTTON_ACCEPT)) {
		skip_segment(*seg);
		return;
	}
	if ((pressed & platform::BUTTON_ACCEPT) && (state == VideoPlayer::PLAYING || state == VideoPlayer::PAUSED)) {
		if (state == VideoPlayer::PLAYING) player_->pause();
		else player_->play();
		jf::client().report_progress(args_.item, plan_, pos, state == VideoPlayer::PLAYING);
	}
	double step = 0;
	if (pressed & platform::BUTTON_LEFT) step -= 10;
	if (pressed & platform::BUTTON_RIGHT) step += 10;
	if (pressed & platform::BUTTON_L1) step -= 60;
	if (pressed & platform::BUTTON_R1) step += 60;
	if (step != 0 && (state == VideoPlayer::PLAYING || state == VideoPlayer::PAUSED)) {
		const double base = pending_seek_ >= 0 ? pending_seek_ : pos;
		pending_seek_ = std::max(0.0, base + step);
		if (duration() > 0) pending_seek_ = std::min(pending_seek_, duration() - 5);
		seek_timer_ = SEEK_DELAY;
	}
	if (pending_seek_ >= 0) {
		seek_timer_ -= dt;
		if (seek_timer_ <= 0) {
			const double target = pending_seek_;
			pending_seek_ = -1;
			platform::log("player: seek to %.0f s", target);
			if (transcode_) {
				// A live transcode can't seek: restart it at the target.
				last_position_ = target;
				start(true, target);
			} else {
				player_->seek(target);
			}
		}
	}
}

void PlayerScreen::draw(Renderer &r) {
	r.rect(0, 0, 1920, 1080, rgb(0x000000));
	const VideoPlayer::State state = player_ ? player_->state() : VideoPlayer::OPENING;
	if (player_) {
		const Texture &frame = player_->frame();
		if (frame.valid()) {
			const float aspect = (float)frame.picture_width() / (float)frame.height;
			float vw = 1920, vh = 1920 / aspect;
			if (vh > 1080) {
				vh = 1080;
				vw = 1080 * aspect;
			}
			r.image_stretch(frame, (1920 - vw) / 2, (1080 - vh) / 2, vw, vh);
			draw_subtitles(r, (1920 - vw) / 2, (1080 - vh) / 2, vw, vh);
		}
	}
	// Centre status while nothing is playing.
	std::string status;
	if (!error_.empty()) status = "Couldn't start playback: " + error_;
	else if (state == VideoPlayer::FAILED) status = "Playback failed: " + player_->error();
	else if (state == VideoPlayer::OPENING || state == VideoPlayer::IDLE) status = transcode_ ? "Starting server transcode\xE2\x80\xA6" : "Loading\xE2\x80\xA6";
	else if (state == VideoPlayer::BUFFERING) status = "Buffering\xE2\x80\xA6";
	if (!status.empty()) {
		const std::string s = fonts_.section.ellipsize(status, 1600);
		r.text(fonts_.section, s, (1920 - fonts_.section.measure(s)) / 2, 540, state == VideoPlayer::FAILED || !error_.empty() ? theme::ERROR : theme::BASE_CONTENT);
		const std::string detail = player_ ? player_->source_status() : "";
		if (!detail.empty()) r.text(fonts_.body, detail, (1920 - fonts_.body.measure(detail)) / 2, 600, theme::BASE_CONTENT_MUTED);
		if (state == VideoPlayer::FAILED || !error_.empty()) {
			r.text(fonts_.body, "Press \xE2\x97\x8B to go back", (1920 - fonts_.body.measure("Press \xE2\x97\x8B to go back")) / 2, 660, theme::BASE_CONTENT_MUTED);
		}
	}
	if (overlay_ > 0.01f) {
		r.push(960, 540, 1.0f, overlay_);
		r.vertical_fade(0, 780, 1920, 300, rgb(0x000000), 0.0f, 0.85f);
		const double pos = pending_seek_ >= 0 ? pending_seek_ : position();
		const double dur = duration();
		r.text(fonts_.section, fonts_.section.ellipsize(args_.item.display_name(), 1500), 100, 900, theme::BASE_CONTENT);
		const std::string mode = transcode_ ? "Transcode" : "Direct play";
		r.text(fonts_.small, mode, 1820 - fonts_.small.measure(mode), 900, theme::BASE_CONTENT_MUTED);
		ui::progress(r, 100, 940, 1720, 8, dur > 0 ? (float)std::min(1.0, pos / dur) : 0.0f);
		const std::string times = clock_text(pos) + " / " + clock_text(dur);
		r.text(fonts_.body, times, 100, 1000, theme::BASE_CONTENT);
		ui::hints(r, fonts_.small,
				{ { "\xC3\x97", state == VideoPlayer::PAUSED ? "Play" : "Pause" }, { "\xE2\x97\x80 \xE2\x96\xB6", "10 s" },
						{ "L1 R1", "1 min" }, { "\xE2\x96\xB3", "Audio & subtitles" }, { "\xE2\x97\x8B", "Stop" } },
				1920 - 100 - ui::hints_width(fonts_.small, { { "\xC3\x97", "Pause" }, { "\xE2\x97\x80 \xE2\x96\xB6", "10 s" }, { "L1 R1", "1 min" }, { "\xE2\x96\xB3", "Audio & subtitles" }, { "\xE2\x97\x8B", "Stop" } }),
				1000);
		r.pop();
	}
	if (skip_focus_ > 0.01f && countdown_ < 0) {
		const jf::Segment *seg = current_segment();
		std::string label = "Skip";
		if (seg) {
			if (seg->type == "Intro") label = "Skip intro";
			else if (seg->type == "Outro") label = has_next_ ? "Next episode" : "Skip credits";
			else if (seg->type == "Recap") label = "Skip recap";
			else if (seg->type == "Preview") label = "Skip preview";
			else if (seg->type == "Commercial") label = "Skip ad";
		}
		label = "\xC3\x97  " + label;
		const float w = fonts_.card_title.measure(label) + 64, h = 76;
		const float x = 1920 - 100 - w, y = overlay_ > 0.5f ? 760 : 900;
		r.push(x + w / 2, y + h / 2, 1.0f, skip_focus_);
		r.rounded_rect(x, y, w, h, theme::radius_field(), theme::PRIMARY);
		r.text(fonts_.card_title, label, x + 32, y + h / 2 + 9, theme::PRIMARY_CONTENT);
		r.pop();
	}
	if (countdown_ >= 0) draw_countdown(r);
	menu_.draw(r, fonts_);
	if (toast_.visible()) toast_.draw(r, fonts_.body, 960, 60);
}

void PlayerScreen::draw_countdown(Renderer &r) {
	r.rect(0, 0, 1920, 1080, rgb(0x000000, 0.6f));
	const float x = 1920 - 100 - 760, y = 600, w = 760, h = 300;
	r.rounded_rect(x, y, w, h, theme::radius_box(), theme::BASE_200);
	const std::string head = countdown_ < 1e8 ? "Next episode in " + std::to_string((int)countdown_ + 1) + " s" : "Next episode";
	r.text(fonts_.section, head, x + 40, y + 70, theme::BASE_CONTENT);
	r.text(fonts_.body, fonts_.body.ellipsize(next_.display_name(), w - 80), x + 40, y + 130, theme::BASE_CONTENT);
	if (!next_.series_name.empty()) r.text(fonts_.small, next_.series_name, x + 40, y + 170, theme::BASE_CONTENT_MUTED);
	if (countdown_ < 1e8) ui::progress(r, x + 40, y + 200, w - 80, 6, 1.0f - (float)(countdown_ / 10.0));
	ui::hints(r, fonts_.small, { { "\xC3\x97", "Play now" }, { "\xE2\x97\x8B", "Cancel" } }, x + 40, y + 260);
}

void PlayerScreen::draw_subtitles(Renderer &r, float vx, float vy, float vw, float vh) {
	if (!player_) return;
	std::string text;
	VideoPlayer::Subtitle sub;
	if (ext_active_) {
		if (const SubtitleCue *cue = cue_at(ext_cues_, position())) text = cue->text;
	} else if (subtitle_ >= 0 && player_->current_subtitle(sub)) {
		if (sub.image) {
			r.image_stretch(*sub.image, vx + sub.x * vw, vy + sub.y * vh, sub.w * vw, sub.h * vh);
			return;
		}
		text = sub.text;
	}
	if (text.empty()) return;
	// Size (percent) and distance from the bottom (pixels) from the settings.
	const float scale = (float)settings::get_int("subtitle_size", 100) / 100.0f;
	const float margin = (float)settings::get_int("subtitle_margin", 80);
	Font &f = fonts_.subtitle;
	const float line_h = theme::SIZE_SUBTITLE * 1.3f * scale;
	const std::vector<std::string> lines = f.wrap(text, std::min(vw, 1920.0f) * 0.8f / scale);
	float bottom = std::min(vy + vh, 1080.0f) - margin;
	if (overlay_ > 0.5f) bottom = std::min(bottom, 1080.0f - 230);
	float baseline = bottom - (lines.size() - 1) * line_h;
	for (const std::string &line : lines) {
		const float w = f.measure(line);
		r.push(960, baseline, scale, 1.0f);
		const float x = 960 - w / 2;
		r.rounded_rect(x - 14, baseline - theme::SIZE_SUBTITLE * 0.95f, w + 28, theme::SIZE_SUBTITLE * 1.3f, 8, rgb(0x000000, 0.6f));
		r.text(f, line, x, baseline, rgb(0xFFFFFF));
		r.pop();
		baseline += line_h;
	}
}
