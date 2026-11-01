// Full-screen player. X play/pause, Left/Right seek 10 s, L1/R1 seek 60 s
// (presses add up), Circle stops. Reports start / progress / stop to
// Jellyfin. If direct play fails before the first frame, it retries once
// through a server transcode (H.264/AAC).
#pragma once

#include "../core/jellyfin.h"
#include "../video/video_player.h"
#include "components.h"
#include "screen.h"

#include <memory>
#include <string>

class PlayerScreen : public Screen {
public:
	struct Args {
		jf::Item item;
		bool transcode = false;
		double start = 0;
	};
	PlayerScreen(Fonts &fonts, Navigator &nav, const Args &args);
	~PlayerScreen() override;
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	void start(bool transcode, double at);
	double position() const;
	double duration() const;
	void stop_reporting();
	void exit();

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
};
