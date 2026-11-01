#include "settings_screen.h"

#include "../core/jellyfin.h"
#include "../core/settings.h"
#include "../net/update.h"
#include "../platform/platform.h"
#include "components.h"
#include "theme.h"

#include <algorithm>
#include <cstdlib>

namespace {
const float LEFT = 160, TOP = 190, ROW_H = 96, ROW_GAP = 12, WIDTH = 1600, VIEW_BOTTOM = 990;

std::vector<std::pair<std::string, std::string>> languages(bool subtitles) {
	std::vector<std::pair<std::string, std::string>> l;
	l.push_back({ subtitles ? "File default" : "File default", "" });
	if (subtitles) l.push_back({ "Off", "off" });
	const char *langs[][2] = { { "English", "eng" }, { "German", "ger" }, { "French", "fre" }, { "Spanish", "spa" },
		{ "Italian", "ita" }, { "Dutch", "dut" }, { "Portuguese", "por" }, { "Polish", "pol" }, { "Russian", "rus" },
		{ "Turkish", "tur" }, { "Swedish", "swe" }, { "Danish", "dan" }, { "Norwegian", "nor" }, { "Finnish", "fin" },
		{ "Japanese", "jpn" }, { "Korean", "kor" }, { "Chinese", "chi" }, { "Arabic", "ara" }, { "Hindi", "hin" } };
	for (auto &p : langs) l.push_back({ p[0], p[1] });
	return l;
}
} // namespace

SettingsScreen::SettingsScreen(Fonts &fonts, Navigator &nav) : fonts_(fonts), nav_(nav) {
	build();
}

void SettingsScreen::build() {
	rows_.clear();
	auto add = [&](Row row, const std::string &stored) {
		row.current = 0;
		for (int i = 0; i < (int)row.options.size(); i++)
			if (row.options[i].value == stored) row.current = i;
		rows_.push_back(row);
	};
	{
		Row r{ "Playback", "Direct play decodes the original file on the PS4 (no server load). Always transcode asks the server to convert everything to H.264.", "always_transcode" };
		r.is_bool = true;
		r.options = { { "Prefer direct play", "0" }, { "Always transcode", "1" } };
		add(r, settings::get_bool("always_transcode") ? "1" : "0");
	}
	{
		Row r{ "Maximum streaming bitrate", "Video bitrate the server transcodes to (only used when transcoding).", "max_bitrate" };
		r.is_int = true;
		r.options = { { "2 Mbps", "2000000" }, { "4 Mbps", "4000000" }, { "8 Mbps", "8000000" }, { "12 Mbps", "12000000" },
			{ "20 Mbps", "20000000" }, { "40 Mbps", "40000000" } };
		add(r, std::to_string(settings::get_int("max_bitrate", 8000000)));
	}
	const std::string uid = jf::client().user_id();
	{
		Row r{ "Default audio language", "Picked automatically when a title has it. Choosing a track in the player also sets this.", "audio_language" };
		r.per_user = true;
		for (auto &l : languages(false)) r.options.push_back({ l.first, l.second });
		add(r, settings::user_string(uid, "audio_language"));
	}
	{
		Row r{ "Default subtitle language", "File default shows subtitles the file marks as default or forced.", "subtitle_language" };
		r.per_user = true;
		for (auto &l : languages(true)) r.options.push_back({ l.first, l.second });
		add(r, settings::user_string(uid, "subtitle_language"));
	}
	{
		Row r{ "Subtitle size", "", "subtitle_size" };
		r.is_int = true;
		for (int p = 60; p <= 180; p += 10) r.options.push_back({ std::to_string(p) + " %", std::to_string(p) });
		add(r, std::to_string(settings::get_int("subtitle_size", 100)));
	}
	{
		Row r{ "Subtitle position", "Distance from the bottom of the picture.", "subtitle_margin" };
		r.is_int = true;
		for (int m = 20; m <= 320; m += 30) r.options.push_back({ std::to_string(m) + " px", std::to_string(m) });
		add(r, std::to_string(settings::get_int("subtitle_margin", 80)));
	}
	{
		Row r{ "Subtitle background", "A dark box behind the text.", "subtitle_bg" };
		r.is_bool = true;
		r.options = { { "On", "1" }, { "Off", "0" } };
		add(r, settings::get_bool("subtitle_bg", true) ? "1" : "0");
	}
	{
		Row r{ "Subtitle outline", "A black outline (used when the background box is off).", "subtitle_outline" };
		r.is_bool = true;
		r.options = { { "On", "1" }, { "Off", "0" } };
		add(r, settings::get_bool("subtitle_outline", true) ? "1" : "0");
	}
	{
		Row r{ "Subtitle colour", "", "subtitle_color" };
		r.options = { { "White", "white" }, { "Yellow", "yellow" }, { "Cyan", "cyan" }, { "Green", "green" } };
		add(r, settings::get_string("subtitle_color", "white"));
	}
	{
		Row r{ "Autoplay next episode", "Count down and play the next episode when one ends.", "autoplay_next" };
		r.is_bool = true;
		r.options = { { "On", "1" }, { "Off", "0" } };
		add(r, settings::get_bool("autoplay_next", true) ? "1" : "0");
	}
	{
		Row r{ "Screensaver", "Show backdrop art after a while with no input (prevents OLED burn-in).", "screensaver" };
		r.is_bool = true;
		r.options = { { "On", "1" }, { "Off", "0" } };
		add(r, settings::get_bool("screensaver", true) ? "1" : "0");
	}
	{
		Row r{ "Screensaver delay", "", "screensaver_seconds" };
		r.is_int = true;
		r.options = { { "1 min", "60" }, { "2 min", "120" }, { "3 min", "180" }, { "5 min", "300" }, { "10 min", "600" } };
		add(r, std::to_string(settings::get_int("screensaver_seconds", 120)));
	}
	{
		Row r{ "Audio output", "Multichannel sends 5.1/7.1 as PCM; the PS4 then downmixes or encodes it as set in Settings > Sound > Audio Output Settings. Bitstream passthrough of the file's AC3/DTS isn't available to homebrew.", "audio_output" };
		r.options = { { "Stereo", "stereo" }, { "Multichannel PCM (5.1 / 7.1)", "multichannel" } };
		add(r, settings::get_string("audio_output", "stereo"));
	}
}

void SettingsScreen::change(Row &row, int delta) {
	const int n = (int)row.options.size();
	row.current = (row.current + delta + n) % n;
	const std::string &v = row.options[row.current].value;
	Json value = row.is_bool ? Json(v == "1") : row.is_int ? Json((int64_t)atoll(v.c_str())) : Json(v);
	if (row.per_user) settings::set_user(jf::client().user_id(), row.key, value);
	else settings::set(row.key, value);
	platform::log("settings: %s = %s", row.key.c_str(), v.c_str());
}

void SettingsScreen::update(double dt, uint32_t pressed) {
	for (int i = 0; i < (int)rows_.size() && i < 16; i++) focus_[i] = ui::approach(focus_[i], i == row_ ? 1.0f : 0.0f, dt);
	if (pressed & platform::BUTTON_BACK) {
		nav_.pop();
		return;
	}
	const int n = (int)rows_.size();
	if (pressed & platform::BUTTON_UP) row_ = (row_ + n - 1) % n;
	if (pressed & platform::BUTTON_DOWN) row_ = (row_ + 1) % n;
	if (pressed & platform::BUTTON_LEFT) change(rows_[row_], -1);
	if (pressed & (platform::BUTTON_RIGHT | platform::BUTTON_ACCEPT)) change(rows_[row_], 1);
	scroll_ = ui::follow(scroll_, row_ * (ROW_H + ROW_GAP), ROW_H + 40, VIEW_BOTTOM - TOP, dt);
}

void SettingsScreen::draw(Renderer &r) {
	for (int i = 0; i < (int)rows_.size(); i++) {
		const Row &row = rows_[i];
		const float y = TOP + i * (ROW_H + ROW_GAP) - scroll_;
		if (y < TOP - ROW_H || y > VIEW_BOTTOM) continue;
		const float f = i < 16 ? focus_[i] : 0;
		ui::begin_focus(r, LEFT, y, WIDTH, ROW_H, theme::radius_field(), f, 1.01f);
		r.rounded_rect(LEFT, y, WIDTH, ROW_H, theme::radius_field(), i == row_ ? theme::BASE_300 : theme::BASE_200);
		r.text(fonts_.card_title, row.title, LEFT + 30, y + 40, theme::BASE_CONTENT);
		if (!row.help.empty()) {
			r.text(fonts_.small, fonts_.small.ellipsize(row.help, WIDTH - 700), LEFT + 30, y + 74, theme::BASE_CONTENT_MUTED);
		}
		const std::string value = "\xE2\x97\x80  " + row.options[row.current].label + "  \xE2\x96\xB6";
		r.text(fonts_.card_title, value, LEFT + WIDTH - 30 - fonts_.card_title.measure(value), y + ROW_H / 2 + 9,
				i == row_ ? theme::PRIMARY : theme::BASE_CONTENT_MUTED);
		ui::end_focus(r, LEFT, y, WIDTH, ROW_H, theme::radius_field(), f);
	}
	r.rect(0, 0, 1920, TOP - 20, theme::BASE_100);
	r.text(fonts_.page_header, "Settings", LEFT, 120, theme::BASE_CONTENT);
	const std::string who = jf::client().user_name() + " \xC2\xB7 " + jf::client().server();
	r.text(fonts_.small, who, LEFT + WIDTH - fonts_.small.measure(who), 120, theme::BASE_CONTENT_MUTED);
	r.rect(0, 1000, 1920, 80, theme::BASE_100);
	if (row_ < (int)rows_.size() && rows_[row_].help.size() > 70) {
		r.text(fonts_.small, fonts_.small.ellipsize(rows_[row_].help, 1600), LEFT, 1015, theme::BASE_CONTENT_MUTED);
	}
	ui::hints(r, fonts_.small, { { "\xE2\x97\x80 \xE2\x96\xB6", "Change" }, { "\xE2\x97\x8B", "Back" } }, LEFT, 1055);
	const std::string upd = update::available();
	if (!upd.empty()) {
		r.text(fonts_.small, "Update " + upd + " at " + update::REPO_URL, LEFT, 1015, theme::SECONDARY);
	}
	r.text(fonts_.small, "Jellyfin PS4 " APP_VERSION, 1920 - 330, 1055, theme::BASE_CONTENT_MUTED);
}
