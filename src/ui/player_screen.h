// Full-screen player. X play/pause, Left/Right seek 10 s, L1/R1 seek 60 s
// (presses add up), Circle stops. Reports start / progress / stop to
// Jellyfin. If direct play fails before the first frame, it retries once
// through a server transcode (H.264/AAC).
#pragma once

#include "../core/jellyfin.h"
#include "../core/subtitles.h"
#include "../video/video_player.h"
#include "components.h"
#include "screen.h"
#include "track_menu.h"

#include <memory>
#include <string>

class PlayerScreen : public Screen {
public:
	struct Args {
		jf::Item item;
		bool transcode = false;
		double start = 0;
		int audio = -2;    // Jellyfin stream index; -2 = the user's default
		int subtitle = -2; // -1 = off; -2 = the user's default
		jf::Item next_item;      // explicit Up Next (e.g. next movie in a collection)
		bool has_next_item = false;
	};
	PlayerScreen(Fonts &fonts, Navigator &nav, const Args &args);
	~PlayerScreen() override;
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;
	bool suppress_screensaver() const override;

private:
	void start(bool transcode, double at);
	double position() const;
	double duration() const;
	void stop_reporting();
	void exit();
	// Tracks: applied once the stream is open (FFmpeg knows its streams).
	void apply_tracks();
	void apply_subtitle();
	int ffmpeg_track(int jellyfin_index, bool audio) const;
	void draw_subtitles(Renderer &r, float vx, float vy, float vw, float vh);
	// Next episode / segments
	const jf::Segment *current_segment() const;
	void skip_segment(const jf::Segment &s);
	void begin_countdown();
	void play_next();
	void draw_countdown(Renderer &r);
	// Chapters
	int chapter_at(double pos) const;
	void jump_chapter(int delta);
	// Playback speed
	void change_speed(int delta);
	// Trickplay (scrub thumbnails)
	void draw_trickplay(Renderer &r, double target, float bar_x, float bar_w, float bar_y);
	// Diagnostics overlay
	void draw_diagnostics(Renderer &r);

	Fonts &fonts_;
	Navigator &nav_;
	Args args_;
	jf::PlayPlan plan_;
	bool have_plan_ = false;
	bool transcode_ = false;
	std::unique_ptr<VideoPlayer> player_;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
	bool reported_start_ = false;
	bool ever_played_ = false;
	bool fell_back_ = false;
	double pending_seek_ = -1, seek_timer_ = 0;
	double report_timer_ = 0;
	double last_position_ = 0;
	float overlay_ = 1;
	double overlay_timer_ = 0;
	VideoPlayer::State last_state_ = VideoPlayer::IDLE;
	std::string error_;
	ui::Toast toast_;
	double stat_timer_ = 0;
	// Tracks
	int audio_ = -1, subtitle_ = -1; // Jellyfin stream indexes
	bool tracks_applied_ = false;
	std::vector<SubtitleCue> ext_cues_; // subtitles fetched as SRT
	int ext_generation_ = 0;
	bool ext_active_ = false, ext_loading_ = false;
	TrackMenu menu_;
	// Next episode, intro / credits skip
	std::vector<jf::Segment> segments_;
	jf::Item next_;
	bool has_next_ = false;
	double countdown_ = -1; // seconds left before the next episode (< 0: none)
	float skip_focus_ = 0;
	// Up Next item (episode, or next movie in a collection); has_next_ gates it.
	// Playback speed
	int speed_index_ = 2;   // index into SPEEDS (2 = 1.0x)
	float speed_hint_ = 0;  // shows the speed briefly after a change
	// Chapters
	float chapter_hint_ = 0;
	std::string chapter_label_;
	// Diagnostics overlay
	bool show_diag_ = false;
	// Trickplay tile currently loading (so the image cache keeps it warm)
	int trickplay_tile_ = -1;
};
