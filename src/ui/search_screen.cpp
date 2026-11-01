#include "search_screen.h"

#include "../core/jellyfin.h"
#include "../platform/ime.h"
#include "../platform/platform.h"
#include "browse_screen.h"
#include "theme.h"

namespace {
const float LEFT = 120, FIELD_Y = 130, FIELD_W = 1100, FIELD_H = 84;
} // namespace

SearchScreen::SearchScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), nav_(nav) {
	grid_.set_letter_jump(false);
	edit(); // straight to the keyboard
}

void SearchScreen::edit() {
	editing_ = true;
	fallback_kb_ = !ime::open("Search", query_, ime::TEXT, 100);
	if (fallback_kb_) platform::log("search: system keyboard failed to open; using the built-in keyboard");
}

void SearchScreen::run() {
	searched_ = query_;
	if (query_.empty()) {
		grid_.set_items({});
		return;
	}
	loading_ = true;
	error_.clear();
	const int gen = ++generation_;
	std::weak_ptr<int> alive = alive_;
	jf::client().search(query_, [this, alive, gen](bool ok, const std::string &error, const std::vector<jf::Item> &items) {
		if (alive.expired() || gen != generation_) return;
		loading_ = false;
		error_ = ok ? "" : error;
		grid_.set_items(items);
		if (!items.empty()) {
			in_grid_ = true;
			grid_.set_focused(true);
		}
	});
}

void SearchScreen::update(double dt, uint32_t pressed) {
	field_focus_ = ui::approach(field_focus_, (!in_grid_ && !editing_) ? 1.0f : 0.0f, dt);
	if (editing_) {
		if (!fallback_kb_) {
			std::string text;
			const ime::State st = ime::poll(text);
			if (st == ime::RUNNING) return;
			cooldown_ = 0.4;
			editing_ = false;
			if (st == ime::FAILED) {
				fallback_kb_ = true;
				editing_ = true;
				return;
			}
			if (st == ime::OK) {
				query_ = text;
				run();
			} else if (searched_.empty()) {
				nav_.pop(); // cancelled before searching anything
			}
		} else if (keyboard_.update(dt, pressed, query_, 100) == Keyboard::DONE) {
			editing_ = false;
			run();
		}
		return;
	}
	if (cooldown_ > 0) {
		cooldown_ -= dt;
		return;
	}
	if (pressed & platform::BUTTON_BACK) {
		if (in_grid_) {
			in_grid_ = false;
			grid_.set_focused(false);
		} else {
			nav_.pop();
		}
		return;
	}
	if (!in_grid_) {
		if (pressed & platform::BUTTON_ACCEPT) edit();
		if ((pressed & platform::BUTTON_DOWN) && !grid_.items().empty()) {
			in_grid_ = true;
			grid_.set_focused(true);
		}
		grid_.update(dt, 0);
		return;
	}
	const PosterGrid::Action a = grid_.update(dt, pressed);
	if (a == PosterGrid::LEAVE_UP) {
		in_grid_ = false;
		grid_.set_focused(false);
	} else if (a == PosterGrid::OPEN && grid_.current()) {
		open_item(fonts_, nav_, *grid_.current());
	}
}

void SearchScreen::draw(Renderer &r) {
	grid_.draw(r, fonts_, LEFT, 290, 250, 1000);
	r.rect(0, 0, 1920, 250, theme::BASE_100);
	r.text(fonts_.section, "Search", LEFT, 90, theme::BASE_CONTENT);
	ui::begin_focus(r, LEFT, FIELD_Y, FIELD_W, FIELD_H, theme::radius_field(), field_focus_, 1.02f);
	r.rounded_rect(LEFT, FIELD_Y, FIELD_W, FIELD_H, theme::radius_field(), theme::BASE_200);
	if (query_.empty()) r.text(fonts_.body, "Movies, shows, episodes\xE2\x80\xA6", LEFT + 28, FIELD_Y + 50, rgb(0x6A6A70));
	else r.text(fonts_.body, fonts_.body.ellipsize(query_, FIELD_W - 56), LEFT + 28, FIELD_Y + 50, theme::BASE_CONTENT);
	ui::end_focus(r, LEFT, FIELD_Y, FIELD_W, FIELD_H, theme::radius_field(), field_focus_);
	if (!searched_.empty()) {
		std::string info = loading_ ? "Searching\xE2\x80\xA6"
				: !error_.empty() ? "Search failed: " + error_
				: std::to_string(grid_.items().size()) + " results for \"" + searched_ + "\"";
		r.text(fonts_.small, info, LEFT + FIELD_W + 40, FIELD_Y + 50, error_.empty() ? theme::BASE_CONTENT_MUTED : theme::ERROR);
	}
	r.rect(0, 1005, 1920, 75, theme::BASE_100);
	if (editing_ && fallback_kb_) {
		keyboard_.draw(r, fonts_, 1920 - keyboard_.width() - 110, 330);
	} else {
		ui::hints(r, fonts_.small, { { "\xC3\x97", in_grid_ ? "Open" : "Type" }, { "\xE2\x97\x8B", "Back" } }, LEFT, 1050);
	}
}
