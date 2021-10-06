/*
	Windows SMTC Integration for DeaDBeeF
	Copyright (C) 2021 Jakub Wasylków <kuba_160@protonmail.com>

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any damages
	arising from the use of this software.

	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:

	1. The origin of this software must not be misrepresented; you must not
	 claim that you wrote the original software. If you use this software
	 in a product, an acknowledgment in the product documentation would be
	 appreciated but is not required.

	2. Altered source versions must be plainly marked as such, and must not be
	 misrepresented as being the original software.

	3. This notice may not be removed or altered from any source distribution.
*/
#include <iostream>
#include "framework.h"
#include "ddb_smtc.h"
#include "include/deadbeef/deadbeef.h"
#include "include/deadbeef/artwork.h"
#undef __ARTWORK_H
#include "include/deadbeef/artwork-legacy.h"
#include "PP-UVC.h"

#ifdef __cplusplus
extern "C" {
#endif

using namespace PP::UVC;

static DB_functions_t *deadbeef;
static DB_misc_t plugin;
static PP::UVC::API *ppuvc;

char smtc_enabled = 0;
int playback_resume = 0;
int playback_resume_status = 0;
ddb_artwork_plugin_t *artwork_new = nullptr;
DB_artwork_plugin_t *artwork_legacy = nullptr;


#define MAX_LEN 256
#define trace(...) { deadbeef->log_detailed (&plugin.plugin, 0, __VA_ARGS__); }
#define trace_err(...) { deadbeef->log ( __VA_ARGS__); }

int
win_charset_conv(const void *in, int inlen, void *out, int outlen, const char *cs_in, const char *cs_out) {
	int ret = 0;
	// Only UTF-8 <-> UTF-16 allowed
	if (strncmp(cs_in, "UTF-8", 5) == 0 && strncmp(cs_out, "WCHAR_T", 7) == 0) {
		// MultiByte to WideChar
		ret = MultiByteToWideChar(CP_UTF8, /*FLAGS*/ 0, (char *)in, inlen, (wchar_t *)out, outlen);
	}
	else if (strncmp(cs_out, "UTF-8", 5) == 0 && strncmp(cs_in, "WCHAR_T", 7) == 0) {
		// WideChar to MultiByte
		ret = WideCharToMultiByte(CP_UTF8, /*FLAGS*/ 0, (wchar_t *)in, inlen, (char *)out, outlen, NULL, NULL);
	}
	return ret;
}

static char * nowplaying_format_string(char * script) {
	DB_playItem_t * nowplaying = deadbeef->streamer_get_playing_track();
	if (!nowplaying) {
		return 0;
	}
	ddb_playlist_t * nowplaying_plt = deadbeef->plt_get_curr();
	char * code_script = deadbeef->tf_compile(script);
	ddb_tf_context_t context;
	context._size = sizeof(ddb_tf_context_t);
	context.flags = 0;
	context.it = nowplaying;
	context.plt = nowplaying_plt;
	context.idx = 0;
	context.id = 0;
	context.iter = PL_MAIN;
	context.update = 0;
	context.dimmed = 0;
	char * out = static_cast<char *>(malloc(MAX_LEN));
	if (out && code_script) {
		deadbeef->tf_eval(&context, code_script, out, MAX_LEN);
		trace("nowplaying_format_string: \"%s\"\n", out);
		deadbeef->tf_free(code_script);
	}
	deadbeef->pl_item_unref(nowplaying);
	if (nowplaying_plt) {
		deadbeef->plt_unref(nowplaying_plt);
	}
	return out;
}

void *cover_data = nullptr;
int cover_size = 0;

void cover_callback(int error, ddb_cover_query_t *query, ddb_cover_info_t *cover) {
	trace ("cover_callback: error %d, cover %x\n", error, cover);
	if (!error) {
		trace ("cover_callback: cover found: %d\n", cover->cover_found);
		if (cover) {
			if (cover->image_filename) {
				// convert backslashes
				{
					char *s = cover->image_filename;
					while (s = strchr(s, '/')) {
						*s = '\\';
					}
				}
				FILE *fp;
				fopen_s(&fp, cover->image_filename, "rb");
				if (fp) {
					if (cover_data) {
						// could lead to double free corruption...
						// should be freed in updateSMTC_title();
						Sleep(100);
						//free(cover_data);
					}
					fseek(fp, 0L, SEEK_END);
					cover_size = ftell(fp);
					rewind(fp);
					cover_data = malloc(cover_size);
					fread(cover_data, 1, cover_size, fp);
					fclose(fp);

					// call update_SMTC_title()
					typedef void(*func)(void);
					reinterpret_cast<func>(query->user_data)();
				}
				else {
					trace ("couldn't open artwork file\n");
				}
			}
		}
	}
	if (query) {
		if (query->track) {
			deadbeef->pl_item_unref(query->track);
		}
		free(query);
	}
	if (cover) {
		artwork_new->cover_info_release(cover);
	}
}

void cover_callback_legacy(const char *fname, const char *artist, const char *album, void *user_data) {
	trace ("cover_callback_legacy\n");
	if (!fname) {
		trace ("cover_callback_legacy: cover not found\n");
		return;
	}
	char *fname_conv = static_cast<char *>(malloc(strlen(fname) + 1));
	strcpy_s(fname_conv, strlen(fname)+1, fname);
	{
		char *s = fname_conv;
		while (s = strchr(s, '/')) {
			*s = '\\';
		}
	}
	FILE *fp;
	fopen_s(&fp, fname_conv, "rb");
	if (fp) {
		if (cover_data) {
			// could lead to double free corruption...
			// should be freed in updateSMTC_title();
			Sleep(100);
			//free(cover_data);
		}
		fseek(fp, 0L, SEEK_END);
		cover_size = ftell(fp);
		rewind(fp);
		cover_data = malloc(cover_size);
		fread(cover_data, 1, cover_size, fp);
		fclose(fp);

		// call update_SMTC_title()
		typedef void(*func)(void);
		if (user_data) {
			reinterpret_cast<func>(user_data)();
		}
	}
	else {
		trace("couldn't open artwork file\n");
	}
}

class DeadbeefCallbacks : public PP::UVC::UserEventCallback {
	void Play() {
		deadbeef->sendmessage(DB_EV_PLAY_CURRENT, 0, 0, 0);
	};
	void Pause() {
		deadbeef->sendmessage(DB_EV_TOGGLE_PAUSE, 0, 0, 0);
	};
	void Stop() {
		deadbeef->sendmessage(DB_EV_STOP, 0, 0, 0);
	};
	void Next() {
		deadbeef->sendmessage(DB_EV_NEXT, 0, 0, 0);
	};
	void Previous() {
		deadbeef->sendmessage(DB_EV_PREV, 0, 0, 0);
	};
	void FastForward() {
		// TODO: Add seeking support?
	};
	void Rewind() {
		// TODO: Add seeking support?
	};

};

static void updateSMTC_title() {
	trace ("updateSMTC_title\n");

	char script[MAX_LEN];

	// title_text
	char * title_text;
	deadbeef->conf_get_str("smtc.title_script", "%title%", script, MAX_LEN);
	title_text = nowplaying_format_string(script);

	// artist_text
	char * artist_text;
	deadbeef->conf_get_str("smtc.artist_script", "%artist%", script, MAX_LEN);
	artist_text = nowplaying_format_string(script);

	// album_text
	char * album_text;
	deadbeef->conf_get_str("smtc.album_script", "%album%", script, MAX_LEN);
	album_text = nowplaying_format_string(script);

	wchar_t title_w[MAX_LEN];
	wchar_t artist_w[MAX_LEN];
	wchar_t album_w[MAX_LEN];
	win_charset_conv(title_text, strlen(title_text)+1, title_w, MAX_LEN, "UTF-8", "WCHAR_T");
	win_charset_conv(artist_text, strlen(artist_text)+1, artist_w, MAX_LEN, "UTF-8", "WCHAR_T");
	win_charset_conv(album_text, strlen(album_text)+1, album_w, MAX_LEN, "UTF-8", "WCHAR_T");

	TrackInfo ti;
	ti.title = title_w;
	ti.artist = artist_w;
	ti.albumTitle = album_w;
	ti.albumArtist = nullptr;
	if (cover_data) {
		ti.imgData = cover_data;
		ti.imgBytes = cover_size;
	}
	else {
		if (artwork_new) {
			ti.imgData = nullptr;
			ti.imgBytes = 0;
			ddb_cover_query_t *query = static_cast<ddb_cover_query_t *>(malloc(sizeof(ddb_cover_query_t)));
			query->_size = sizeof(ddb_cover_query_t);
			query->track = deadbeef->streamer_get_streaming_track();
			query->user_data = updateSMTC_title;
			query->flags = 0;
			query->type = nullptr;
			artwork_new->cover_get(query, cover_callback);
		}
		else if (artwork_legacy) {
			DB_playItem_t *it = deadbeef->streamer_get_streaming_track();
			if (it) {
				char fname[FILENAME_MAX];
				deadbeef->pl_lock();
				strcpy_s(fname, FILENAME_MAX, deadbeef->pl_find_meta(it, ":URI"));
				deadbeef->pl_unlock();
				char * ret = artwork_legacy->get_album_art(fname, artist_text, album_text, -1, cover_callback_legacy, updateSMTC_title);
				if (ret) {
					// call callback just to read file into cover_data, ergo don't pass updateSMTC_title
					cover_callback_legacy(ret, artist_text, album_text, nullptr);
					ti.imgData = cover_data;
					ti.imgBytes = cover_size;
				}
				else {
					ti.imgData = nullptr;
					ti.imgBytes = 0;
				}
				deadbeef->pl_item_unref(it);
			}
		}
		else {
			ti.imgData = nullptr;
			ti.imgBytes = 0;
		}
	}
	// TODO: set track count to number of tracks in playlist?
	ti.trackCount = 0;
	ti.trackNumber = 0;

	ppuvc->NewTrack(ti);
	ppuvc->Paused(0);

	if (title_text)
		free(title_text);
	if (artist_text)
		free(artist_text);
	if (album_text)
		free(album_text);

	// free only if cover_data was used in this update, else it should be freed on next update
	if (cover_data && ti.imgData == cover_data) {
		free(cover_data);
		cover_data = nullptr;
	}

}

int ddb_smtc_start() {
	ppuvc = PP_UVC_Init(new DeadbeefCallbacks);
	int enable = deadbeef->conf_get_int("smtc.enable", 1);
	if (enable) {
		ppuvc->Stopped();
		smtc_enabled = 1;
	}
	// HACK, determine if resuming track
	{
		int resume = deadbeef->conf_get_int("resume_last_session", 1);
		int plt = deadbeef->conf_get_int("resume.playlist", -1);
		int track = deadbeef->conf_get_int("resume.track", -1);
		float pos = deadbeef->conf_get_float("resume.position", -1);
		int paused = deadbeef->conf_get_int("resume.paused", 0);
		if (resume && plt >= 0 && track >= 0 && pos >= 0) {
			playback_resume = 3;
			playback_resume_status = paused;
		}
	}
	return 0;
}

int ddb_smtc_stop() {
	if (smtc_enabled) {
		// PP-UWP does not support closing
		smtc_enabled = 0;
	}
	return 0;
}

int ddb_smtc_connect() {
	artwork_new = reinterpret_cast<ddb_artwork_plugin_t *>(deadbeef->plug_get_for_id("artwork2"));
	artwork_legacy = reinterpret_cast<DB_artwork_plugin_t *>(deadbeef->plug_get_for_id("artwork"));
	if (!artwork_new || !artwork_legacy) {
		trace ("artwork not found, no cover support\n");
	}

	return 0;
}

int ddb_smtc_disconnect() {
	// free current cover if any
	if (cover_data) {
		free(cover_data);
		cover_data = nullptr;
	}
	return 0;
}

static int
ddb_smtc_message(uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2) {
	switch (id) {
	case DB_EV_CONFIGCHANGED:
		if (deadbeef->conf_get_int("smtc.enable", 1)) {
			// NOTE: no proper option to disable completely
			// changing put_IsEnabled breaks MediaPlayer link which
			//  makes no easy way to turn on smtc again
			if (!smtc_enabled) {
				smtc_enabled = 1;
			}
		}
		else {
			if (smtc_enabled) {
				smtc_enabled = 0;
				ppuvc->Stopped();
			}
		}
		break;
	case DB_EV_SONGCHANGED:
		if (smtc_enabled) {
			// do not update if track changed to NULL
			if (((ddb_event_trackchange_t *)ctx)->to != 0) {
				float nextitem_length = deadbeef->pl_get_item_duration(((ddb_event_trackchange_t *)ctx)->to);
				updateSMTC_title();
			}
			else {
				// todo
				// update: change track if it changed to null as this happens when option "Stop after current track/album" is enabled
				ppuvc->Stopped();
			}
		}
		break;
	case DB_EV_PAUSED:
		if (smtc_enabled) {
			ppuvc->Paused(p1);
		}
		break;
	case DB_EV_STOP:
		if (smtc_enabled) {
			ppuvc->Stopped();
		}
		break;
	}
	return 0;
}

static const char settings_dlg[] =
	"property \"Enable\" checkbox smtc.enable 1;\n"
	"property \"Title format\" entry smtc.title_script \"%title%\";\n"
	"property \"Artist format\" entry smtc.artist_script \"%artist%\";\n"
	"property \"Album format\" entry smtc.album_script \"%album%\";\n";

DDBSMTC_API DB_plugin_t * ddb_smtc_load(DB_functions_t *api) {
	deadbeef = api;

	plugin.plugin.api_vmajor = 1;
	plugin.plugin.api_vminor = 10;
	plugin.plugin.version_major = 1;
	plugin.plugin.version_minor = 0;
	plugin.plugin.type = DB_PLUGIN_MISC;
	plugin.plugin.id = "ddb_smtc";
	plugin.plugin.name = "Windows SMTC Integration";
	plugin.plugin.descr = 
		"Implements SystemMediaTransportControls allowing to control the player through native system controls on Windows.\n"
		"This plugin uses PP-UWP library that was released into public domain. It was made by Peter Pawlowski and can be found on:\n"
		"https://perkele.cc/software/UWPInterop \n";
	plugin.plugin.copyright =
		"Windows SMTC Integration for DeaDBeeF\n"
		"Copyright (C) 2021 Jakub Wasylków <kuba_160@protonmail.com>\n"
		"\n"
		"This software is provided 'as-is', without any express or implied\n"
		"warranty.  In no event will the authors be held liable for any damages\n"
		"arising from the use of this software.\n"
		"\n"
		"Permission is granted to anyone to use this software for any purpose,\n"
		"including commercial applications, and to alter it and redistribute it\n"
		"freely, subject to the following restrictions:\n"
		"\n"
		"1. The origin of this software must not be misrepresented; you must not\n"
		" claim that you wrote the original software. If you use this software\n"
		" in a product, an acknowledgment in the product documentation would be\n"
		" appreciated but is not required.\n"
		"\n"
		"2. Altered source versions must be plainly marked as such, and must not be\n"
		" misrepresented as being the original software.\n"
		"\n"
		"3. This notice may not be removed or altered from any source distribution.\n",
	plugin.plugin.website = "http://github.com/DeaDBeeF-for-Windows/ddb_smtc";
	plugin.plugin.start = ddb_smtc_start;
	plugin.plugin.stop = ddb_smtc_stop;
	plugin.plugin.connect = ddb_smtc_connect;
	plugin.plugin.disconnect = ddb_smtc_disconnect;
	plugin.plugin.configdialog = settings_dlg;
	plugin.plugin.message = ddb_smtc_message;

	return DB_PLUGIN(&plugin);
}

#ifdef __cplusplus
}
#endif