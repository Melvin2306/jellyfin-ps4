#include "home_screen.h"

#include "../platform/platform.h"
#include "browse_screen.h"
#include "search_screen.h"
#include "settings_screen.h"
#include "theme.h"

#include <algorithm>

namespace {
const char *NAMES[] = { "Search", "Continue Watching", "Next Up", "Favorites", "Movies", "TV Shows", "Collections", "Genres",
	"Settings", "Switch user", "Sign out" };
const float SIDE_X = 70, SIDE_W = 330, SIDE_TOP = 190, SIDE_ROW = 70;
const float GRID_X = 470, GRID_TOP = 150, VIEW_TOP = 130, VIEW_BOTTOM = 1000;
} // namespace

HomeScreen::HomeScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), nav_(nav) {
	seen_library_rev_ = jf::client().library_rev;
	for (int i = 0; i < SECTIONS; i++) {
		if (is_list(i)) load(i);
		grids_[i].set_letter_jump(sortable(i) || i == COLLECTIONS || i == GENRES);
	}
}

void HomeScreen::load(int section) {
	if (!is_list(section) || loading_[section]) return;
	loading_[section] = true;
	std::weak_ptr<int> alive = alive_;
	auto done = [this, alive, section](bool ok, const std::string &error, const std::vector<jf::Item> &items) {
		if (alive.expired()) return;
		loading_[section] = false;
		loaded_[section] = true;
		error_[section] = ok ? "" : error;
		if (ok) grids_[section].set_items(items);
	};
	jf::Client &c = jf::client();
	const std::string sort = sorting::params(sort_[section]);
	switch (section) {
		case RESUME: c.resume(done); break;
		case NEXT_UP: c.next_up(done); break;
		case FAVORITES: c.favorites(done); break;
		case MOVIES: c.browse("IncludeItemTypes=Movie&Recursive=true&Limit=1000&" + sort, done); break;
		case SHOWS: c.browse("IncludeItemTypes=Series&Recursive=true&Limit=500&" + sort, done); break;
		case COLLECTIONS: c.collections(done); break;
		case GENRES: c.genres(done); break;
	}
}

void HomeScreen::update(double dt, uint32_t pressed) {
	// Back from the player or a change of favourites / watched state.
	if (jf::client().library_rev != seen_library_rev_) {
		seen_library_rev_ = jf::client().library_rev;
		for (int i = 0; i < SECTIONS; i++)
			if (is_list(i) && i != GENRES) load(i);
	}
	for (int i = 0; i < SECTIONS; i++) {
		side_focus_[i] = ui::approach(side_focus_[i], (!in_grid_ && i == section_) ? 1.0f : 0.0f, dt);
	}
	PosterGrid &grid = grids_[section_];
	grid.set_focused(in_grid_);
	for (int i = 0; i < SECTIONS; i++)
		if (i != section_) grids_[i].set_focused(false);

	if (!in_grid_) {
		grid.update(dt, 0);
		if (pressed & platform::BUTTON_UP) section_ = (section_ + SECTIONS - 1) % SECTIONS;
		if (pressed & platform::BUTTON_DOWN) section_ = (section_ + 1) % SECTIONS;
		if (pressed & (platform::BUTTON_RIGHT | platform::BUTTON_ACCEPT)) {
			if (section_ == SIGN_OUT) {
				if (pressed & platform::BUTTON_ACCEPT) jf::client().logout();
			} else if (section_ == SEARCH) {
				if (pressed & platform::BUTTON_ACCEPT) nav_.push(std::unique_ptr<Screen>(new SearchScreen(fonts_, nav_)));
			} else if (section_ == SETTINGS) {
				if (pressed & platform::BUTTON_ACCEPT) nav_.push(std::unique_ptr<Screen>(new SettingsScreen(fonts_, nav_)));
			} else if (section_ == SWITCH_USER) {
				if (pressed & platform::BUTTON_ACCEPT) jf::client().leave(); // the app shows the user picker
			} else if (!grid.items().empty()) {
				in_grid_ = true;
			} else if ((pressed & platform::BUTTON_ACCEPT) && !loading_[section_]) {
				load(section_); // retry
			}
		}
	} else {
		if (grid.items().empty()) {
			in_grid_ = false;
			return;
		}
		if (pressed & platform::BUTTON_BACK) {
			in_grid_ = false;
			return;
		}
		const PosterGrid::Action a = grid.update(dt, pressed);
		if (a == PosterGrid::LEAVE_LEFT) in_grid_ = false;
		if (a == PosterGrid::OPEN && grid.current()) open_item(fonts_, nav_, *grid.current());
	}
	if (pressed & platform::BUTTON_OPTIONS) {
		nav_.push(std::unique_ptr<Screen>(new SettingsScreen(fonts_, nav_)));
		return;
	}
	if (pressed & platform::BUTTON_TRIANGLE) load(section_);
	if ((pressed & platform::BUTTON_SQUARE) && sortable(section_)) {
		sort_[section_] = (sort_[section_] + 1) % sorting::COUNT;
		grids_[section_].set_letter_jump(sort_[section_] == 0);
		load(section_);
	}
}

void HomeScreen::draw(Renderer &r) {
	const bool list = is_list(section_);
	if (list) grids_[section_].draw(r, fonts_, GRID_X, GRID_TOP, VIEW_TOP, VIEW_BOTTOM);

	// Header above the grid.
	r.rect(GRID_X - 40, 0, 1920 - GRID_X + 40, VIEW_TOP - 5, theme::BASE_100);
	r.text(fonts_.section, NAMES[section_], GRID_X, 100, theme::BASE_CONTENT);
	if (list) {
		std::string right;
		if (sortable(section_)) right = std::string("Sort: ") + sorting::label(sort_[section_]);
		if (in_grid_ && !grids_[section_].items().empty()) {
			right += (right.empty() ? "" : "   ") + std::to_string(grids_[section_].selected() + 1) + " / " +
					std::to_string(grids_[section_].items().size());
		}
		r.text(fonts_.small, right, 1840 - fonts_.small.measure(right), 100, theme::BASE_CONTENT_MUTED);
	}
	if (list && grids_[section_].items().empty()) {
		static const char *empty[] = { "", "Nothing to continue yet.", "No next episodes. Start a series to see it here.",
			"No favorites yet. Add some from a title's details.", "No movies.", "No shows.", "No collections.", "No genres." };
		const std::string msg = loading_[section_] ? "Loading\xE2\x80\xA6"
				: !error_[section_].empty() ? "Couldn't load: " + error_[section_] + " (\xC3\x97 to retry)"
				: empty[section_];
		r.text(fonts_.body, msg, GRID_X, 220, error_[section_].empty() ? theme::BASE_CONTENT_MUTED : theme::ERROR);
	}
	if (section_ == SEARCH) {
		r.text(fonts_.body, "Press \xC3\x97 to search movies, shows and episodes.", GRID_X, 200, theme::BASE_CONTENT_MUTED);
	} else if (section_ == SETTINGS) {
		r.text(fonts_.body, "Press Ã (or OPTIONS anywhere on Home) for playback, subtitle and audio settings.", GRID_X, 200, theme::BASE_CONTENT_MUTED);
	} else if (section_ == SWITCH_USER) {
		r.text(fonts_.body, "Press Ã to pick another user. You stay signed in, so switching back needs no password.", GRID_X, 200, theme::BASE_CONTENT_MUTED);
	} else if (section_ == SIGN_OUT) {
		r.text(fonts_.body, "Press \xC3\x97 to sign out of " + jf::client().server(), GRID_X, 200, theme::BASE_CONTENT_MUTED);
		r.text(fonts_.small, "This removes this user's saved session from the PS4. The server address and username stay remembered.", GRID_X, 245, theme::BASE_CONTENT_MUTED);
	}

	// Sidebar.
	r.rect(0, 0, SIDE_X + SIDE_W + 30, 1080, theme::BASE_200);
	r.text(fonts_.page_header, "Jellyfin", SIDE_X, 110, theme::PRIMARY);
	r.text(fonts_.small, jf::client().user_name(), SIDE_X, 155, theme::BASE_CONTENT_MUTED);
	for (int i = 0; i < SECTIONS; i++) {
		const float y = SIDE_TOP + i * SIDE_ROW + (i >= SETTINGS ? 24 : 0);
		const float h = SIDE_ROW - 12;
		const bool cur = i == section_;
		ui::begin_focus(r, SIDE_X - 16, y, SIDE_W, h, theme::radius_field(), side_focus_[i], 1.03f);
		if (cur) r.rounded_rect(SIDE_X - 16, y, SIDE_W, h, theme::radius_field(), in_grid_ ? theme::BASE_300 : theme::PRIMARY);
		r.text(fonts_.card_title, NAMES[i], SIDE_X + 8, y + h / 2 + 9, cur ? theme::PRIMARY_CONTENT : theme::BASE_CONTENT_MUTED);
		ui::end_focus(r, SIDE_X - 16, y, SIDE_W, h, theme::radius_field(), 1.0f, theme::BASE_200);
	}
	r.rect(GRID_X - 40, 1005, 1920, 75, theme::BASE_100);
	std::vector<ui::Hint> hints = { { "\xC3\x97", "Select" }, { "\xE2\x97\x8B", "Back" }, { "\xE2\x96\xB3", "Refresh" } };
	if (sortable(section_)) hints.push_back({ "\xE2\x96\xA1", "Sort" });
	if (list && in_grid_) hints.push_back({ "L2 R2", sort_[section_] == 0 ? "A\xE2\x80\x93Z" : "Page" });
	ui::hints(r, fonts_.small, hints, GRID_X, 1050);
}
