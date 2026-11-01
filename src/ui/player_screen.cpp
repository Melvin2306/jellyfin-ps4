#include "player_screen.h"

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
	start(args.transcode, args.start);
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
	std::weak_ptr<int> alive = alive_;
	platform::log("player: start '%s' (%s) %s at %.0f s", args_.item.name.c_str(), args_.item.id.c_str(),
			transcode ? "transcode" : "direct", at);
	jf::client().play_plan(args_.item, transcode, at, [this, alive, transcode, at](bool ok, const std::string &error,
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
	if (state == VideoPlayer::ENDED) {
		platform::log("player: ended");
		last_position_ = duration() > 0 ? duration() : last_position_;
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
						{ "L1 R1", "1 min" }, { "\xE2\x97\x8B", "Stop" } },
				1920 - 100 - ui::hints_width(fonts_.small, { { "\xC3\x97", "Pause" }, { "\xE2\x97\x80 \xE2\x96\xB6", "10 s" }, { "L1 R1", "1 min" }, { "\xE2\x97\x8B", "Stop" } }),
				1000);
		r.pop();
	}
	if (toast_.visible()) toast_.draw(r, fonts_.body, 960, 60);
}
