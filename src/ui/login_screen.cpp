#include "login_screen.h"

#include "../platform/ime.h"
#include "../platform/platform.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

namespace {
const float LEFT = 160, FIELD_W = 900, FIELD_H = 84;
const double QC_POLL_SECONDS = 3;
const char *PLACEHOLDER = "http://192.168.x.x:8096";
} // namespace

LoginScreen::LoginScreen(Fonts &fonts) : fonts_(fonts) {
	jf::Client &c = jf::client();
	fields_[F_SERVER] = c.saved_server();
	fields_[F_USER] = c.saved_user();
	const std::string notice = c.take_notice();
	if (!notice.empty()) set_message(notice, true);
	if (!fields_[F_SERVER].empty()) {
		connect(); // remembered server: go straight to the account step
	}
}

LoginScreen::~LoginScreen() {
	jf::client().quick_connect_cancel();
}

void LoginScreen::set_message(const std::string &m, bool error) {
	message_ = m;
	message_error_ = error;
}

std::vector<LoginScreen::Row> LoginScreen::rows() const {
	if (step_ == SERVER) return { R_SERVER, R_CONNECT };
	if (step_ == QUICK) return {};
	std::vector<Row> r;
	if (server_.quick_connect) r.push_back(R_QUICK);
	r.push_back(R_USER);
	r.push_back(R_PASSWORD);
	r.push_back(R_SIGN_IN);
	r.push_back(R_CHANGE);
	return r;
}

void LoginScreen::connect() {
	if (busy_) return;
	busy_ = true;
	const bool keep_notice = message_error_ && !message_.empty();
	if (!keep_notice) set_message("Looking for the server\xE2\x80\xA6", false);
	std::weak_ptr<int> alive = alive_;
	jf::client().check_server(fields_[F_SERVER], [this, alive, keep_notice](bool ok, const std::string &error,
			const jf::ServerInfo &info) {
		if (alive.expired()) return;
		busy_ = false;
		if (!ok) {
			step_ = SERVER;
			row_ = 0;
			set_message(error, true);
			return;
		}
		server_ = info;
		fields_[F_SERVER] = info.url;
		step_ = ACCOUNT;
		if (!keep_notice) message_.clear(); // keep e.g. "session expired" visible
		// Focus the password if the username is remembered, else Quick Connect.
		const std::vector<Row> rs = rows();
		const Row want = !fields_[F_USER].empty() ? R_PASSWORD : (info.quick_connect ? R_QUICK : R_USER);
		row_ = (int)(std::find(rs.begin(), rs.end(), want) - rs.begin());
	});
}

void LoginScreen::submit() {
	if (busy_) return;
	if (fields_[F_USER].empty()) {
		set_message("Enter your username.", true);
		return;
	}
	busy_ = true;
	set_message("Signing in\xE2\x80\xA6", false);
	std::weak_ptr<int> alive = alive_;
	jf::client().login(fields_[F_USER], fields_[F_PASSWORD], [this, alive](bool ok, const std::string &error) {
		if (alive.expired()) return; // signed in: the app has switched to Home
		busy_ = false;
		fields_[F_PASSWORD].clear();
		if (!ok) set_message(error, true);
	});
}

void LoginScreen::start_quick_connect() {
	if (busy_) return;
	busy_ = true;
	set_message("Requesting a Quick Connect code\xE2\x80\xA6", false);
	std::weak_ptr<int> alive = alive_;
	jf::client().quick_connect_start([this, alive](bool ok, const std::string &error, const std::string &code) {
		if (alive.expired()) return;
		busy_ = false;
		if (!ok) {
			set_message(error, true);
			return;
		}
		qc_code_ = code;
		qc_timer_ = QC_POLL_SECONDS;
		qc_polling_ = false;
		step_ = QUICK;
		message_.clear();
	});
}

void LoginScreen::edit(Field f) {
	static const char *titles[] = { "Jellyfin server address", "Jellyfin username", "Jellyfin password" };
	const ime::Kind kind = f == F_SERVER ? ime::URL : f == F_PASSWORD ? ime::PASSWORD : ime::TEXT;
	const std::string initial = f == F_PASSWORD ? "" : (f == F_SERVER && fields_[f].empty() ? "http://" : fields_[f]);
	editing_ = f;
	if (ime::open(titles[f], initial, kind)) {
		fallback_kb_ = false;
	} else {
		platform::log("login: system keyboard failed to open; using the built-in keyboard");
		fallback_kb_ = true;
	}
}

void LoginScreen::field_done(Field f, bool accepted) {
	editing_ = -1;
	if (!accepted) return;
	// Enter on the keyboard moves on: server -> connect, password -> sign in.
	if (f == F_SERVER) {
		connect();
	} else if (f == F_PASSWORD && !fields_[F_USER].empty()) {
		submit();
	} else if (f == F_USER) {
		const std::vector<Row> rs = rows();
		row_ = (int)(std::find(rs.begin(), rs.end(), R_PASSWORD) - rs.begin());
	}
}

void LoginScreen::update(double dt, uint32_t pressed) {
	cursor_time_ += (float)dt;
	const std::vector<Row> rs = rows();
	for (int i = 0; i < 8; i++) {
		const bool on = editing_ < 0 && i < (int)rs.size() && i == row_;
		focus_[i] = ui::approach(focus_[i], on ? 1.0f : 0.0f, dt);
	}

	// Text entry in progress.
	if (editing_ >= 0) {
		const Field f = (Field)editing_;
		if (!fallback_kb_) {
			std::string text;
			const ime::State st = ime::poll(text);
			if (st == ime::RUNNING) return;
			input_cooldown_ = 0.4; // the button that closed it mustn't act here too
			if (st == ime::FAILED) {
				platform::log("login: system keyboard failed; using the built-in keyboard");
				fallback_kb_ = true;
				return;
			}
			if (st == ime::OK) {
				fields_[f] = text;
				if (f != F_PASSWORD) { // trim spaces around the address / username
					while (!fields_[f].empty() && fields_[f].back() == ' ') fields_[f].pop_back();
					while (!fields_[f].empty() && fields_[f].front() == ' ') fields_[f].erase(0, 1);
				}
				if (f == F_SERVER && fields_[f] == "http://") fields_[f].clear();
			}
			field_done(f, st == ime::OK);
		} else if (keyboard_.update(dt, pressed, fields_[f]) == Keyboard::DONE) {
			field_done(f, true);
		}
		return;
	}
	if (input_cooldown_ > 0) {
		input_cooldown_ -= dt;
		return;
	}

	if (step_ == QUICK) {
		if (pressed & platform::BUTTON_BACK) {
			jf::client().quick_connect_cancel();
			step_ = ACCOUNT;
			row_ = 0;
			qc_code_.clear();
			return;
		}
		qc_timer_ -= dt;
		if (qc_timer_ <= 0 && !qc_polling_) {
			qc_polling_ = true;
			std::weak_ptr<int> alive = alive_;
			jf::client().quick_connect_poll([this, alive](int state, const std::string &error) {
				if (alive.expired()) return; // approved: the app has switched to Home
				qc_polling_ = false;
				qc_timer_ = QC_POLL_SECONDS;
				if (state < 0) {
					step_ = ACCOUNT;
					row_ = 0;
					qc_code_.clear();
					set_message(error, true);
				}
			});
		}
		return;
	}

	if (busy_ || rs.empty()) return;
	row_ = std::min(row_, (int)rs.size() - 1);
	if (pressed & platform::BUTTON_UP) row_ = (row_ + (int)rs.size() - 1) % (int)rs.size();
	if (pressed & platform::BUTTON_DOWN) row_ = (row_ + 1) % (int)rs.size();
	if (pressed & platform::BUTTON_ACCEPT) {
		switch (rs[row_]) {
			case R_SERVER: edit(F_SERVER); break;
			case R_CONNECT: connect(); break;
			case R_QUICK: start_quick_connect(); break;
			case R_USER: edit(F_USER); break;
			case R_PASSWORD: edit(F_PASSWORD); break;
			case R_SIGN_IN: submit(); break;
			case R_CHANGE:
				step_ = SERVER;
				row_ = 0;
				message_.clear();
				break;
		}
	}
	if ((pressed & platform::BUTTON_BACK) && step_ == ACCOUNT) {
		step_ = SERVER;
		row_ = 0;
		message_.clear();
	}
}

void LoginScreen::draw(Renderer &r) {
	r.text(fonts_.page_header, "Jellyfin", LEFT, 150, theme::PRIMARY);
	std::string sub = "Connect to your Jellyfin server";
	if (step_ != SERVER && !server_.url.empty()) {
		sub = "Connected to " + (server_.name.empty() ? std::string("Jellyfin") : server_.name) +
				(server_.version.empty() ? "" : " \xC2\xB7 Jellyfin " + server_.version);
	}
	r.text(fonts_.body, sub, LEFT, 205, step_ == SERVER ? theme::BASE_CONTENT_MUTED : theme::ACCENT);
	if (step_ != SERVER) r.text(fonts_.small, server_.url, LEFT, 240, theme::BASE_CONTENT_MUTED);

	float y = 300;
	if (step_ == QUICK) {
		r.text(fonts_.section, "Quick Connect", LEFT, y + 10, theme::BASE_CONTENT);
		r.text(fonts_.body, "On your phone or computer, open Jellyfin (signed in as you), go to", LEFT, y + 80, theme::BASE_CONTENT_MUTED);
		r.text(fonts_.body, "your user menu \xE2\x86\x92 Quick Connect and enter this code:", LEFT, y + 116, theme::BASE_CONTENT_MUTED);
		r.rounded_rect(LEFT, y + 160, 620, 170, theme::radius_box(), theme::BASE_200);
		const float w = fonts_.page_header.measure(qc_code_);
		r.push(LEFT + 310, y + 245, 1.8f, 1.0f);
		r.text(fonts_.page_header, qc_code_, LEFT + 310 - w / 2, y + 245 + 17, theme::PRIMARY);
		r.pop();
		const int dots = (int)(cursor_time_ * 2) % 4;
		r.text(fonts_.body, std::string("Waiting for approval") + std::string(dots, '.'), LEFT, y + 400, theme::BASE_CONTENT_MUTED);
		ui::hints(r, fonts_.small, { { "\xE2\x97\x8B", "Cancel" } }, LEFT, 1040);
		return;
	}

	const std::vector<Row> rs = rows();
	auto field = [&](int index, Field f, const char *label) {
		r.text(fonts_.small, label, LEFT, y - 10, theme::BASE_CONTENT_MUTED);
		ui::begin_focus(r, LEFT, y, FIELD_W, FIELD_H, theme::radius_field(), focus_[index], 1.02f);
		const bool active = editing_ == f;
		r.rounded_rect(LEFT, y, FIELD_W, FIELD_H, theme::radius_field(), active ? theme::BASE_300 : theme::BASE_200);
		std::string shown = f == F_PASSWORD ? std::string(fields_[f].size(), '*') : fields_[f];
		if (active && fallback_kb_ && fmodf(cursor_time_, 1.0f) < 0.6f) shown += "|";
		if (shown.empty()) {
			const char *hint = f == F_SERVER ? PLACEHOLDER : f == F_USER ? "Your Jellyfin username" : "Password";
			r.text(fonts_.body, hint, LEFT + 28, y + FIELD_H / 2 + 8, rgb(0x6A6A70));
		} else {
			r.text(fonts_.body, fonts_.body.ellipsize(shown, FIELD_W - 56), LEFT + 28, y + FIELD_H / 2 + 8, theme::BASE_CONTENT);
		}
		ui::end_focus(r, LEFT, y, FIELD_W, FIELD_H, theme::radius_field(), focus_[index]);
		y += FIELD_H + 56;
	};
	auto button = [&](int index, const std::string &label, bool primary) {
		const float bw = std::max(320.0f, fonts_.section.measure(label) + 80);
		ui::begin_focus(r, LEFT, y, bw, FIELD_H, theme::radius_field(), focus_[index], 1.05f);
		r.rounded_rect(LEFT, y, bw, FIELD_H, theme::radius_field(), busy_ ? theme::BASE_300 : primary ? theme::PRIMARY : theme::BASE_300);
		r.text(fonts_.section, label, LEFT + (bw - fonts_.section.measure(label)) / 2, y + FIELD_H / 2 + 11, theme::PRIMARY_CONTENT);
		ui::end_focus(r, LEFT, y, bw, FIELD_H, theme::radius_field(), focus_[index]);
		y += FIELD_H + 30;
	};
	if (step_ == ACCOUNT && server_.quick_connect) y = 280;
	for (int i = 0; i < (int)rs.size(); i++) {
		switch (rs[i]) {
			case R_SERVER: field(i, F_SERVER, "Server address"); break;
			case R_CONNECT: button(i, busy_ ? "Please wait" : "Connect", true); break;
			case R_QUICK:
				button(i, "Sign in with Quick Connect", true);
				r.text(fonts_.small, "or with your username and password:", LEFT, y + 6, theme::BASE_CONTENT_MUTED);
				y += 60;
				break;
			case R_USER: field(i, F_USER, "Username"); break;
			case R_PASSWORD: field(i, F_PASSWORD, "Password"); break;
			case R_SIGN_IN: button(i, busy_ ? "Please wait" : "Sign in", !server_.quick_connect); break;
			case R_CHANGE: {
				const float tw = fonts_.body.measure("Change server");
				ui::begin_focus(r, LEFT, y, tw + 48, 56, theme::radius_field(), focus_[i], 1.04f);
				r.rounded_rect(LEFT, y, tw + 48, 56, theme::radius_field(), theme::BASE_200);
				r.text(fonts_.body, "Change server", LEFT + 24, y + 36, theme::BASE_CONTENT_MUTED);
				ui::end_focus(r, LEFT, y, tw + 48, 56, theme::radius_field(), focus_[i]);
				y += 80;
				break;
			}
		}
	}
	if (!message_.empty()) {
		const float mx = LEFT + FIELD_W + 100;
		float my = step_ == SERVER ? 330 : 420;
		for (const std::string &line : fonts_.body.wrap(message_, 1920 - mx - 100, 6)) {
			r.text(fonts_.body, line, mx, my, message_error_ ? theme::ERROR : theme::BASE_CONTENT_MUTED);
			my += 34;
		}
	}
	if (editing_ >= 0 && fallback_kb_) {
		keyboard_.draw(r, fonts_, 1920 - keyboard_.width() - 110, 560);
	} else if (editing_ < 0) {
		std::vector<ui::Hint> hints = { { "\xC3\x97", "Select" } };
		if (step_ == ACCOUNT) hints.push_back({ "\xE2\x97\x8B", "Change server" });
		ui::hints(r, fonts_.small, hints, LEFT, 1040);
	}
	r.text(fonts_.small, "Jellyfin PS4 " APP_VERSION, 1920 - 330, 1040, theme::BASE_CONTENT_MUTED);
}
