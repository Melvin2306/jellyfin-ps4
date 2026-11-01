// Video backend factory. Jellyfin PS4 uses the FFmpeg software player: it
// direct-plays everything in the library (H.264/HEVC in MP4/MKV, AAC/AC3),
// which matters because the Raspberry Pi server can't transcode in real time.
#include "video_player.h"

#include "../platform/platform.h"

std::unique_ptr<VideoPlayer> create_ffmpeg_player();

bool VideoPlayer::hardware_selected() { return false; }
void VideoPlayer::select_hardware(bool) {}

std::unique_ptr<VideoPlayer> VideoPlayer::create() {
	platform::log("player: FFmpeg backend");
	return create_ffmpeg_player();
}
