// Home: a sidebar (Continue Watching, Movies, TV Shows, Sign out) and a
// poster grid of the selected section.
#pragma once

#include "../core/jellyfin.h"
#include "components.h"
#include "screen.h"

#include <string>
#include <vector>

class HomeScreen : public Screen {
public:
	HomeScreen(Fonts &fonts, Navigator &nav);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	enum Section { RESUME, NEXT_UP, MOVIES, SHOWS, SIGN_OUT, SECTIONS };
	static const int LISTS = SIGN_OUT; // sections with an item grid
	void load(int section);
	void open(const jf::Item &item);

	Fonts &fonts_;
	Navigator &nav_;
	int section_ = MOVIES;
	bool in_grid_ = false;
	std::vector<jf::Item> items_[LISTS];
	bool loaded_[LISTS] = {}, loading_[LISTS] = {};
	std::string error_[LISTS];
	int selected_[LISTS] = {};
	float scroll_ = 0;
	float side_focus_[SECTIONS] = {};
	std::vector<float> card_focus_;
	int seen_library_rev_ = 0;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};
