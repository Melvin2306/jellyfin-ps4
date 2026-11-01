// Jellyfin PS4: a small native Jellyfin client for jailbroken PS4s
// (OpenOrbis homebrew). Platform, renderer, HTTP and FFmpeg player layers
// are adapted from Stremio PS4 (GPL-3.0); see README.md.

#include "core/jellyfin.h"
#include "core/settings.h"
#include "net/http.h"
#include "net/images.h"
#include "core/remote.h"
#include "net/update.h"
#include "platform/platform.h"
#include "render/font.h"
#include "render/renderer.h"
#include "ui/home_screen.h"
#include "ui/login_screen.h"
#include "ui/player_screen.h"
#include "ui/screensaver.h"
#include "ui/theme.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace {

const int WIDTH = 1920, HEIGHT = 1080;
const int HTTP_WORKERS = 4;
const double REPEAT_DELAY = 0.4, REPEAT_INTERVAL = 0.12;

// Held buttons -> "pressed this frame", with auto-repeat for directions.
class InputRepeater {
public:
	uint32_t update(uint32_t held, double now) {
		uint32_t pressed = held & ~previous_;
		for (int bit = 0; bit < 32; bit++) {
			const uint32_t b = 1u << bit;
			if (!(platform::DIRECTIONS & b)) continue;
			if (pressed & b) {
				next_repeat_[bit] = now + REPEAT_DELAY;
			} else if ((held & b) && now >= next_repeat_[bit]) {
				pressed |= b;
				next_repeat_[bit] = now + REPEAT_INTERVAL;
			}
		}
		previous_ = held;
		return pressed;
	}

private:
	uint32_t previous_ = 0;
	double next_repeat_[32] = {};
};

bool load_fonts(Fonts &f) {
	auto font = [](const char *file) { return platform::asset_path(std::string("fonts/") + file); };
	Font::set_fallbacks({});
	return f.page_header.load(font("Inter-Bold.ttf"), theme::size_page_header()) &&
			f.section.load(font("Inter-SemiBold.ttf"), theme::size_section_header()) &&
			f.card_title.load(font("Inter-Medium.ttf"), theme::size_card_title()) &&
			f.body.load(font("Inter-Regular.ttf"), theme::size_body()) &&
			f.small.load(font("Inter-Regular.ttf"), theme::size_small()) &&
			f.subtitle.load(font("Inter-SemiBold.ttf"), theme::SIZE_SUBTITLE);
}

} // namespace

int main() {
	if (!platform::init(WIDTH, HEIGHT)) {
		platform::log("platform init failed");
		return 1;
	}
	Renderer renderer;
	static Fonts fonts; // large (glyph atlases); keep off the stack
	bool ok = renderer.init(WIDTH, HEIGHT);
	platform::log("renderer init: %d", ok);
	ok = ok && load_fonts(fonts);
	platform::log("fonts: %d", ok);
	ok = ok && http::init(HTTP_WORKERS);
	platform::log("http: %d", ok);
	if (!ok) {
		platform::log("startup failed");
		for (;;) { // keep the process alive so the log can be read
			uint32_t held;
			if (!platform::poll(held)) return 1;
			platform::present();
		}
	}

	jf::Client &client = jf::client();
	client.init();
	update::start(); // non-blocking GitHub tag check
	remote::start();  // remote-control target (idles until signed in)
	platform::log("ready");

	struct Stack : Navigator {
		std::vector<std::unique_ptr<Screen>> screens;
		std::vector<std::unique_ptr<Screen>> to_push;
		int to_pop = 0;
		std::unique_ptr<Screen> new_root;
		void push(std::unique_ptr<Screen> screen) override { to_push.push_back(std::move(screen)); }
		void pop() override { to_pop++; }
		void replace_root(std::unique_ptr<Screen> screen) override { new_root = std::move(screen); }
		void apply() {
			if (new_root) {
				while (!screens.empty()) screens.pop_back();
				screens.push_back(std::move(new_root));
			}
			for (; to_pop > 0 && screens.size() > 1; to_pop--) screens.pop_back();
			to_pop = 0;
			for (auto &s : to_push) screens.push_back(std::move(s));
			to_push.clear();
		}
		void clear() {
			new_root.reset();
			to_push.clear();
			to_pop = 0;
			while (!screens.empty()) screens.pop_back(); // top first
		}
	} stack;

	int seen_auth_rev = -1;
	int frames = 0;
	InputRepeater input;
	double last = platform::now();
	uint32_t held = 0;
	static Screensaver screensaver;
	double idle = 0;
	std::string banner;
	double banner_time = 0;
	while (platform::poll(held)) {
		const double now = platform::now();
		const double dt = std::min(now - last, 0.1);
		last = now;

		http::poll();
		images::update();
		// A phone/web-UI "Play" request: fetch the item and start it.
		{
			std::string m = remote::take_message();
			if (!m.empty()) {
				banner = m;
				banner_time = 5.0;
			}
			banner_time = std::max(0.0, banner_time - dt);
		}
		remote::Command rc;
		if (client.logged_in() && remote::poll_play(rc)) {
			const double start = rc.start_seconds;
			Stack *st = &stack;
			Fonts *fp = &fonts;
			client.item(rc.item_id, [st, fp, start](bool ok, const std::string &, const std::vector<jf::Item> &items) {
				if (!ok || items.empty()) return;
				PlayerScreen::Args a;
				a.item = items[0];
				a.start = start > 0 ? start : items[0].position_seconds();
				st->push(std::unique_ptr<Screen>(new PlayerScreen(*fp, *st, a)));
			});
		}
		if (client.auth_rev != seen_auth_rev) {
			seen_auth_rev = client.auth_rev;
			stack.clear();
			if (client.logged_in()) {
				platform::log("screen: home");
				stack.screens.push_back(std::unique_ptr<Screen>(new HomeScreen(fonts, stack)));
			} else {
				platform::log("screen: sign-in");
				stack.screens.push_back(std::unique_ptr<Screen>(new LoginScreen(fonts)));
			}
		}

		Screen &top = *stack.screens.back();
		uint32_t pressed = input.update(held, now);
		// Idle screensaver (default on). Any input wakes it (and is swallowed).
		const double timeout = (double)settings::get_int("screensaver_seconds", 120);
		const bool enabled = settings::get_bool("screensaver", true) && timeout > 0;
		if (held != 0 || pressed != 0) idle = 0;
		else idle += dt;
		if (screensaver.active()) {
			screensaver.update(dt);
			if (pressed != 0 || top.suppress_screensaver()) {
				screensaver.deactivate();
				idle = 0;
				pressed = 0; // the wake press doesn't act on the screen below
			}
		} else if (enabled && idle >= timeout && !top.suppress_screensaver()) {
			screensaver.activate();
			pressed = 0;
		}
		top.update(dt, pressed);
		renderer.begin_frame(theme::BASE_100);
		top.draw(renderer);
		if (screensaver.active()) screensaver.draw(renderer, fonts);
		if (banner_time > 0 && !screensaver.active()) {
			const float w = fonts.body.measure(banner) + 80;
			renderer.rounded_rect((1920 - w) / 2, 40, w, 70, theme::radius_box(), theme::SECONDARY);
			renderer.text(fonts.body, banner, (1920 - fonts.body.measure(banner)) / 2, 84, theme::PRIMARY_CONTENT);
		}
		renderer.end_frame();
		platform::present();
		stack.apply();
		if (++frames == 1) platform::log("first frame drawn");
	}
	return 0;
}
