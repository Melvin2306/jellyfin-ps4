#include "track_menu.h"

#include "../core/tracks.h"
#include "../platform/platform.h"
#include "theme.h"

#include <algorithm>

namespace {
const float PANEL_X = 760, PANEL_W = 1160;
const float COL_W = 520, COL_GAP = 40, TOP = 230, ROW_H = 78, ROW_GAP = 10, BOTTOM = 990;
} // namespace

void TrackMenu::open(const jf::Item &item, bool transcode, int audio, int subtitle, int column) {
	item_ = item;
	transcode_ = transcode;
	audio_ = audio;
	subtitle_ = subtitle;
	column_ = column;
	open_ = true;
	for (int c = 0; c < 2; c++) {
		rows_[c].clear();
		scroll_[c] = 0;
	}
	for (const jf::Stream &s : item.audio_streams()) {
		std::string detail = s.is_default ? "Default" : "";
		rows_[0].push_back({ s.index, s.title, detail, true });
	}
	rows_[1].push_back({ -1, "Off", "", true });
	for (const jf::Stream &s : item.subtitle_streams()) {
		const bool ok = tracks::subtitle_supported(s, transcode);
		std::string detail = s.is_external ? "External" : "Embedded";
		if (s.is_forced) detail += " \xC2\xB7 Forced";
		if (!ok) detail += transcode ? " \xC2\xB7 image subtitles need direct play" : " \xC2\xB7 not supported";
		rows_[1].push_back({ s.index, s.title, detail, ok });
	}
	for (int c = 0; c < 2; c++) {
		const int want = c == 0 ? audio_ : subtitle_;
		selected_[c] = 0;
		for (int i = 0; i < (int)rows_[c].size(); i++)
			if (rows_[c][i].index == want) selected_[c] = i;
	}
	if (rows_[0].empty()) column_ = 1;
}

TrackMenu::Result TrackMenu::update(double dt, uint32_t pressed) {
	if (!open_) return NONE;
	if (pressed & (platform::BUTTON_BACK | platform::BUTTON_TRIANGLE)) {
		open_ = false;
		return CLOSED;
	}
	if ((pressed & platform::BUTTON_LEFT) && !rows_[0].empty()) column_ = 0;
	if (pressed & platform::BUTTON_RIGHT) column_ = 1;
	std::vector<Row> &rows = rows_[column_];
	int &sel = selected_[column_];
	if (rows.empty()) return NONE;
	if (pressed & platform::BUTTON_UP) sel = std::max(0, sel - 1);
	if (pressed & platform::BUTTON_DOWN) sel = std::min((int)rows.size() - 1, sel + 1);
	const float view = BOTTOM - TOP;
	scroll_[column_] = ui::follow(scroll_[column_], sel * (ROW_H + ROW_GAP), ROW_H, view, dt);
	if (pressed & platform::BUTTON_ACCEPT) {
		const Row &row = rows[sel];
		if (!row.supported) return NONE;
		if (column_ == 0) {
			if (row.index == audio_) return NONE;
			audio_ = row.index;
			tracks::remember_audio(item_, audio_);
			platform::log("tracks: audio -> %d", audio_);
			return AUDIO_CHANGED;
		}
		if (row.index == subtitle_) return NONE;
		subtitle_ = row.index;
		tracks::remember_subtitle(item_, subtitle_);
		platform::log("tracks: subtitle -> %d", subtitle_);
		return SUBTITLE_CHANGED;
	}
	return NONE;
}

void TrackMenu::draw(Renderer &r, Fonts &fonts) {
	if (!open_) return;
	r.rect(PANEL_X, 0, PANEL_W, 1080, theme::BASE_100.with_alpha(0.95f));
	r.text(fonts.page_header, "Audio & subtitles", PANEL_X + 40, 130, theme::BASE_CONTENT);
	const char *titles[] = { "Audio", "Subtitles" };
	for (int c = 0; c < 2; c++) {
		const float x = PANEL_X + 40 + c * (COL_W + COL_GAP);
		const bool focused = c == column_;
		r.text(fonts.section, titles[c], x, TOP - 24, focused ? theme::BASE_CONTENT : theme::BASE_CONTENT_MUTED);
		const int active = c == 0 ? audio_ : subtitle_;
		if (rows_[c].empty()) {
			r.text(fonts.body, "None", x, TOP + 40, theme::BASE_CONTENT_MUTED);
			continue;
		}
		for (int i = 0; i < (int)rows_[c].size(); i++) {
			const Row &row = rows_[c][i];
			const float y = TOP + i * (ROW_H + ROW_GAP) - scroll_[c];
			if (y < TOP - ROW_H || y > BOTTOM) continue;
			const bool sel = focused && i == selected_[c];
			r.rounded_rect(x, y, COL_W, ROW_H, theme::radius_field(), sel ? theme::BASE_300 : theme::BASE_200);
			if (sel) r.rect(x, y + 10, 5, ROW_H - 20, theme::PRIMARY);
			if (row.index == active) ui::status(r, x + 26, y + ROW_H / 2);
			const Color fg = row.supported ? theme::BASE_CONTENT : theme::BASE_CONTENT_MUTED;
			const float tx = x + 48, tw = COL_W - 64;
			r.text(fonts.card_title, fonts.card_title.ellipsize(row.label, tw), tx, row.detail.empty() ? y + ROW_H / 2 + 9 : y + 34, fg);
			if (!row.detail.empty()) r.text(fonts.small, fonts.small.ellipsize(row.detail, tw), tx, y + 62, theme::BASE_CONTENT_MUTED);
		}
	}
	ui::hints(r, fonts.small, { { "\xC3\x97", "Select" }, { "\xE2\x97\x80 \xE2\x96\xB6", "Column" }, { "\xE2\x97\x8B", "Close" } },
			PANEL_X + 40, 1040);
}
