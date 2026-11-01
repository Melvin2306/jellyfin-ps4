#include "home_screen.h"

#include "../net/images.h"
#include "../platform/platform.h"
#include "detail_screen.h"
#include "theme.h"

#include <algorithm>

namespace {
const char *NAMES[] = { "Continue Watching", "Movies", "TV Shows", "Sign out" };
const float SIDE_X = 70, SIDE_W = 330, SIDE_TOP = 230, SIDE_ROW = 84;
const float GRID_X = 470, GRID_TOP = 150, CARD_W = 200, CARD_H = 300, GAP_X = 34, ROW_H = 400;
const int COLS = 6;
const float VIEW_TOP = 130, VIEW_BOTTOM = 1000;
} // namespace

HomeScreen::HomeScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), nav_(nav) {
	seen_library_rev_ = jf::client().library_rev;
	load(RESUME);
	load(MOVIES);
	load(SHOWS);
}

void HomeScreen::load(int section) {
	if (section < 0 || section > SHOWS || loading_[section]) return;
	loading_[section] = true;
	std::weak_ptr<int> alive = alive_;
	auto done = [this, alive, section](bool ok, const std::string &error, const std::vector<jf::Item> &items) {
		if (alive.expired()) return;
		loading_[section] = false;
		loaded_[section] = true;
		error_[section] = ok ? "" : error;
		if (ok) {
			items_[section] = items;
			selected_[section] = std::min(selected_[section], std::max(0, (int)items.size() - 1));
		}
	};
	jf::Client &c = jf::client();
	if (section == RESUME) c.resume(done);
	else if (section == MOVIES) c.movies(done);
	else c.series(done);
}

void HomeScreen::open(const jf::Item &item) {
	platform::log("home: open %s '%s'", item.type.c_str(), item.name.c_str());
	nav_.push(std::unique_ptr<Screen>(new DetailScreen(fonts_, nav_, item)));
}

void HomeScreen::update(double dt, uint32_t pressed) {
	// Back from the player: positions changed, refresh Continue Watching.
	if (jf::client().library_rev != seen_library_rev_) {
		seen_library_rev_ = jf::client().library_rev;
		load(RESUME);
		load(MOVIES);
		load(SHOWS);
	}
	for (int i = 0; i < SECTIONS; i++) {
		side_focus_[i] = ui::approach(side_focus_[i], (!in_grid_ && i == section_) ? 1.0f : 0.0f, dt);
	}
	const bool grid_section = section_ <= SHOWS;
	std::vector<jf::Item> *items = grid_section ? &items_[section_] : nullptr;
	const int n = items ? (int)items->size() : 0;
	if ((int)card_focus_.size() != n) card_focus_.assign(n, 0.0f);
	int &sel = grid_section ? selected_[section_] : selected_[0];
	for (int i = 0; i < n; i++) card_focus_[i] = ui::approach(card_focus_[i], (in_grid_ && i == sel) ? 1.0f : 0.0f, dt);

	if (!in_grid_) {
		if (pressed & platform::BUTTON_UP) section_ = (section_ + SECTIONS - 1) % SECTIONS;
		if (pressed & platform::BUTTON_DOWN) section_ = (section_ + 1) % SECTIONS;
		if (pressed & (platform::BUTTON_UP | platform::BUTTON_DOWN)) scroll_ = 0;
		if (pressed & (platform::BUTTON_RIGHT | platform::BUTTON_ACCEPT)) {
			if (section_ == SIGN_OUT) {
				if (pressed & platform::BUTTON_ACCEPT) jf::client().logout();
			} else if (n > 0) {
				in_grid_ = true;
			} else if ((pressed & platform::BUTTON_ACCEPT) && !loading_[section_]) {
				load(section_); // retry
			}
		}
		if (pressed & platform::BUTTON_TRIANGLE) load(section_);
	} else {
		if (n == 0) {
			in_grid_ = false;
			return;
		}
		const int col = sel % COLS;
		if (pressed & platform::BUTTON_LEFT) {
			if (col == 0) in_grid_ = false;
			else sel--;
		}
		if ((pressed & platform::BUTTON_RIGHT) && sel + 1 < n && col + 1 < COLS) sel++;
		if ((pressed & platform::BUTTON_UP) && sel >= COLS) sel -= COLS;
		if ((pressed & platform::BUTTON_DOWN)) sel = std::min(n - 1, sel + COLS);
		if (pressed & platform::BUTTON_BACK) in_grid_ = false;
		if (pressed & platform::BUTTON_ACCEPT) open((*items)[sel]);
		if (pressed & platform::BUTTON_TRIANGLE) load(section_);
	}
	if (grid_section) {
		const int row = sel / COLS;
		scroll_ = ui::follow(scroll_, row * ROW_H, ROW_H, VIEW_BOTTOM - GRID_TOP, dt);
	}
}

void HomeScreen::draw(Renderer &r) {
	// Sidebar.
	r.rect(0, 0, SIDE_X + SIDE_W + 30, 1080, theme::BASE_200);
	r.text(fonts_.page_header, "Jellyfin", SIDE_X, 120, theme::PRIMARY);
	r.text(fonts_.small, jf::client().user_name(), SIDE_X, 165, theme::BASE_CONTENT_MUTED);
	for (int i = 0; i < SECTIONS; i++) {
		const float y = SIDE_TOP + i * SIDE_ROW + (i == SIGN_OUT ? 40 : 0);
		const bool cur = i == section_;
		ui::begin_focus(r, SIDE_X - 16, y, SIDE_W, SIDE_ROW - 14, theme::radius_field(), side_focus_[i], 1.03f);
		if (cur) {
			r.rounded_rect(SIDE_X - 16, y, SIDE_W, SIDE_ROW - 14, theme::radius_field(),
					in_grid_ ? theme::BASE_300 : theme::PRIMARY);
		}
		r.text(fonts_.card_title, NAMES[i], SIDE_X + 8, y + (SIDE_ROW - 14) / 2 + 9,
				cur ? theme::PRIMARY_CONTENT : theme::BASE_CONTENT_MUTED);
		ui::end_focus(r, SIDE_X - 16, y, SIDE_W, SIDE_ROW - 14, theme::radius_field(), 1.0f, theme::BASE_200);
	}
	ui::hints(r, fonts_.small, { { "\xC3\x97", "Select" }, { "\xE2\x97\x8B", "Back" }, { "\xE2\x96\xB3", "Refresh" } }, SIDE_X,
			1030);

	if (section_ == SIGN_OUT) {
		r.text(fonts_.section, "Sign out", GRID_X, 120, theme::BASE_CONTENT);
		r.text(fonts_.body, "Press \xC3\x97 to sign out of " + jf::client().server(), GRID_X, 200, theme::BASE_CONTENT_MUTED);
		return;
	}
	const std::vector<jf::Item> &items = items_[section_];
	const int sel = selected_[section_];
	char header[96];
	snprintf(header, sizeof(header), "%s", NAMES[section_]);
	r.text(fonts_.section, header, GRID_X, 100, theme::BASE_CONTENT);
	if (!items.empty() && in_grid_) {
		const std::string pos = std::to_string(sel + 1) + " / " + std::to_string(items.size());
		r.text(fonts_.small, pos, 1840 - fonts_.small.measure(pos), 100, theme::BASE_CONTENT_MUTED);
	}
	if (items.empty()) {
		std::string msg = loading_[section_] ? "Loading\xE2\x80\xA6"
				: !error_[section_].empty() ? "Couldn't load: " + error_[section_] + " (\xC3\x97 to retry)"
				: section_ == RESUME ? "Nothing to continue yet." : "Nothing here.";
		r.text(fonts_.body, msg, GRID_X, 220, error_[section_].empty() ? theme::BASE_CONTENT_MUTED : theme::ERROR);
		return;
	}
	jf::Client &c = jf::client();
	for (int i = 0; i < (int)items.size(); i++) {
		const int row = i / COLS, col = i % COLS;
		const float x = GRID_X + col * (CARD_W + GAP_X);
		const float y = GRID_TOP + row * ROW_H - scroll_;
		if (y + ROW_H < VIEW_TOP || y > VIEW_BOTTOM + 40) continue;
		const jf::Item &it = items[i];
		const float f = i < (int)card_focus_.size() ? card_focus_[i] : 0;
		ui::begin_focus(r, x, y, CARD_W, CARD_H, theme::radius_field(), f, 1.08f);
		r.rounded_rect(x, y, CARD_W, CARD_H, theme::radius_field(), theme::BASE_200);
		const std::string img = c.image_url(it, 360);
		if (!img.empty()) {
			const Texture &t = images::get(img);
			if (t.valid()) {
				r.image_cover(t, x, y, CARD_W, CARD_H);
				r.round_corners(x, y, CARD_W, CARD_H, theme::radius_field(), theme::BASE_100);
			}
		} else {
			r.text(fonts_.small, fonts_.small.ellipsize(it.name, CARD_W - 20), x + 10, y + CARD_H / 2, theme::BASE_CONTENT);
		}
		if (it.runtime_ticks > 0 && it.position_ticks > 0) {
			ui::progress(r, x + 12, y + CARD_H - 18, CARD_W - 24, 6, (float)it.position_ticks / (float)it.runtime_ticks);
		}
		if (it.played) ui::status(r, x + CARD_W - 18, y + 18);
		ui::end_focus(r, x, y, CARD_W, CARD_H, theme::radius_field(), f);
		const std::string title = section_ == RESUME && it.type == "Episode" ? it.series_name : it.name;
		r.text(fonts_.small, fonts_.small.ellipsize(title, CARD_W + 20), x, y + CARD_H + 34,
				f > 0.5f ? theme::BASE_CONTENT : theme::BASE_CONTENT_MUTED);
		if (it.year > 0 || it.type == "Episode") {
			const std::string sub = it.type == "Episode" ? it.display_name() : std::to_string(it.year);
			r.text(fonts_.small, fonts_.small.ellipsize(sub, CARD_W + 20), x, y + CARD_H + 62, theme::BASE_CONTENT_MUTED);
		}
	}
	// Keep the header readable above scrolled cards.
	r.rect(GRID_X - 40, 0, 1920 - GRID_X + 40, VIEW_TOP - 5, theme::BASE_100);
	r.text(fonts_.section, header, GRID_X, 100, theme::BASE_CONTENT);
	if (in_grid_) {
		const std::string pos = std::to_string(sel + 1) + " / " + std::to_string(items.size());
		r.text(fonts_.small, pos, 1840 - fonts_.small.measure(pos), 100, theme::BASE_CONTENT_MUTED);
	}
}
