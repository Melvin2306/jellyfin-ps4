#include "login_screen.h"

#include "../net/images.h"
#include "../platform/ime.h"
#include "../platform/platform.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

namespace {
const float LEFT = 160, FIELD_W = 900, FIELD_H = 84;
const double QC_POLL_SECONDS = 3;
const char *PLACEHOLDER = "http://192.168.x.x:8096";
const float AVATAR = 190, AVATAR_GAP = 50, USERS_TOP = 330;
const int USERS_PER_ROW = 6;
} // namespace

LoginScreen::LoginScreen(Fonts &fonts) : fonts_(fonts) {
	jf::Client &c = jf::client();
	fields_[F_SERVER] = c.saved_server();
	fields_[F_USER] = c.saved_user();
	const std::string notice = c.take_notice();
	if (!notice.empty()) set_message(notice, true);
	if (!fields_[F_SERVER].empty()) {
		connect(); // remembered server: straight to the users
	} else {
		discovery::start();
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
	std::vector<Row> r;
	if (step_ == SERVER) {
		for (int i = 0; i < (int)found_.size() && i < 3; i++) r.push_back({ R_FOUND, i });
		r.push_back({ R_SERVER });
		r.push_back({ R_CONNECT });
		r.push_back({ R_SEARCH });
	} else if (step_ == ACCOUNT) {
		if (server_.quick_connect) r.push_back({ R_QUICK });
		r.push_back({ R_USER });
		r.push_back({ R_PASSWORD });
		r.push_back({ R_SIGN_IN });
		r.push_back({ R_CHANGE });
	}
	return r;
}

int LoginScreen::row_of(Kind k) const {
	const std::vector<Row> rs = rows();
	for (int i = 0; i < (int)rs.size(); i++)
		if (rs[i].kind == k) return i;
	return 0;
}

void LoginScreen::to_server_step() {
	step_ = SERVER;
	row_ = row_of(R_SERVER);
	if (found_.empty() && !discovery::running()) discovery::start();
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
			to_server_step();
			set_message(error, true);
			return;
		}
		server_ = info;
		fields_[F_SERVER] = info.url;
		if (!keep_notice) message_.clear(); // keep e.g. "session expired" visible
		load_users();
	});
}

void LoginScreen::load_users() {
	busy_ = true;
	std::weak_ptr<int> alive = alive_;
	jf::client().public_users([this, alive](bool, const std::vector<jf::UserEntry> &users) {
		if (alive.expired()) return;
		busy_ = false;
		users_ = users;
		if (users_.empty()) {
			// The server hides its users: straight to manual entry.
			step_ = ACCOUNT;
			row_ = !fields_[F_USER].empty() ? row_of(R_PASSWORD) : (server_.quick_connect ? row_of(R_QUICK) : row_of(R_USER));
			return;
		}
		step_ = USERS;
		user_ = 0;
		for (int i = 0; i < (int)users_.size(); i++)
			if (users_[i].name == fields_[F_USER]) user_ = i;
	});
}

void LoginScreen::pick_user(int index) {
	if (index < 0) { // Other user
		step_ = ACCOUNT;
		fields_[F_USER].clear();
		fields_[F_PASSWORD].clear();
		row_ = server_.quick_connect ? row_of(R_QUICK) : row_of(R_USER);
		return;
	}
	const jf::UserEntry &u = users_[index];
	if (u.signed_in && jf::client().switch_to(u.id)) return; // the app switches to Home
	fields_[F_USER] = u.name;
	fields_[F_PASSWORD].clear();
	if (!u.has_password) {
		submit();
		return;
	}
	step_ = ACCOUNT;
	row_ = row_of(R_PASSWORD);
	edit(F_PASSWORD); // straight to the keyboard
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
		if (!ok) {
			set_message(error, true);
			if (step_ != ACCOUNT) {
				step_ = ACCOUNT;
				row_ = row_of(R_PASSWORD);
			}
		}
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
	if (f == F_SERVER) {
		connect();
	} else if (f == F_PASSWORD && !fields_[F_USER].empty()) {
		submit();
	} else if (f == F_USER) {
		row_ = row_of(R_PASSWORD);
	}
}

void LoginScreen::update(double dt, uint32_t pressed) {
	cursor_time_ += (float)dt;
	const std::vector<Row> rs = rows();
	for (int i = 0; i < 16; i++) {
		const bool on = editing_ < 0 && i < (int)rs.size() && i == row_;
		focus_[i] = ui::approach(focus_[i], on ? 1.0f : 0.0f, dt);
	}
	for (int i = 0; i < 32; i++) {
		user_focus_[i] = ui::approach(user_focus_[i], (step_ == USERS && i == user_) ? 1.0f : 0.0f, dt);
	}
	if (step_ == SERVER) {
		const std::vector<discovery::Server> found = discovery::results();
		if (found.size() != found_.size()) {
			const bool first = found_.empty() && !found.empty();
			found_ = found;
			if (first && editing_ < 0) row_ = 0; // focus the first server found
		}
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
				if (f != F_PASSWORD) {
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
	if (busy_) return;

	if (step_ == USERS) {
		const int n = (int)users_.size() + 1; // + Other user
		if (pressed & platform::BUTTON_LEFT) user_ = std::max(0, user_ - 1);
		if (pressed & platform::BUTTON_RIGHT) user_ = std::min(n - 1, user_ + 1);
		if (pressed & platform::BUTTON_UP) user_ = std::max(0, user_ - USERS_PER_ROW);
		if (pressed & platform::BUTTON_DOWN) user_ = std::min(n - 1, user_ + USERS_PER_ROW);
		if (pressed & platform::BUTTON_ACCEPT) pick_user(user_ < (int)users_.size() ? user_ : -1);
		if (pressed & platform::BUTTON_BACK) {
			to_server_step();
			message_.clear();
		}
		return;
	}

	if (rs.empty()) return;
	row_ = std::min(row_, (int)rs.size() - 1);
	if (pressed & platform::BUTTON_UP) row_ = (row_ + (int)rs.size() - 1) % (int)rs.size();
	if (pressed & platform::BUTTON_DOWN) row_ = (row_ + 1) % (int)rs.size();
	if (pressed & platform::BUTTON_ACCEPT) {
		const Row &row = rs[row_];
		switch (row.kind) {
			case R_FOUND:
				fields_[F_SERVER] = found_[row.index].address;
				connect();
				break;
			case R_SERVER: edit(F_SERVER); break;
			case R_CONNECT: connect(); break;
			case R_SEARCH:
				found_.clear();
				discovery::start();
				break;
			case R_QUICK: start_quick_connect(); break;
			case R_USER: edit(F_USER); break;
			case R_PASSWORD: edit(F_PASSWORD); break;
			case R_SIGN_IN: submit(); break;
			case R_CHANGE:
				to_server_step();
				message_.clear();
				break;
		}
	}
	if ((pressed & platform::BUTTON_BACK) && step_ == ACCOUNT) {
		if (!users_.empty()) step_ = USERS;
		else to_server_step();
		message_.clear();
	}
}

void LoginScreen::draw_users(Renderer &r) {
	r.text(fonts_.section, "Who's watching?", LEFT, USERS_TOP - 50, theme::BASE_CONTENT);
	const int n = (int)users_.size() + 1;
	for (int i = 0; i < n; i++) {
		const float x = LEFT + (i % USERS_PER_ROW) * (AVATAR + AVATAR_GAP);
		const float y = USERS_TOP + (i / USERS_PER_ROW) * (AVATAR + 110);
		const float f = i < 32 ? user_focus_[i] : 0;
		const bool other = i == (int)users_.size();
		ui::begin_focus(r, x, y, AVATAR, AVATAR, AVATAR / 2, f, 1.08f);
		r.rounded_rect(x, y, AVATAR, AVATAR, AVATAR / 2, other ? theme::BASE_300 : theme::BASE_200);
		std::string label = other ? "Other user" : users_[i].name;
		const std::string img = other ? "" : jf::client().user_image_url(users_[i], 256);
		// User images aren't scaled by the server: skip huge originals (initial instead).
		const Texture *t = img.empty() ? nullptr : &images::get(img, nullptr, 4 * 1024 * 1024);
		if (t && t->valid()) {
			r.image_cover(*t, x, y, AVATAR, AVATAR);
			r.round_corners(x, y, AVATAR, AVATAR, AVATAR / 2, theme::BASE_100);
		} else if (other) {
			r.text(fonts_.page_header, "+", x + AVATAR / 2 - fonts_.page_header.measure("+") / 2, y + AVATAR / 2 + 18, theme::BASE_CONTENT);
		} else {
			ui::avatar(r, fonts_.page_header, users_[i].name, x, y, AVATAR);
		}
		ui::end_focus(r, x, y, AVATAR, AVATAR, AVATAR / 2, f);
		label = fonts_.card_title.ellipsize(label, AVATAR + 30);
		r.text(fonts_.card_title, label, x + (AVATAR - fonts_.card_title.measure(label)) / 2, y + AVATAR + 44,
				f > 0.5f ? theme::BASE_CONTENT : theme::BASE_CONTENT_MUTED);
		if (!other && users_[i].signed_in) {
			const std::string s = "Signed in";
			r.text(fonts_.small, s, x + (AVATAR - fonts_.small.measure(s)) / 2, y + AVATAR + 76, theme::ACCENT);
		}
	}
	ui::hints(r, fonts_.small, { { "\xC3\x97", "Select" }, { "\xE2\x97\x8B", "Change server" } }, LEFT, 1040);
}

void LoginScreen::draw(Renderer &r) {
	r.text(fonts_.page_header, "Jellyfin for PS4", LEFT, 150, theme::PRIMARY);
	std::string sub = "Connect to your Jellyfin server";
	if (step_ != SERVER && !server_.url.empty()) {
		sub = "Connected to " + (server_.name.empty() ? std::string("Jellyfin") : server_.name) +
				(server_.version.empty() ? "" : " \xC2\xB7 Jellyfin " + server_.version);
	}
	r.text(fonts_.body, sub, LEFT, 205, step_ == SERVER ? theme::BASE_CONTENT_MUTED : theme::ACCENT);
	if (step_ != SERVER) r.text(fonts_.small, server_.url, LEFT, 240, theme::BASE_CONTENT_MUTED);
	r.text(fonts_.small, "Jellyfin PS4 " APP_VERSION, 1920 - 330, 1040, theme::BASE_CONTENT_MUTED);

	if (step_ == USERS) {
		draw_users(r);
		if (!message_.empty()) r.text(fonts_.body, message_, LEFT, 990, message_error_ ? theme::ERROR : theme::BASE_CONTENT_MUTED);
		return;
	}
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
	auto small_button = [&](int index, const std::string &label) {
		const float tw = fonts_.body.measure(label);
		ui::begin_focus(r, LEFT, y, tw + 48, 56, theme::radius_field(), focus_[index], 1.04f);
		r.rounded_rect(LEFT, y, tw + 48, 56, theme::radius_field(), theme::BASE_200);
		r.text(fonts_.body, label, LEFT + 24, y + 36, theme::BASE_CONTENT_MUTED);
		ui::end_focus(r, LEFT, y, tw + 48, 56, theme::radius_field(), focus_[index]);
		y += 80;
	};
	if (step_ == ACCOUNT && server_.quick_connect) y = 280;
	if (step_ == SERVER && !found_.empty()) {
		r.text(fonts_.small, "Servers found on your network", LEFT, y - 10, theme::BASE_CONTENT_MUTED);
	}
	for (int i = 0; i < (int)rs.size(); i++) {
		switch (rs[i].kind) {
			case R_FOUND: {
				const discovery::Server &s = found_[rs[i].index];
				ui::begin_focus(r, LEFT, y, FIELD_W, 76, theme::radius_field(), focus_[i], 1.02f);
				r.rounded_rect(LEFT, y, FIELD_W, 76, theme::radius_field(), theme::BASE_200);
				r.text(fonts_.card_title, fonts_.card_title.ellipsize(s.name, 420), LEFT + 28, y + 48, theme::BASE_CONTENT);
				r.text(fonts_.small, s.address, LEFT + FIELD_W - 28 - fonts_.small.measure(s.address), y + 46, theme::BASE_CONTENT_MUTED);
				ui::end_focus(r, LEFT, y, FIELD_W, 76, theme::radius_field(), focus_[i]);
				y += 76 + 18;
				if (i + 1 < (int)rs.size() && rs[i + 1].kind != R_FOUND) y += 50;
				break;
			}
			case R_SERVER: field(i, F_SERVER, found_.empty() ? "Server address" : "Or enter an address"); break;
			case R_CONNECT: button(i, busy_ ? "Please wait" : "Connect", true); break;
			case R_SEARCH: small_button(i, discovery::running() ? "Searching\xE2\x80\xA6" : "Search the network again"); break;
			case R_QUICK:
				button(i, "Sign in with Quick Connect", true);
				r.text(fonts_.small, "or with your username and password:", LEFT, y + 6, theme::BASE_CONTENT_MUTED);
				y += 60;
				break;
			case R_USER: field(i, F_USER, "Username"); break;
			case R_PASSWORD: field(i, F_PASSWORD, "Password"); break;
			case R_SIGN_IN: button(i, busy_ ? "Please wait" : "Sign in", !server_.quick_connect); break;
			case R_CHANGE: small_button(i, "Change server"); break;
		}
	}
	std::string msg = message_;
	bool err = message_error_;
	if (msg.empty() && step_ == SERVER) {
		msg = discovery::status();
		err = false;
	}
	if (!msg.empty()) {
		const float mx = LEFT + FIELD_W + 100;
		float my = step_ == SERVER ? 330 : 420;
		for (const std::string &line : fonts_.body.wrap(msg, 1920 - mx - 100, 6)) {
			r.text(fonts_.body, line, mx, my, err ? theme::ERROR : theme::BASE_CONTENT_MUTED);
			my += 34;
		}
	}
	if (editing_ >= 0 && fallback_kb_) {
		keyboard_.draw(r, fonts_, 1920 - keyboard_.width() - 110, 560);
	} else if (editing_ < 0) {
		std::vector<ui::Hint> hints = { { "\xC3\x97", "Select" } };
		if (step_ == ACCOUNT) hints.push_back({ "\xE2\x97\x8B", users_.empty() ? "Change server" : "Users" });
		ui::hints(r, fonts_.small, hints, LEFT, 1040);
	}
}
