// Remote-control target: keeps a WebSocket to the Jellyfin server open so a
// phone or the web UI can control playback on the PS4. Commands arrive on a
// background thread and are drained on the main thread.
#pragma once

#include <string>

namespace remote {

struct Command {
	enum Type { PAUSE, UNPAUSE, PLAYPAUSE, STOP, SEEK, NEXT, PREV, PLAY } type = PAUSE;
	double seek_seconds = 0;    // SEEK
	std::string item_id;        // PLAY
	double start_seconds = 0;   // PLAY
};

// Starts the connection (after sign-in). Reconnects on its own.
void start();
void stop();

// The player drains playstate commands (pause/seek/stop/next/prev).
bool poll_playstate(Command &out);
// The main loop drains Play requests (start a new item).
bool poll_play(Command &out);

// A short message pushed by the server (DisplayMessage), or "".
std::string take_message();

} // namespace remote
