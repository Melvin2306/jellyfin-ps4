#include "screensaver.h"

#include "../net/images.h"
#include "../platform/platform.h"
#include "theme.h"

#include <algorithm>

namespace {
const double HOLD_SECONDS = 12.0; // time on each backdrop
} // namespace

void Screensaver::activate() {
	if (active_) return;
	active_ = true;
	fade_ = 0;
	hold_ = 0;
	if (items_.empty() && !loading_) {
		loading_ = true;
		std::weak_ptr<int> alive = alive_;
		jf::client().random_backdrops([this, alive](bool ok, const std::string &, const std::vector<jf::Item> &items) {
			if (alive.expired()) return;
			loading_ = false;
			if (ok) {
				for (const jf::Item &it : items)
					if (!it.backdrop_tag.empty()) items_.push_back(it);
				platform::log("screensaver: %d backdrops", (int)items_.size());
			}
		});
	}
}

void Screensaver::deactivate() {
	active_ = false;
}

void Screensaver::update(double dt) {
	if (!active_) return;
	fade_ = std::min(1.0f, fade_ + (float)dt * 0.7f);
	hold_ += dt;
	if (!items_.empty() && hold_ >= HOLD_SECONDS) {
		hold_ = 0;
		index_ = (index_ + 1) % (int)items_.size();
	}
}

void Screensaver::draw(Renderer &r, Fonts &fonts) {
	if (!active_) return;
	r.rect(0, 0, 1920, 1080, rgb(0x000000));
	if (items_.empty()) {
		const std::string msg = loading_ ? "" : "Jellyfin";
		r.text(fonts.page_header, msg, (1920 - fonts.page_header.measure(msg)) / 2, 540, theme::BASE_CONTENT_MUTED.with_alpha(fade_));
		return;
	}
	const jf::Item &it = items_[index_];
	const Texture &t = images::get(jf::client().backdrop_url(it, 1920));
	// Slow fade of each backdrop as it comes in.
	const float imga = std::min(1.0f, (float)hold_ * 1.2f) * fade_;
	if (t.valid()) r.image_cover(t, 0, 0, 1920, 1080, imga);
	// Darken the lower third for the text.
	r.vertical_fade(0, 620, 1920, 460, rgb(0x000000), 0.0f, 0.85f * fade_);
	r.text(fonts.page_header, fonts.page_header.ellipsize(it.name, 1600), 120, 900, theme::BASE_CONTENT.with_alpha(fade_));
	if (it.year > 0) r.text(fonts.body, std::to_string(it.year), 120, 945, theme::BASE_CONTENT_MUTED.with_alpha(fade_));
	const std::vector<std::string> lines = fonts.body.wrap(it.overview, 1400, 2);
	float y = 990;
	for (const std::string &line : lines) {
		r.text(fonts.body, line, 120, y, theme::BASE_CONTENT_MUTED.with_alpha(fade_));
		y += 32;
	}
	r.text(fonts.small, "Press any button", 1920 - 260, 1040, theme::BASE_CONTENT_MUTED.with_alpha(fade_ * 0.6f));
}
