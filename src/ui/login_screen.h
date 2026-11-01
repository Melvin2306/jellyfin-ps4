// Sign-in:
//   1. Server: servers found by UDP discovery, or an address typed with the
//      system keyboard; checked via /System/Info/Public.
//   2. Users: the server's public users (with avatars) and the users with a
//      saved session on this console; "Other user" for manual entry.
//   3. Account: Quick Connect (when enabled) or username + password.
// The server URL and usernames are remembered; passwords never are.
// The app's own keyboard is only a fallback if the system one can't open.
#pragma once

#include "../core/jellyfin.h"
#include "../net/discovery.h"
#include "components.h"
#include "keyboard.h"
#include "screen.h"

#include <memory>
#include <string>
#include <vector>

class LoginScreen : public Screen {
public:
	explicit LoginScreen(Fonts &fonts);
	~LoginScreen() override;
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	enum Step { SERVER, USERS, ACCOUNT, QUICK };
	enum Kind { R_FOUND, R_SERVER, R_CONNECT, R_SEARCH, R_QUICK, R_USER, R_PASSWORD, R_SIGN_IN, R_CHANGE };
	enum Field { F_SERVER, F_USER, F_PASSWORD };
	struct Row {
		Kind kind;
		int index = 0; // R_FOUND: discovered server
	};

	std::vector<Row> rows() const;
	int row_of(Kind k) const;
	void edit(Field f);
	void field_done(Field f, bool accepted);
	void connect();
	void load_users();
	void pick_user(int index); // -1: Other user
	void submit();
	void start_quick_connect();
	void to_server_step();
	void set_message(const std::string &m, bool error);
	void draw_users(Renderer &r);

	Fonts &fonts_;
	Step step_ = SERVER;
	std::string fields_[3];
	int row_ = 0;
	float focus_[16] = {};
	bool busy_ = false;
	std::string message_;
	bool message_error_ = false;
	jf::ServerInfo server_;
	std::vector<discovery::Server> found_;
	// Users
	std::vector<jf::UserEntry> users_;
	int user_ = 0;
	float user_focus_[32] = {};
	// Text entry
	int editing_ = -1;
	bool fallback_kb_ = false;
	Keyboard keyboard_;
	double input_cooldown_ = 0;
	float cursor_time_ = 0;
	// Quick Connect
	std::string qc_code_;
	double qc_timer_ = 0;
	bool qc_polling_ = false;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};
