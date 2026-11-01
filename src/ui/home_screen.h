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
	enum Section { RESUME, MOVIES, SHOWS, SIGN_OUT, SECTIONS };
	void load(int section);
	void open(const jf::Item &item);

	Fonts &fonts_;
	Navigator &nav_;
	int section_ = MOVIES;
	bool in_grid_ = false;
	std::vector<jf::Item> items_[3];
	bool loaded_[3] = {}, loading_[3] = {};
	std::string error_[3];
	int selected_[3] = {};
	float scroll_ = 0;
	float side_focus_[SECTIONS] = {};
	std::vector<float> card_focus_;
	int seen_library_rev_ = 0;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};
