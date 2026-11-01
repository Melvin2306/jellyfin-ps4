// Home: a sidebar of sections (Search, Continue Watching, Next Up,
// Favorites, Movies, TV Shows, Collections, Genres, Sign out) and the
// selected section's poster grid. Movies / TV Shows can be sorted (Square)
// and jumped through by letter (L2 / R2).
#pragma once

#include "../core/jellyfin.h"
#include "components.h"
#include "poster_grid.h"
#include "screen.h"

#include <memory>
#include <string>
#include <vector>

class HomeScreen : public Screen {
public:
	HomeScreen(Fonts &fonts, Navigator &nav);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	enum Section { SEARCH, RESUME, NEXT_UP, FAVORITES, MOVIES, SHOWS, COLLECTIONS, GENRES, SETTINGS, SIGN_OUT, SECTIONS };
	static bool is_list(int s) { return s >= RESUME && s <= GENRES; }
	static bool sortable(int s) { return s == MOVIES || s == SHOWS; }
	void load(int section);

	Fonts &fonts_;
	Navigator &nav_;
	int section_ = MOVIES;
	bool in_grid_ = false;
	PosterGrid grids_[SECTIONS];
	bool loaded_[SECTIONS] = {}, loading_[SECTIONS] = {};
	std::string error_[SECTIONS];
	int sort_[SECTIONS] = {};
	float side_focus_[SECTIONS] = {};
	int seen_library_rev_ = 0;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};
