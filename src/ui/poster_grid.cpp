#include "poster_grid.h"

#include "../net/images.h"
#include "../platform/platform.h"
#include "theme.h"

#include <algorithm>
#include <cctype>

namespace {
const float CARD_W = 200, CARD_H = 300, GAP_X = 34, ROW_H = 400;
} // namespace

std::string index_letter(const jf::Item &item) {
	const std::string &n = item.sort_name.empty() ? item.name : item.sort_name;
	if (n.empty()) return "#";
	const unsigned char c = (unsigned char)n[0];
	if (isalpha(c)) return std::string(1, (char)toupper(c));
	return "#";
}

void PosterGrid::set_items(const std::vector<jf::Item> &items) {
	const std::string keep = current() ? current()->id : "";
	items_ = items;
	selected_ = 0;
	for (int i = 0; i < (int)items_.size(); i++)
		if (items_[i].id == keep) selected_ = i;
	focus_.assign(items_.size(), 0.0f);
}

const jf::Item *PosterGrid::current() const {
	return selected_ >= 0 && selected_ < (int)items_.size() ? &items_[selected_] : nullptr;
}

PosterGrid::Action PosterGrid::update(double dt, uint32_t pressed) {
	const int n = (int)items_.size();
	if ((int)focus_.size() != n) focus_.assign(n, 0.0f);
	for (int i = 0; i < n; i++) focus_[i] = ui::approach(focus_[i], (focused_ && i == selected_) ? 1.0f : 0.0f, dt);
	letter_time_ = std::max(0.0f, letter_time_ - (float)dt);
	scroll_ = ui::follow(scroll_, (selected_ / COLS) * ROW_H, ROW_H, view_h_, dt);
	if (!focused_ || n == 0) return NONE;
	const int col = selected_ % COLS;
	if (pressed & platform::BUTTON_LEFT) {
		if (col == 0) return LEAVE_LEFT;
		selected_--;
	}
	if ((pressed & platform::BUTTON_RIGHT) && selected_ + 1 < n && col + 1 < COLS) selected_++;
	if (pressed & platform::BUTTON_UP) {
		if (selected_ < COLS) return LEAVE_UP;
		selected_ -= COLS;
	}
	if (pressed & platform::BUTTON_DOWN) selected_ = std::min(n - 1, selected_ + COLS);
	if (pressed & (platform::BUTTON_L2 | platform::BUTTON_R2)) {
		const bool forward = (pressed & platform::BUTTON_R2) != 0;
		if (by_name_) {
			const std::string cur = index_letter(items_[selected_]);
			int i = selected_;
			if (forward) {
				while (i < n && index_letter(items_[i]) == cur) i++;
				if (i >= n) i = n - 1;
			} else {
				// Start of the previous letter (or of this one, if not at its start).
				while (i > 0 && index_letter(items_[i - 1]) == cur) i--;
				if (i == selected_ && i > 0) {
					const std::string prev = index_letter(items_[i - 1]);
					while (i > 0 && index_letter(items_[i - 1]) == prev) i--;
				}
			}
			selected_ = i;
			letter_ = index_letter(items_[selected_]);
		} else {
			selected_ = std::clamp(selected_ + (forward ? 3 : -3) * COLS, 0, n - 1);
			letter_ = std::to_string(selected_ + 1);
		}
		letter_time_ = 1.0f;
	}
	if (pressed & platform::BUTTON_ACCEPT) return OPEN;
	return NONE;
}

void PosterGrid::draw(Renderer &r, Fonts &fonts, float x0, float top, float clip_top, float bottom) {
	view_h_ = bottom - top;
	jf::Client &c = jf::client();
	for (int i = 0; i < (int)items_.size(); i++) {
		const int row = i / COLS, col = i % COLS;
		const float x = x0 + col * (CARD_W + GAP_X);
		const float y = top + row * ROW_H - scroll_;
		if (y + ROW_H < clip_top || y > bottom + 40) continue;
		const jf::Item &it = items_[i];
		const float f = i < (int)focus_.size() ? focus_[i] : 0;
		ui::begin_focus(r, x, y, CARD_W, CARD_H, theme::radius_field(), f, 1.08f);
		r.rounded_rect(x, y, CARD_W, CARD_H, theme::radius_field(), theme::BASE_200);
		const std::string img = c.image_url(it, 360);
		const Texture *t = img.empty() ? nullptr : &images::get(img);
		if (t && t->valid()) {
			r.image_cover(*t, x, y, CARD_W, CARD_H);
			r.round_corners(x, y, CARD_W, CARD_H, theme::radius_field(), theme::BASE_100);
		} else {
			// No artwork (genres, some collections): the name on the card.
			float ty = y + CARD_H / 2 - 20;
			for (const std::string &line : fonts.card_title.wrap(it.name, CARD_W - 24, 3)) {
				r.text(fonts.card_title, line, x + (CARD_W - fonts.card_title.measure(line)) / 2, ty, theme::BASE_CONTENT);
				ty += 32;
			}
		}
		if (it.runtime_ticks > 0 && it.position_ticks > 0) {
			ui::progress(r, x + 12, y + CARD_H - 18, CARD_W - 24, 6, (float)it.position_ticks / (float)it.runtime_ticks);
		}
		if (it.played) ui::status(r, x + CARD_W - 18, y + 18);
		if (it.favorite) ui::status(r, x + 18, y + 18, theme::SECONDARY);
		ui::end_focus(r, x, y, CARD_W, CARD_H, theme::radius_field(), f);
		if (it.type == "Genre") continue;
		const std::string title = it.type == "Episode" && !it.series_name.empty() ? it.series_name : it.name;
		r.text(fonts.small, fonts.small.ellipsize(title, CARD_W + 20), x, y + CARD_H + 34,
				f > 0.5f ? theme::BASE_CONTENT : theme::BASE_CONTENT_MUTED);
		std::string sub;
		if (it.type == "Episode") sub = it.display_name();
		else if (it.year > 0) sub = std::to_string(it.year);
		if (!sub.empty()) r.text(fonts.small, fonts.small.ellipsize(sub, CARD_W + 20), x, y + CARD_H + 62, theme::BASE_CONTENT_MUTED);
	}
	if (letter_time_ > 0 && !letter_.empty()) {
		const float a = std::min(1.0f, letter_time_ * 3);
		r.push(1180, 560, 1.0f, a);
		r.rounded_rect(1180 - 90, 560 - 90, 180, 180, theme::radius_box(), theme::BASE_300.with_alpha(0.95f));
		const float w = fonts.page_header.measure(letter_);
		r.text(fonts.page_header, letter_, 1180 - w / 2, 560 + 18, theme::BASE_CONTENT);
		r.pop();
	}
}
