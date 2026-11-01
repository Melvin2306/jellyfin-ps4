// sceImeDialog wrapper. Set-up as in Apollo Save Tool (bucanero/apollo-ps4,
// source/dialog.c and main.c): load the IME dialog and CommonDialog modules,
// sceCommonDialogInitialize, then sceImeDialogInit / GetStatus / GetResult /
// Term. Text buffers are UTF-16 in uint16_t arrays: the PS4 system libraries
// use a 2-byte wchar_t, while this compiler's wchar_t is 4 bytes.
#include "ime.h"

#include "platform.h"

#include <orbis/CommonDialog.h>
#include <orbis/ImeDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>

#include <cstring>

namespace ime {

namespace {

const uint32_t OPTION_PASSWORD = 0x4; // SCE_IME_OPTION_PASSWORD
const int MAX_TEXT = 512;

bool g_ready = false, g_tried = false, g_running = false;
uint16_t g_text[MAX_TEXT + 1];
uint16_t g_title[128];

bool init() {
	if (g_tried) return g_ready;
	g_tried = true;
	const int ime = sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG);
	const int common = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG);
	const int cd = common >= 0 ? sceCommonDialogInitialize() : -1;
	platform::log("ime: module 0x%08X, common dialog module 0x%08X, init 0x%08X", ime, common, cd);
	// "Already initialised" (0x80B80002) is fine too.
	g_ready = ime >= 0 && common >= 0 && (cd >= 0 || (uint32_t)cd == 0x80B80002u);
	return g_ready;
}

void to_utf16(const std::string &in, uint16_t *out, size_t cap) {
	size_t n = 0, i = 0;
	while (i < in.size() && n + 2 < cap) {
		uint32_t c = (unsigned char)in[i++];
		int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
		if (extra) c &= (0x3F >> extra);
		while (extra-- > 0 && i < in.size()) c = (c << 6) | ((unsigned char)in[i++] & 0x3F);
		if (c >= 0x10000) {
			c -= 0x10000;
			out[n++] = (uint16_t)(0xD800 + (c >> 10));
			out[n++] = (uint16_t)(0xDC00 + (c & 0x3FF));
		} else {
			out[n++] = (uint16_t)c;
		}
	}
	out[n] = 0;
}

std::string from_utf16(const uint16_t *in) {
	std::string out;
	for (size_t i = 0; in[i]; i++) {
		uint32_t c = in[i];
		if (c >= 0xD800 && c < 0xDC00 && in[i + 1] >= 0xDC00 && in[i + 1] < 0xE000) {
			c = 0x10000 + ((c - 0xD800) << 10) + (in[i + 1] - 0xDC00);
			i++;
		}
		if (c < 0x80) {
			out += (char)c;
		} else if (c < 0x800) {
			out += (char)(0xC0 | (c >> 6));
			out += (char)(0x80 | (c & 0x3F));
		} else if (c < 0x10000) {
			out += (char)(0xE0 | (c >> 12));
			out += (char)(0x80 | ((c >> 6) & 0x3F));
			out += (char)(0x80 | (c & 0x3F));
		} else {
			out += (char)(0xF0 | (c >> 18));
			out += (char)(0x80 | ((c >> 12) & 0x3F));
			out += (char)(0x80 | ((c >> 6) & 0x3F));
			out += (char)(0x80 | (c & 0x3F));
		}
	}
	return out;
}

} // namespace

bool open(const std::string &title, const std::string &initial, Kind kind, int max_length) {
	if (g_running) return true;
	if (!init()) {
		platform::log("ime: system keyboard unavailable (module load / CommonDialog init failed)");
		return false;
	}
	if (max_length > MAX_TEXT) max_length = MAX_TEXT;
	memset(g_text, 0, sizeof(g_text));
	memset(g_title, 0, sizeof(g_title));
	to_utf16(initial, g_text, MAX_TEXT + 1);
	to_utf16(title, g_title, sizeof(g_title) / sizeof(g_title[0]));

	int32_t user = -1;
	sceUserServiceGetInitialUser(&user);

	OrbisImeDialogSetting param;
	memset(&param, 0, sizeof(param));
	param.userId = (uint32_t)user;
	// Password mode: basic-latin keyboard, default enter label (the GO label
	// plus the password option was rejected on 13.xx with 0x80BC0030).
	param.type = kind == URL ? ORBIS_TYPE_TYPE_URL : kind == PASSWORD ? ORBIS_TYPE_BASIC_LATIN : ORBIS_TYPE_DEFAULT;
	param.supportedLanguages = 0;
	param.enterLabel = ORBIS_BUTTON_LABEL_DEFAULT;
	param.option = kind == PASSWORD ? OPTION_PASSWORD : 0;
	param.maxTextLength = (uint32_t)max_length;
	param.inputTextBuffer = (wchar_t *)g_text;
	param.title = (const wchar_t *)g_title;
	param.posx = 1920 / 2;
	param.posy = 1080 / 2;
	param.horizontalAlignment = ORBIS_H_CENTER;
	param.verticalAlignment = ORBIS_V_CENTER;
	const int rc = sceImeDialogInit(&param, nullptr);
	if (rc < 0) {
		platform::log("ime: sceImeDialogInit failed 0x%08X (user %d, kind %d)", (uint32_t)rc, user, (int)kind);
		return false;
	}
	platform::log("ime: opened (kind %d)", (int)kind);
	g_running = true;
	return true;
}

State poll(std::string &text) {
	if (!g_running) return FAILED;
	const OrbisDialogStatus status = sceImeDialogGetStatus();
	if (status == ORBIS_DIALOG_STATUS_RUNNING) return RUNNING;
	OrbisDialogResult result;
	memset(&result, 0, sizeof(result));
	const int rc = sceImeDialogGetResult(&result);
	sceImeDialogTerm();
	g_running = false;
	if (status != ORBIS_DIALOG_STATUS_STOPPED || rc < 0) {
		platform::log("ime: dialog ended unexpectedly (status %d, result 0x%08X)", (int)status, (uint32_t)rc);
		return FAILED;
	}
	if (result.endstatus == ORBIS_DIALOG_OK) {
		text = from_utf16(g_text);
		platform::log("ime: done (%d characters)", (int)text.size());
		return OK;
	}
	platform::log("ime: cancelled");
	return CANCELLED;
}

bool running() {
	return g_running;
}

} // namespace ime
