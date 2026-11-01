#include "filter_panel.h"

#include "../platform/platform.h"
#include "theme.h"

#include <cstdio>

namespace {
const float PANEL_X = 560, PANEL_W = 800, TOP = 300, ROW_H = 96, ROW_GAP = 14;
const int ROWS = 4;
} // namespace

std::string Filters::query() const {
	std::string q;
	if (unwatched) q += "&Filters=IsUnplayed";
	if (min_rating > 0) q += "&minCommunityRating=" + std::to_string(min_rating);
	if (year_from > 0) {
		char buf[48];
		snprintf(buf, sizeof(buf), "&minPremiereDate=%04d-01-01T00:00:00.000Z", year_from);
		q += buf;
	}
	if (year_to > 0) {
		char buf[48];
		snprintf(buf, sizeof(buf), "&maxPremiereDate=%04d-12-31T23:59:59.000Z", year_to);
		q += buf;
	}
	return q;
}

std::string Filters::summary() const {
	std::string s;
	if (year_from || year_to) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%s-%s", year_from ? std::to_string(year_from).c_str() : "any",
				year_to ? std::to_string(year_to).c_str() : "any");
		s += buf;
	}
	if (min_rating > 0) s += (s.empty() ? "" : " \xC2\xB7 ") + std::to_string(min_rating) + "+";
	if (unwatched) s += (s.empty() ? "" : " \xC2\xB7 ") + std::string("Unwatched");
	return s;
}

bool FilterPanel::update(double dt, uint32_t pressed) {
	for (int i = 0; i < ROWS; i++) focus_[i] = ui::approach(focus_[i], i == row_ ? 1.0f : 0.0f, dt);
	if (pressed & (platform::BUTTON_BACK | platform::BUTTON_TRIANGLE)) {
		open_ = false;
		return false;
	}
	if (pressed & platform::BUTTON_UP) row_ = (row_ + ROWS - 1) % ROWS;
	if (pressed & platform::BUTTON_DOWN) row_ = (row_ + 1) % ROWS;
	int d = 0;
	if (pressed & platform::BUTTON_LEFT) d = -1;
	if (pressed & platform::BUTTON_RIGHT) d = 1;
	if ((pressed & platform::BUTTON_ACCEPT) && row_ == 3) d = 1; // X toggles unwatched
	if (d == 0) return false;
	switch (row_) {
		case 0: // year from
			filters_.year_from = filters_.year_from == 0 ? (d > 0 ? 1950 : 2025) : filters_.year_from + d;
			if (filters_.year_from < 1900 || filters_.year_from > 2030) filters_.year_from = 0;
			break;
		case 1: // year to
			filters_.year_to = filters_.year_to == 0 ? (d > 0 ? 1950 : 2025) : filters_.year_to + d;
			if (filters_.year_to < 1900 || filters_.year_to > 2030) filters_.year_to = 0;
			break;
		case 2: // min rating
			filters_.min_rating += d;
			if (filters_.min_rating < 0) filters_.min_rating = 0;
			if (filters_.min_rating > 9) filters_.min_rating = 9;
			break;
		case 3: filters_.unwatched = !filters_.unwatched; break;
	}
	return true;
}

void FilterPanel::draw(Renderer &r, Fonts &fonts) {
	if (!open_) return;
	r.rect(0, 0, 1920, 1080, rgb(0x000000, 0.5f));
	const float h = 70 + ROWS * (ROW_H + ROW_GAP) + 80;
	r.rounded_rect(PANEL_X, TOP - 80, PANEL_W, h, theme::radius_box(), theme::BASE_200);
	r.text(fonts.section, "Filters", PANEL_X + 40, TOP - 24, theme::BASE_CONTENT);
	auto year = [](int y) { return y ? std::to_string(y) : std::string("Any"); };
	std::string values[ROWS] = { year(filters_.year_from), year(filters_.year_to),
		filters_.min_rating ? std::to_string(filters_.min_rating) + "+" : "Any", filters_.unwatched ? "On" : "Off" };
	const char *labels[ROWS] = { "Year from", "Year to", "Minimum rating", "Unwatched only" };
	for (int i = 0; i < ROWS; i++) {
		const float y = TOP + i * (ROW_H + ROW_GAP);
		ui::begin_focus(r, PANEL_X + 30, y, PANEL_W - 60, ROW_H, theme::radius_field(), focus_[i], 1.01f);
		r.rounded_rect(PANEL_X + 30, y, PANEL_W - 60, ROW_H, theme::radius_field(), i == row_ ? theme::BASE_300 : theme::BASE_100);
		r.text(fonts.card_title, labels[i], PANEL_X + 60, y + ROW_H / 2 + 9, theme::BASE_CONTENT);
		const std::string v = "\xE2\x97\x80  " + values[i] + "  \xE2\x96\xB6";
		r.text(fonts.card_title, v, PANEL_X + PANEL_W - 60 - fonts.card_title.measure(v), y + ROW_H / 2 + 9,
				i == row_ ? theme::PRIMARY : theme::BASE_CONTENT_MUTED);
		ui::end_focus(r, PANEL_X + 30, y, PANEL_W - 60, ROW_H, theme::radius_field(), focus_[i]);
	}
	ui::hints(r, fonts.small, { { "\xE2\x97\x80 \xE2\x96\xB6", "Change" }, { "\xE2\x96\xB3", "Close" } }, PANEL_X + 40,
			TOP + ROWS * (ROW_H + ROW_GAP) + 40);
}
