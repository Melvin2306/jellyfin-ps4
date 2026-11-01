// Sign-in in two steps:
//   1. Server: URL (system keyboard), checked via /System/Info/Public.
//   2. Account: Quick Connect (when the server enables it) or username +
//      password. The server URL and username are remembered; the password
//      never is.
// Text is entered with the PS4 system keyboard (sceImeDialog); the app's own
// keyboard is only a fallback if that can't be opened.
#pragma once

#include "../core/jellyfin.h"
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
	enum Step { SERVER, ACCOUNT, QUICK };
	enum Row { R_SERVER, R_CONNECT, R_QUICK, R_USER, R_PASSWORD, R_SIGN_IN, R_CHANGE };
	enum Field { F_SERVER, F_USER, F_PASSWORD };

	std::vector<Row> rows() const;
	void edit(Field f);
	void field_done(Field f, bool accepted);
	void connect();
	void submit();
	void start_quick_connect();
	void set_message(const std::string &m, bool error);

	Fonts &fonts_;
	Step step_ = SERVER;
	std::string fields_[3];
	int row_ = 0;
	float focus_[8] = {};
	bool busy_ = false;
	std::string message_;
	bool message_error_ = false;
	jf::ServerInfo server_;
	// Text entry
	int editing_ = -1;          // field being edited
	bool fallback_kb_ = false;  // the app's keyboard instead of the system one
	Keyboard keyboard_;
	double input_cooldown_ = 0; // ignore presses right after the system keyboard closes
	float cursor_time_ = 0;
	// Quick Connect
	std::string qc_code_;
	double qc_timer_ = 0;
	bool qc_polling_ = false;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};
