#include "detail_screen.h"

#include "../core/settings.h"
#include "../core/tracks.h"
#include "../net/images.h"
#include "../platform/platform.h"
#include "browse_screen.h"
#include "player_screen.h"
#include "theme.h"

#include <algorithm>
#include <cstdio>

namespace {
const float POSTER_X = 110, POSTER_Y = 120, POSTER_W = 440, POSTER_H = 660;
const float INFO_X = 640, INFO_W = 1170;
const float EP_TOP = 380, EP_ROW = 92, EP_VIEW = 600;
// Cast and "More like this" rows (content coordinates, before page scroll).
const float CARD_GAP = 24;
const float CAST_Y = 1060, CAST_W = 160, CAST_IMG = 160;
const float SIM_Y = 1400, SIM_W = 170, SIM_IMG_H = 255;

std::string clock_text(double seconds) {
	const int s = (int)seconds;
	char buf[32];
	if (s >= 3600) snprintf(buf, sizeof(buf), "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
	else snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
	return buf;
}
} // namespace

DetailScreen::DetailScreen(Fonts &fonts, Navigator &nav, const jf::Item &item, const jf::Item *up_next)
	: fonts_(fonts), nav_(nav), item_(item) {
	if (up_next) {
		up_next_ = *up_next;
		has_up_next_ = true;
	}
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
		// Fresh user data (resume position), media streams, cast, chapters.
		jf::client().item(item_.id, [this, alive](bool ok, const std::string &, const std::vector<jf::Item> &items) {
			if (alive.expired() || !ok || items.empty()) return;
			const std::string keep_image = item_.image_tag;
			item_ = items[0];
			if (item_.image_tag.empty()) item_.image_tag = keep_image;
			cast_focus_.assign(item_.people.size(), 0.0f);
			build_buttons();
		});
		jf::client().similar(item_.id, [this, alive](bool ok, const std::string &, const std::vector<jf::Item> &items) {
			if (alive.expired() || !ok) return;
			similar_ = items;
			sim_focus_.assign(similar_.size(), 0.0f);
		});
	}
}

void DetailScreen::toggle(bool favorite) {
	const bool value = favorite ? !item_.favorite : !item_.played;
	if (favorite) item_.favorite = value;
	else item_.played = value;
	build_buttons();
	std::weak_ptr<int> alive = alive_;
	const std::string what = favorite ? "favorite" : "watched";
	auto done = [this, alive, favorite, value](bool ok, const std::string &) {
		if (alive.expired() || ok) return;
		if (favorite) item_.favorite = !value; // undo
		else item_.played = !value;
		build_buttons();
	};
	platform::log("detail: %s -> %d", what.c_str(), value);
	if (favorite) jf::client().set_favorite(item_.id, value, done);
	else jf::client().set_played(item_.id, value, done);
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
	{
		Button f{ item_.favorite ? "Remove favorite" : "Add to favorites", false, 0 };
		f.menu = 2;
		b.push_back(f);
		Button w{ item_.played ? "Mark unwatched" : "Mark watched", false, 0 };
		w.menu = 3;
		b.push_back(w);
		const bool inwl = jf::client().in_watchlist(item_.id);
		Button wl{ inwl ? "Remove from Watchlist" : "Add to Watchlist", false, 0 };
		wl.menu = 4;
		b.push_back(wl);
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
		if (pressed & platform::BUTTON_TRIANGLE) toggle(true);
		if (pressed & platform::BUTTON_SQUARE) toggle(false);
		return;
	}
	// Focus animations.
	for (int i = 0; i < (int)buttons_.size(); i++)
		buttons_[i].focus = ui::approach(buttons_[i].focus, (section_ == S_BUTTONS && i == button_) ? 1.0f : 0.0f, dt);
	if ((int)cast_focus_.size() != (int)item_.people.size()) cast_focus_.assign(item_.people.size(), 0.0f);
	for (int i = 0; i < (int)cast_focus_.size(); i++)
		cast_focus_[i] = ui::approach(cast_focus_[i], (section_ == S_CAST && i == cast_) ? 1.0f : 0.0f, dt);
	if ((int)sim_focus_.size() != (int)similar_.size()) sim_focus_.assign(similar_.size(), 0.0f);
	for (int i = 0; i < (int)sim_focus_.size(); i++)
		sim_focus_[i] = ui::approach(sim_focus_[i], (section_ == S_SIMILAR && i == similar_sel_) ? 1.0f : 0.0f, dt);

	if (!has_section(section_)) section_ = next_section(section_, 1);
	if (pressed & platform::BUTTON_UP) {
		const int s = next_section(section_, -1);
		if (s != section_) section_ = s;
	}
	if (pressed & platform::BUTTON_DOWN) {
		const int s = next_section(section_, 1);
		if (s != section_) section_ = s;
	}

	if (section_ == S_BUTTONS) {
		if (pressed & platform::BUTTON_LEFT) button_ = std::max(0, button_ - 1);
		if (pressed & platform::BUTTON_RIGHT) button_ = std::min((int)buttons_.size() - 1, button_ + 1);
		if (pressed & platform::BUTTON_ACCEPT) {
			const Button &b = buttons_[button_];
			if (b.menu == 2 || b.menu == 3) {
				toggle(b.menu == 2);
			} else if (b.menu == 4) {
				if (jf::client().in_watchlist(item_.id)) jf::client().watchlist_remove(item_.id);
				else jf::client().watchlist_add(item_.id);
				build_buttons();
			} else if (b.menu >= 0) {
				menu_.open(item_, false, audio_, subtitle_, b.menu);
			} else {
				play(b.transcode, b.start);
			}
		}
	} else if (section_ == S_CAST) {
		const int n = (int)item_.people.size();
		if (pressed & platform::BUTTON_LEFT) cast_ = std::max(0, cast_ - 1);
		if (pressed & platform::BUTTON_RIGHT) cast_ = std::min(n - 1, cast_ + 1);
		row_scroll_cast_ = ui::follow(row_scroll_cast_, cast_ * (CAST_W + CARD_GAP), CAST_W, 1920 - INFO_X - 80, dt);
		if ((pressed & platform::BUTTON_ACCEPT) && cast_ < n) {
			const jf::Person &p = item_.people[cast_];
			platform::log("detail: open person '%s'", p.name.c_str());
			nav_.push(std::unique_ptr<Screen>(new BrowseScreen(fonts_, nav_, p.name, "personIds=" + p.id +
					"&IncludeItemTypes=Movie,Series,Episode&Recursive=true")));
		}
	} else if (section_ == S_SIMILAR) {
		const int n = (int)similar_.size();
		if (pressed & platform::BUTTON_LEFT) similar_sel_ = std::max(0, similar_sel_ - 1);
		if (pressed & platform::BUTTON_RIGHT) similar_sel_ = std::min(n - 1, similar_sel_ + 1);
		row_scroll_sim_ = ui::follow(row_scroll_sim_, similar_sel_ * (SIM_W + CARD_GAP), SIM_W, 1920 - INFO_X - 80, dt);
		if ((pressed & platform::BUTTON_ACCEPT) && similar_sel_ < n) open_item(fonts_, nav_, similar_[similar_sel_]);
	}
	const float target = section_ == S_BUTTONS ? 0.0f : section_ == S_CAST ? CAST_Y - 360 : SIM_Y - 360;
	page_scroll_ = ui::approach(page_scroll_, target, dt);
}

bool DetailScreen::has_section(int s) const {
	if (s == S_BUTTONS) return !buttons_.empty();
	if (s == S_CAST) return !item_.people.empty();
	if (s == S_SIMILAR) return !similar_.empty();
	return false;
}

int DetailScreen::next_section(int from, int dir) const {
	for (int s = from + dir; s >= 0 && s < S_COUNT; s += dir)
		if (has_section(s)) return s;
	return from;
}

void DetailScreen::play(bool transcode, double start) {
	platform::log("detail: play '%s' transcode=%d start=%.0f", item_.name.c_str(), transcode, start);
	PlayerScreen::Args args;
	args.item = item_;
	args.transcode = transcode || settings::get_bool("always_transcode");
	args.start = start;
	args.audio = audio_;
	const jf::Stream *sub = item_.stream(subtitle_);
	args.subtitle = (args.transcode && sub && !tracks::subtitle_supported(*sub, true)) ? -1 : subtitle_;
	if (has_up_next_) {
		args.next_item = up_next_;
		args.has_next_item = true;
	}
	nav_.push(std::unique_ptr<Screen>(new PlayerScreen(fonts_, nav_, args)));
}

void DetailScreen::draw_row(Renderer &r, float y, const char *title, int section) {
	r.text(fonts_.section, title, INFO_X, y - 30, section == section_ ? theme::BASE_CONTENT : theme::BASE_CONTENT_MUTED);
	if (section == S_CAST) {
		for (int i = 0; i < (int)item_.people.size(); i++) {
			const jf::Person &p = item_.people[i];
			const float x = INFO_X + i * (CAST_W + CARD_GAP) - row_scroll_cast_;
			if (x + CAST_W < INFO_X - 40 || x > 1920) continue;
			const float f = i < (int)cast_focus_.size() ? cast_focus_[i] : 0;
			ui::begin_focus(r, x, y, CAST_IMG, CAST_IMG, CAST_IMG / 2, f, 1.08f);
			r.rounded_rect(x, y, CAST_IMG, CAST_IMG, CAST_IMG / 2, theme::BASE_200);
			const std::string img = jf::client().person_image_url(p, 256);
			const Texture *t = img.empty() ? nullptr : &images::get(img, nullptr, 4 * 1024 * 1024);
			if (t && t->valid()) {
				r.image_cover(*t, x, y, CAST_IMG, CAST_IMG);
				r.round_corners(x, y, CAST_IMG, CAST_IMG, CAST_IMG / 2, theme::BASE_100);
			} else {
				ui::avatar(r, fonts_.page_header, p.name, x, y, CAST_IMG);
			}
			ui::end_focus(r, x, y, CAST_IMG, CAST_IMG, CAST_IMG / 2, f);
			r.text(fonts_.small, fonts_.small.ellipsize(p.name, CAST_W + 20), x, y + CAST_IMG + 32,
					f > 0.5f ? theme::BASE_CONTENT : theme::BASE_CONTENT_MUTED);
			if (!p.role.empty()) r.text(fonts_.small, fonts_.small.ellipsize(p.role, CAST_W + 20), x, y + CAST_IMG + 60, theme::BASE_CONTENT_MUTED);
		}
	} else {
		jf::Client &c = jf::client();
		for (int i = 0; i < (int)similar_.size(); i++) {
			const jf::Item &it = similar_[i];
			const float x = INFO_X + i * (SIM_W + CARD_GAP) - row_scroll_sim_;
			if (x + SIM_W < INFO_X - 40 || x > 1920) continue;
			const float f = i < (int)sim_focus_.size() ? sim_focus_[i] : 0;
			ui::begin_focus(r, x, y, SIM_W, SIM_IMG_H, theme::radius_field(), f, 1.07f);
			r.rounded_rect(x, y, SIM_W, SIM_IMG_H, theme::radius_field(), theme::BASE_200);
			const std::string img = c.image_url(it, 360);
			const Texture *t = img.empty() ? nullptr : &images::get(img);
			if (t && t->valid()) {
				r.image_cover(*t, x, y, SIM_W, SIM_IMG_H);
				r.round_corners(x, y, SIM_W, SIM_IMG_H, theme::radius_field(), theme::BASE_100);
			} else {
				r.text(fonts_.small, fonts_.small.ellipsize(it.name, SIM_W - 16), x + 10, y + SIM_IMG_H / 2, theme::BASE_CONTENT);
			}
			ui::end_focus(r, x, y, SIM_W, SIM_IMG_H, theme::radius_field(), f);
			r.text(fonts_.small, fonts_.small.ellipsize(it.name, SIM_W + 16), x, y + SIM_IMG_H + 30,
					f > 0.5f ? theme::BASE_CONTENT : theme::BASE_CONTENT_MUTED);
		}
	}
}

void DetailScreen::draw(Renderer &r) {
	jf::Client &c = jf::client();
	const float oy = series_ ? 0.0f : -page_scroll_; // movie page scrolls; series doesn't
	r.rounded_rect(POSTER_X, POSTER_Y + oy, POSTER_W, POSTER_H, theme::radius_box(), theme::BASE_200);
	const std::string img = c.image_url(item_, 720);
	if (!img.empty()) {
		const Texture &t = images::get(img);
		if (t.valid()) {
			r.image_cover(t, POSTER_X, POSTER_Y + oy, POSTER_W, POSTER_H);
			r.round_corners(POSTER_X, POSTER_Y + oy, POSTER_W, POSTER_H, theme::radius_box(), theme::BASE_100);
		}
	}
	float y = POSTER_Y + 50 + oy;
	if (item_.type == "Episode" && !item_.series_name.empty()) {
		r.text(fonts_.card_title, item_.series_name, INFO_X, y - 6, theme::PRIMARY);
		y += 56;
	}
	r.text(fonts_.page_header, fonts_.page_header.ellipsize(item_.display_name(), INFO_W), INFO_X, y, theme::BASE_CONTENT);
	y += 60;
	r.text(fonts_.body, fonts_.body.ellipsize(item_.info_line(), INFO_W), INFO_X, y, theme::BASE_CONTENT_MUTED);
	y += 50;
	if (item_.played || item_.favorite) {
		float bx0 = INFO_X;
		if (item_.played) bx0 += ui::badge(r, fonts_.small, "Watched", bx0, y - 26, theme::ACCENT, theme::ACCENT_CONTENT) + 12;
		if (item_.favorite) ui::badge(r, fonts_.small, "Favorite", bx0, y - 26, theme::SECONDARY, theme::PRIMARY_CONTENT);
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
		r.rect(INFO_X - 30, EP_TOP - EP_ROW - 10, INFO_W + 60, EP_ROW + 4, theme::BASE_100);
		r.text(fonts_.section, "Episodes", INFO_X, EP_TOP - 30, theme::BASE_CONTENT);
		if (!episodes_.empty()) {
			const std::string pos = std::to_string(episode_ + 1) + " / " + std::to_string(episodes_.size());
			r.text(fonts_.small, pos, INFO_X + INFO_W - fonts_.small.measure(pos), EP_TOP - 30, theme::BASE_CONTENT_MUTED);
		}
		ui::hints(r, fonts_.small, { { "\xC3\x97", "Open" }, { "L1 R1", "Page" },
				{ "\xE2\x96\xB3", item_.favorite ? "Unfavorite" : "Favorite" }, { "\xE2\x96\xA1", item_.played ? "Mark unwatched" : "Mark watched" },
				{ "\xE2\x97\x8B", "Back" } }, INFO_X, 1030);
		return;
	}

	for (const std::string &line : fonts_.body.wrap(item_.overview, INFO_W, 6)) {
		r.text(fonts_.body, line, INFO_X, y, theme::BASE_CONTENT);
		y += 34;
	}
	float bx = INFO_X, tx = INFO_X;
	const float by = 800 + oy, bh = 84, ty = 910 + oy, th = 64;
	for (Button &b : buttons_) {
		if (b.menu >= 0) {
			const std::string label = fonts_.body.ellipsize(b.label, b.menu <= 1 ? 260 : 300);
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
	if (!item_.people.empty()) draw_row(r, CAST_Y + oy, "Cast & crew", S_CAST);
	if (!similar_.empty()) draw_row(r, SIM_Y + oy, "More like this", S_SIMILAR);
	// Hints bar (pinned).
	r.rect(0, 1005, 1920, 75, theme::BASE_100);
	std::vector<ui::Hint> hints = { { "\xC3\x97", "Select" } };
	if (has_section(S_CAST) || has_section(S_SIMILAR)) hints.push_back({ "\xE2\x96\xBC \xE2\x96\xB2", "Rows" });
	hints.push_back({ "\xE2\x97\x8B", "Back" });
	ui::hints(r, fonts_.small, hints, INFO_X, 1050);
	menu_.draw(r, fonts_);
}
