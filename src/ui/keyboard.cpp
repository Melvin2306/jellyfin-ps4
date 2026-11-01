#include "keyboard.h"

#include "../platform/platform.h"
#include "components.h"
#include "theme.h"

#include <algorithm>
#include <cctype>

namespace {
const int COLS = 11;
const float KEY = 76, GAP = 8;
} // namespace

Keyboard::Keyboard() {
	const char *rows[] = { "1234567890-", "qwertyuiop/", "asdfghjkl:.", "zxcvbnm_@!?", "#$%&*+=()~," };
	for (int r = 0; r < 5; r++) {
		const std::string chars = rows[r];
		for (int c = 0; c < (int)chars.size(); c++) {
			const std::string t(1, chars[c]);
			keys_.push_back({ t, t, c, r, 1 });
		}
	}
	keys_.push_back({ "Shift", "", 0, 5, 2 });
	keys_.push_back({ "Space", " ", 2, 5, 4 });
	keys_.push_back({ "Delete", "", 6, 5, 2 });
	keys_.push_back({ "Done", "", 8, 5, 3 });
}

float Keyboard::width() const {
	return COLS * KEY + (COLS - 1) * GAP;
}

float Keyboard::height() const {
	return 6 * KEY + 5 * GAP + 50;
}

int Keyboard::key_at(int col, int row) const {
	for (int i = 0; i < (int)keys_.size(); i++) {
		const Key &k = keys_[i];
		if (k.row == row && col >= k.col && col < k.col + k.span) return i;
	}
	return -1;
}

std::string Keyboard::label_of(const Key &k) const {
	if (upper_ && k.text.size() == 1 && isalpha((unsigned char)k.text[0])) {
		return std::string(1, (char)toupper((unsigned char)k.text[0]));
	}
	return k.label;
}

Keyboard::Result Keyboard::update(double dt, uint32_t pressed, std::string &text, size_t max_len) {
	for (int i = 0; i < (int)keys_.size(); i++) {
		keys_[i].focus = ui::approach(keys_[i].focus, i == key_ ? 1.0f : 0.0f, dt);
	}
	const Key &k = keys_[key_];
	int dx = 0, dy = 0;
	if (pressed & platform::BUTTON_LEFT) dx = -1;
	if (pressed & platform::BUTTON_RIGHT) dx = 1;
	if (pressed & platform::BUTTON_UP) dy = -1;
	if (pressed & platform::BUTTON_DOWN) dy = 1;
	if (dx != 0) {
		int col = dx > 0 ? k.col + k.span : k.col - 1;
		if (col >= COLS) col = 0;
		if (col < 0) col = COLS - 1;
		const int i = key_at(col, k.row);
		if (i >= 0) key_ = i;
	} else if (dy != 0) {
		int row = (k.row + dy + 6) % 6;
		const int i = key_at(k.col, row);
		if (i >= 0) key_ = i;
	}
	if (pressed & platform::BUTTON_SQUARE) {
		if (!text.empty()) text.pop_back();
	}
	if (pressed & platform::BUTTON_TRIANGLE) upper_ = !upper_;
	if (pressed & (platform::BUTTON_OPTIONS | platform::BUTTON_BACK)) return DONE;
	if (pressed & platform::BUTTON_ACCEPT) {
		const Key &cur = keys_[key_];
		if (cur.label == "Shift") {
			upper_ = !upper_;
		} else if (cur.label == "Delete") {
			if (!text.empty()) text.pop_back();
		} else if (cur.label == "Done") {
			return DONE;
		} else if (text.size() < max_len) {
			text += cur.text == " " ? std::string(" ") : label_of(cur);
		}
	}
	return NONE;
}

void Keyboard::draw(Renderer &r, Fonts &fonts, float x, float y) {
	r.rounded_rect(x - 24, y - 24, width() + 48, height() + 48, theme::radius_box(), theme::BASE_200);
	for (const Key &k : keys_) {
		const float kx = x + k.col * (KEY + GAP);
		const float ky = y + k.row * (KEY + GAP);
		const float kw = k.span * KEY + (k.span - 1) * GAP;
		const bool on = k.label == "Shift" && upper_;
		ui::begin_focus(r, kx, ky, kw, KEY, theme::radius_field(), k.focus, 1.06f);
		r.rounded_rect(kx, ky, kw, KEY, theme::radius_field(), on ? theme::SECONDARY : theme::BASE_300);
		const std::string label = label_of(k);
		Font &f = label.size() > 1 ? fonts.small : fonts.card_title;
		const float tw = f.measure(label);
		r.text(f, label, kx + (kw - tw) / 2, ky + KEY / 2 + f.size() * 0.35f, theme::BASE_CONTENT);
		ui::end_focus(r, kx, ky, kw, KEY, theme::radius_field(), k.focus, theme::BASE_200);
	}
	ui::hints(r, fonts.small,
			{ { "\xC3\x97", "Type" }, { "\xE2\x96\xA1", "Delete" }, { "\xE2\x96\xB3", "Shift" }, { "\xE2\x97\x8B", "Done" } }, x,
			y + 6 * (KEY + GAP) + 30);
}
