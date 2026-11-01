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
	DetailScreen(Fonts &fonts, Navigator &nav, const jf::Item &item, const jf::Item *up_next = nullptr);
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
	void play(bool transcode, double start);
	void draw_row(Renderer &r, float y, const char *title, int section);
	// Sections the user can move between with Up/Down (movies/episodes).
	enum Section { S_BUTTONS, S_CAST, S_SIMILAR, S_COUNT };
	bool has_section(int s) const;
	int next_section(int from, int dir) const;

	Fonts &fonts_;
	Navigator &nav_;
	jf::Item item_;
	jf::Item up_next_;
	bool has_up_next_ = false;
	bool series_ = false;
	std::vector<Button> buttons_;
	int button_ = 0;
	int audio_ = -2, subtitle_ = -2; // chosen tracks (-2 = not chosen yet)
	TrackMenu menu_;
	// Movie/episode page sections
	int section_ = S_BUTTONS;
	int cast_ = 0, similar_sel_ = 0;
	std::vector<float> cast_focus_, sim_focus_;
	float row_scroll_cast_ = 0, row_scroll_sim_ = 0, page_scroll_ = 0;
	std::vector<jf::Item> similar_;
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
