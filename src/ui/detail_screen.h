// Item details. Movies / episodes: poster, info, overview and play buttons
// (Resume, Play from start, Play via server transcode). Series: the episode
// list; X on an episode opens its details.
#pragma once

#include "../core/jellyfin.h"
#include "components.h"
#include "screen.h"
#include "track_menu.h"

#include <memory>
#include <string>
#include <vector>

class DetailScreen : public Screen {
public:
	DetailScreen(Fonts &fonts, Navigator &nav, const jf::Item &item);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	struct Button {
		std::string label;
		bool transcode;
		double start;
		float focus = 0;
		int menu = -1; // >= 0: opens the track menu at this column instead of playing
	};
	void build_buttons();
	void reload();
	void toggle(bool favorite); // favorite or watched state

	Fonts &fonts_;
	Navigator &nav_;
	jf::Item item_;
	bool series_ = false;
	std::vector<Button> buttons_;
	int button_ = 0;
	int audio_ = -2, subtitle_ = -2; // chosen tracks (-2 = not chosen yet)
	TrackMenu menu_;
	// Series
	std::vector<jf::Item> episodes_;
	bool loading_ = false;
	std::string error_;
	int episode_ = 0;
	float scroll_ = 0;
	std::vector<float> ep_focus_;
	int seen_library_rev_ = 0;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};
