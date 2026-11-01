#include "detail_screen.h"

#include "../core/tracks.h"
#include "../net/images.h"
#include "../platform/platform.h"
#include "player_screen.h"
#include "theme.h"

#include <algorithm>
#include <cstdio>

namespace {
const float POSTER_X = 110, POSTER_Y = 120, POSTER_W = 440, POSTER_H = 660;
const float INFO_X = 640, INFO_W = 1170;
const float EP_TOP = 380, EP_ROW = 92, EP_VIEW = 600;

std::string clock_text(double seconds) {
	const int s = (int)seconds;
	char buf[32];
	if (s >= 3600) snprintf(buf, sizeof(buf), "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
	else snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
	return buf;
}
} // namespace

DetailScreen::DetailScreen(Fonts &fonts, Navigator &nav, const jf::Item &item) : fonts_(fonts), nav_(nav), item_(item) {
	series_ = item.type == "Series";
	seen_library_rev_ = jf::client().library_rev;
	build_buttons();
	reload();
}

void DetailScreen::reload() {
	std::weak_ptr<int> alive = alive_;
	if (series_) {
		loading_ = true;
		jf::client().episodes(item_.id, [this, alive](bool ok, const std::string &error, const std::vector<jf::Item> &items) {
			if (alive.expired()) return;
			loading_ = false;
			error_ = ok ? "" : error;
			if (ok) {
				episodes_ = items;
				// Start at the first unwatched episode.
				if (episode_ == 0) {
					for (int i = 0; i < (int)episodes_.size(); i++) {
						if (!episodes_[i].played) {
							episode_ = i;
							break;
						}
					}
				}
				episode_ = std::min(episode_, std::max(0, (int)episodes_.size() - 1));
			}
		});
	} else {
		// Fresh user data (resume position) and media streams.
		jf::client().item(item_.id, [this, alive](bool ok, const std::string &, const std::vector<jf::Item> &items) {
			if (alive.expired() || !ok || items.empty()) return;
			item_ = items[0];
			build_buttons();
		});
	}
}

void DetailScreen::build_buttons() {
	if (series_) return;
	std::vector<Button> b;
	const double pos = item_.position_seconds();
	if (pos > 30) b.push_back({ "Resume from " + clock_text(pos), false, pos });
	b.push_back({ pos > 30 ? "Play from start" : "Play", false, 0 });
	b.push_back({ "Play (server transcode)", true, pos > 30 ? pos : 0 });
	// Tracks: the user's defaults until chosen here.
	if (audio_ == -2 || !item_.stream(audio_)) audio_ = tracks::default_audio(item_);
	if (subtitle_ == -2 || (subtitle_ >= 0 && !item_.stream(subtitle_))) subtitle_ = tracks::default_subtitle(item_, false);
	if (!item_.audio_streams().empty()) {
		Button a{ "Audio: " + tracks::audio_label(item_, audio_), false, 0 };
		a.menu = 0;
		b.push_back(a);
	}
	{
		Button t{ "Subtitles: " + tracks::subtitle_label(item_, subtitle_), false, 0 };
		t.menu = 1;
		b.push_back(t);
	}
	const bool same = b.size() == buttons_.size();
	buttons_ = b;
	if (!same) button_ = 0;
	button_ = std::min(button_, (int)buttons_.size() - 1);
}

void DetailScreen::update(double dt, uint32_t pressed) {
	if (jf::client().library_rev != seen_library_rev_) {
		seen_library_rev_ = jf::client().library_rev;
		reload();
	}
	if (!series_ && menu_.visible()) {
		const TrackMenu::Result res = menu_.update(dt, pressed);
		if (res == TrackMenu::AUDIO_CHANGED || res == TrackMenu::SUBTITLE_CHANGED) {
			audio_ = menu_.audio();
			subtitle_ = menu_.subtitle();
			build_buttons();
		}
		return;
	}
	if (pressed & platform::BUTTON_BACK) {
		nav_.pop();
		return;
	}
	if (series_) {
		const int n = (int)episodes_.size();
		if ((int)ep_focus_.size() != n) ep_focus_.assign(n, 0.0f);
		for (int i = 0; i < n; i++) ep_focus_[i] = ui::approach(ep_focus_[i], i == episode_ ? 1.0f : 0.0f, dt);
		if (n == 0) {
			if ((pressed & platform::BUTTON_ACCEPT) && !loading_) reload();
			return;
		}
		if (pressed & platform::BUTTON_UP) episode_ = std::max(0, episode_ - 1);
		if (pressed & platform::BUTTON_DOWN) episode_ = std::min(n - 1, episode_ + 1);
		if (pressed & platform::BUTTON_L1) episode_ = std::max(0, episode_ - 6);
		if (pressed & platform::BUTTON_R1) episode_ = std::min(n - 1, episode_ + 6);
		scroll_ = ui::follow(scroll_, episode_ * EP_ROW, EP_ROW, EP_VIEW, dt);
		if (pressed & platform::BUTTON_ACCEPT) {
			nav_.push(std::unique_ptr<Screen>(new DetailScreen(fonts_, nav_, episodes_[episode_])));
		}
		return;
	}
	for (int i = 0; i < (int)buttons_.size(); i++) {
		buttons_[i].focus = ui::approach(buttons_[i].focus, i == button_ ? 1.0f : 0.0f, dt);
	}
	if (pressed & (platform::BUTTON_LEFT | platform::BUTTON_UP)) button_ = std::max(0, button_ - 1);
	if (pressed & (platform::BUTTON_RIGHT | platform::BUTTON_DOWN)) button_ = std::min((int)buttons_.size() - 1, button_ + 1);
	if (pressed & platform::BUTTON_ACCEPT) {
		const Button &b = buttons_[button_];
		if (b.menu >= 0) {
			menu_.open(item_, false, audio_, subtitle_, b.menu);
			return;
		}
		platform::log("detail: play '%s' transcode=%d start=%.0f", item_.name.c_str(), b.transcode, b.start);
		PlayerScreen::Args args;
		args.item = item_;
		args.transcode = b.transcode;
		args.start = b.start;
		args.audio = audio_;
		// A subtitle that needs direct play falls back to off when transcoding.
		const jf::Stream *sub = item_.stream(subtitle_);
		args.subtitle = (b.transcode && sub && !tracks::subtitle_supported(*sub, true)) ? -1 : subtitle_;
		nav_.push(std::unique_ptr<Screen>(new PlayerScreen(fonts_, nav_, args)));
	}
}

void DetailScreen::draw(Renderer &r) {
	jf::Client &c = jf::client();
	r.rounded_rect(POSTER_X, POSTER_Y, POSTER_W, POSTER_H, theme::radius_box(), theme::BASE_200);
	const std::string img = c.image_url(item_, 720);
	if (!img.empty()) {
		const Texture &t = images::get(img);
		if (t.valid()) {
			r.image_cover(t, POSTER_X, POSTER_Y, POSTER_W, POSTER_H);
			r.round_corners(POSTER_X, POSTER_Y, POSTER_W, POSTER_H, theme::radius_box(), theme::BASE_100);
		}
	}
	float y = POSTER_Y + 50;
	if (item_.type == "Episode" && !item_.series_name.empty()) {
		r.text(fonts_.card_title, item_.series_name, INFO_X, y - 6, theme::PRIMARY);
		y += 56;
	}
	r.text(fonts_.page_header, fonts_.page_header.ellipsize(item_.display_name(), INFO_W), INFO_X, y, theme::BASE_CONTENT);
	y += 60;
	r.text(fonts_.body, fonts_.body.ellipsize(item_.info_line(), INFO_W), INFO_X, y, theme::BASE_CONTENT_MUTED);
	y += 50;
	if (item_.played) {
		ui::badge(r, fonts_.small, "Watched", INFO_X, y - 26, theme::ACCENT, theme::ACCENT_CONTENT);
		y += 40;
	}

	if (series_) {
		for (const std::string &line : fonts_.body.wrap(item_.overview, INFO_W, 3)) {
			r.text(fonts_.body, line, INFO_X, y, theme::BASE_CONTENT);
			y += 32;
		}
		if (episodes_.empty()) {
			const std::string msg = loading_ ? "Loading episodes\xE2\x80\xA6"
					: !error_.empty() ? "Couldn't load episodes: " + error_ + " (\xC3\x97 to retry)" : "No episodes.";
			r.text(fonts_.body, msg, INFO_X, EP_TOP + 40, error_.empty() ? theme::BASE_CONTENT_MUTED : theme::ERROR);
		}
		for (int i = 0; i < (int)episodes_.size(); i++) {
			const float ey = EP_TOP + i * EP_ROW - scroll_;
			if (ey < EP_TOP - EP_ROW || ey > EP_TOP + EP_VIEW) continue;
			const jf::Item &e = episodes_[i];
			const float f = i < (int)ep_focus_.size() ? ep_focus_[i] : 0;
			ui::begin_focus(r, INFO_X, ey, INFO_W, EP_ROW - 14, theme::radius_field(), f, 1.01f);
			r.rounded_rect(INFO_X, ey, INFO_W, EP_ROW - 14, theme::radius_field(), i == episode_ ? theme::BASE_300 : theme::BASE_200);
			r.text(fonts_.card_title, fonts_.card_title.ellipsize(e.display_name(), INFO_W - 260), INFO_X + 24, ey + 48,
					theme::BASE_CONTENT);
			std::string right = e.played ? "Watched" : "";
			if (!e.played && e.position_ticks > 0) right = "In progress";
			if (e.runtime_ticks > 0) right += (right.empty() ? "" : " \xC2\xB7 ") + std::to_string((int)(e.runtime_seconds() / 60)) + " min";
			r.text(fonts_.small, right, INFO_X + INFO_W - 24 - fonts_.small.measure(right), ey + 46, theme::BASE_CONTENT_MUTED);
			if (e.runtime_ticks > 0 && e.position_ticks > 0) {
				ui::progress(r, INFO_X + 24, ey + EP_ROW - 26, INFO_W - 48, 4, (float)e.position_ticks / (float)e.runtime_ticks);
			}
			ui::end_focus(r, INFO_X, ey, INFO_W, EP_ROW - 14, theme::radius_field(), f);
		}
		// Mask rows scrolled above the list.
		r.rect(INFO_X - 30, EP_TOP - EP_ROW - 10, INFO_W + 60, EP_ROW + 4, theme::BASE_100);
		r.text(fonts_.section, "Episodes", INFO_X, EP_TOP - 30, theme::BASE_CONTENT);
		if (!episodes_.empty()) {
			const std::string pos = std::to_string(episode_ + 1) + " / " + std::to_string(episodes_.size());
			r.text(fonts_.small, pos, INFO_X + INFO_W - fonts_.small.measure(pos), EP_TOP - 30, theme::BASE_CONTENT_MUTED);
		}
		ui::hints(r, fonts_.small, { { "\xC3\x97", "Open" }, { "L1 R1", "Page" }, { "\xE2\x97\x8B", "Back" } }, INFO_X, 1030);
		return;
	}

	for (const std::string &line : fonts_.body.wrap(item_.overview, INFO_W, 9)) {
		r.text(fonts_.body, line, INFO_X, y, theme::BASE_CONTENT);
		y += 34;
	}
	float bx = INFO_X, tx = INFO_X;
	const float by = 800, bh = 84, ty = 910, th = 64;
	for (Button &b : buttons_) {
		if (b.menu >= 0) { // track chips on a second row
			const std::string label = fonts_.body.ellipsize(b.label, 520);
			const float bw = fonts_.body.measure(label) + 56;
			ui::begin_focus(r, tx, ty, bw, th, theme::radius_field(), b.focus, 1.05f);
			r.rounded_rect(tx, ty, bw, th, theme::radius_field(), theme::BASE_200);
			r.text(fonts_.body, label, tx + 28, ty + th / 2 + 8, theme::BASE_CONTENT);
			ui::end_focus(r, tx, ty, bw, th, theme::radius_field(), b.focus);
			tx += bw + 24;
			continue;
		}
		const float bw = fonts_.card_title.measure(b.label) + 80;
		ui::begin_focus(r, bx, by, bw, bh, theme::radius_field(), b.focus, 1.06f);
		const bool primary = &b == &buttons_[0];
		r.rounded_rect(bx, by, bw, bh, theme::radius_field(), primary ? theme::PRIMARY : theme::BASE_300);
		r.text(fonts_.card_title, b.label, bx + 40, by + bh / 2 + 9, theme::PRIMARY_CONTENT);
		ui::end_focus(r, bx, by, bw, bh, theme::radius_field(), b.focus);
		bx += bw + 30;
	}
	if (item_.runtime_ticks > 0 && item_.position_ticks > 0) {
		ui::progress(r, INFO_X, by - 40, 600, 8, (float)item_.position_ticks / (float)item_.runtime_ticks);
	}
	ui::hints(r, fonts_.small, { { "\xC3\x97", "Select" }, { "\xE2\x97\x8B", "Back" } }, INFO_X, 1040);
	menu_.draw(r, fonts_);
}
