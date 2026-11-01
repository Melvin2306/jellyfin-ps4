// The PS4 system keyboard (sceImeDialog), non-blocking: open() shows it, the
// app keeps drawing frames and calls poll() each frame until it's finished.
#pragma once

#include <string>

namespace ime {

enum Kind { TEXT, URL, PASSWORD };
enum State { RUNNING, OK, CANCELLED, FAILED };

// Shows the system keyboard. False if it couldn't be opened (the caller then
// uses the app's own keyboard); the reason is logged.
bool open(const std::string &title, const std::string &initial, Kind kind, int max_length = 256);
// Call every frame while open. On OK, `text` receives the entered text (UTF-8).
State poll(std::string &text);
bool running();

} // namespace ime
