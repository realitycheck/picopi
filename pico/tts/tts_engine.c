#include "tts_engine.h"
#include "langfiles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <picoapi.h>
#include <picodefs.h>
#include <assert.h>

#if 0 // enable for debugging
#define PICO_DBG(...) fprintf(stderr, __VA_ARGS__)
#else
#define PICO_DBG(...)
#endif

#define PICO_MEM_SIZE       3 * 1024 * 1024
/* speech rate    */
#define PICO_MIN_RATE        20
#define PICO_MAX_RATE       500
#define PICO_DEF_RATE       100
/* speech pitch   */
#define PICO_MIN_PITCH       50
#define PICO_MAX_PITCH      200
#define PICO_DEF_PITCH      100
/* speech volume */
#define PICO_MIN_VOL          0
#define PICO_MAX_VOL        500
#define PICO_DEF_VOL        100

#define MAX_OUTBUF_SIZE     128
#define SYNTH_BUFFER_SIZE   (128 * 1024)

static const char * PICO_VOICE_NAME                = "PicoVoice";

struct sTTS_Engine {
	tts_callback_t  synth_callback;
	void *          pico_mem_pool;
	pico_System     pico_sys;
	pico_Resource   pico_ta;
	pico_Resource   pico_sg;
	pico_Resource   pico_utpp;
	pico_Engine     pico_engine;
	char *  current_language;
	char * languages_path;
	uint8_t *synthesis_buffer;
	int     current_rate;
	int     current_pitch;
	int     current_volume;
	bool    synthesis_abort_flag;
};

/* Local helper functions */
static bool is_readable(const char *filename);
static bool load_language(TTS_Engine *engine, const char *lang);
static const char *add_properties(TTS_Engine *engine, const char *text);
static int clamp(int val, int min_val, int max_val);

TTS_Engine *TtsEngine_Create(const char *lang_dir, const char *language, tts_callback_t cb)
{
	TTS_Engine *engine = NULL;
	
	if (!cb || !language || !lang_dir || strlen(lang_dir) <= 0) {
		PICO_DBG("%s: Invalid parameter\n", __FUNCTION__);
		return NULL;
	}

	PICO_DBG("TtsEngine_Create: lang:%s dir:%s\n", language, lang_dir);
	engine = (TTS_Engine *) calloc(1, sizeof(TTS_Engine));
	engine->current_pitch = PICO_DEF_PITCH;
	engine->current_rate = PICO_DEF_RATE;
	engine->current_volume = PICO_DEF_VOL;

	engine->pico_mem_pool = calloc(PICO_MEM_SIZE , 1);
	if (!engine->pico_mem_pool) {
		PICO_DBG("Failed to allocate memory for Pico system\n");
		TtsEngine_Destroy(engine);
		return NULL;
	}

	if (pico_initialize(engine->pico_mem_pool, PICO_MEM_SIZE, &engine->pico_sys) != PICO_OK) {
		PICO_DBG("pico_initialize failed\n");
		TtsEngine_Destroy(engine);
		return NULL;
	}

	engine->synth_callback = cb;
	engine->languages_path = strdup(lang_dir);
	engine->synthesis_buffer = (uint8_t *) malloc(SYNTH_BUFFER_SIZE);
	if (!engine->synthesis_buffer) {
		PICO_DBG("Failed to allocate synth buffer\n");
		TtsEngine_Destroy(engine);
		return NULL;
	}

	if (!load_language(engine, language)) {
		PICO_DBG("load_language %s failed\n", language);
		TtsEngine_Destroy(engine);
		return NULL;
	}

	return engine;
}

int TtsEngine_SetRate(TTS_Engine *engine, int rate)
{
	assert(engine);
	engine->current_rate = clamp(rate, PICO_MIN_RATE, PICO_MAX_RATE);
	return engine->current_rate;
}

int TtsEngine_GetRate(const TTS_Engine *engine)
{
	assert(engine);
	return engine->current_rate;
}

int TtsEngine_SetVolume(TTS_Engine *engine, int vol)
{
	assert(engine);
	engine->current_volume = clamp(vol, PICO_MIN_VOL, PICO_MAX_VOL);
	return engine->current_volume;
}

int TtsEngine_GetVolume(const TTS_Engine *engine)
{
	assert(engine);
	return engine->current_volume;
}

int TtsEngine_SetPitch(TTS_Engine *engine, int pitch)
{
	assert(engine);
	engine->current_pitch = clamp(pitch, PICO_MIN_PITCH, PICO_MAX_PITCH);
	return engine->current_pitch;
}

int TtsEngine_GetPitch(const TTS_Engine *engine)
{
	assert(engine);
	return engine->current_pitch;
}

void TtsEngine_Stop(TTS_Engine *engine)
{
	assert(engine);
	engine->synthesis_abort_flag = true;
}

bool TtsEngine_Speak(TTS_Engine *engine, const char *text, void *userdata)
{
	uint8_t    *buffer = NULL;
	bool        cont = true;
	pico_Char * inp = NULL;
	const char * local_text = NULL;
	short       outbuf[MAX_OUTBUF_SIZE/2];
	pico_Int16  bytes_sent, bytes_recv, text_remaining, out_data_type;
	pico_Status ret;
	bool success = false;
	uint32_t rate = 16000;
	uint32_t depth = 16;
	int channels = 1;
	size_t bufused = 0;

	assert(engine);
	assert(text);

	if (!text || !engine) {
		PICO_DBG("Invalid argument\n");
		return false;
	}

	engine->synthesis_abort_flag = false;
	buffer = engine->synthesis_buffer;

	if (strlen(text) == 0) {
		return true;
	}

	/* Add property tags to the string - if any.    */
	local_text = add_properties(engine, text);

	text_remaining = strlen((const char *) local_text) + 1;

	inp = (pico_Char *) local_text;

	/* synthesis loop   */
	while (text_remaining) {
		if (engine->synthesis_abort_flag) {
			ret = pico_resetEngine(engine->pico_engine, PICO_RESET_SOFT);
			break;
		}

		/* Feed the text into the engine.   */
		ret = pico_putTextUtf8(engine->pico_engine, inp, text_remaining, &bytes_sent);
		if (ret != PICO_OK) {
			PICO_DBG("Error synthesizing string '%s': [%d]\n", text, ret);
			goto cleanup;
		}

		text_remaining -= bytes_sent;
		inp += bytes_sent;
		do {
			if (engine->synthesis_abort_flag) {
				ret = pico_resetEngine(engine->pico_engine, PICO_RESET_SOFT);
				break;
			}
			/* Retrieve the samples and add them to the buffer. */
			ret = pico_getData(engine->pico_engine, (void *) outbuf, MAX_OUTBUF_SIZE, &bytes_recv,
							   &out_data_type);
			if (bytes_recv) {
				if ((bufused + bytes_recv) <= SYNTH_BUFFER_SIZE) {
					memcpy(buffer+bufused, outbuf, bytes_recv);
					bufused += bytes_recv;
				} else {
					/* The buffer filled; pass this on to the callback function.    */
					cont = engine->synth_callback(userdata, rate, depth, channels, buffer, bufused, false);
					if (!cont) {
						PICO_DBG("Halt requested by caller. Halting.\n");
						engine->synthesis_abort_flag = true;
						ret = pico_resetEngine(engine->pico_engine, PICO_RESET_SOFT);
						break;
					}
					bufused = 0;
					memcpy(buffer, (int8_t *) outbuf, bytes_recv);
					bufused += bytes_recv;
				}
			}
		} while (PICO_STEP_BUSY == ret);

		if (!engine->synthesis_abort_flag) {
			/* Pass any remaining samples. */
			engine->synth_callback(userdata, rate, depth, channels, buffer, bufused, false);
			bufused = 0;
		}

		if (ret != PICO_STEP_IDLE) {

			PICO_DBG("Error occurred during synthesis [%d]\n", ret);
			PICO_DBG("Synth loop: sending TTS_SYNTH_DONE after error\n");
			bufused = 0;
			engine->synth_callback(userdata, rate, depth, channels, buffer, bufused, true);
			pico_resetEngine(engine->pico_engine, PICO_RESET_SOFT);
			goto cleanup;
		}
	}

	/* Synthesis is done; notify the caller */
	PICO_DBG("Synth loop: sending TTS_SYNTH_DONE after all done, or was asked to stop\n");
	engine->synth_callback(userdata, rate, depth, channels, buffer, bufused, true);

	success = true;

cleanup:
	engine->synthesis_abort_flag = false;
	if (local_text != text) {
		free((void*) local_text);
	}
	return success;
}

void TtsEngine_Destroy(TTS_Engine *engine)
{
	if (!engine) {
		return;
	}

	if (engine->pico_engine) {
		pico_disposeEngine(engine->pico_sys, &engine->pico_engine);
		pico_releaseVoiceDefinition(engine->pico_sys, (pico_Char *) PICO_VOICE_NAME);
	}

	if (engine->pico_utpp) {
		pico_unloadResource(engine->pico_sys, &engine->pico_utpp);
		engine->pico_utpp = NULL;
	}

	if (engine->pico_ta) {
		pico_unloadResource(engine->pico_sys, &engine->pico_ta);
		engine->pico_ta = NULL;
	}

	if (engine->pico_sg) {
		pico_unloadResource(engine->pico_sys, &engine->pico_sg);
		engine->pico_sg = NULL;
	}

	if (engine->pico_sys) {
		pico_terminate(&engine->pico_sys);
		engine->pico_sys = NULL;
	}

	free(engine->pico_mem_pool);
	free(engine->current_language);
	free(engine->languages_path);
	free(engine->synthesis_buffer);
	free(engine);
}

static bool load_language(TTS_Engine *engine, const char *lang)
{
	pico_Status ret;
	bool success = false;
	pico_Char resource_name_ta[PICO_MAX_RESOURCE_NAME_SIZE];
	pico_Char resource_name_sg[PICO_MAX_RESOURCE_NAME_SIZE];
	pico_Char resource_name_utpp[PICO_MAX_RESOURCE_NAME_SIZE];
	const pico_Char *fname_utpp = (const pico_Char *) "dummy.bin";
	Lang_Filenames lf;

	lang_files_find(&lf, engine->languages_path, lang);

	if (!is_readable(lf.fname_ta)) {
		PICO_DBG("%s is not readable.\n", lf.fname_ta);
		goto cleanup;
	}

	if (!is_readable(lf.fname_sg)) {
		PICO_DBG("%s is not readable.\n", lf.fname_sg);
		goto cleanup;
	}

	/* Load the text analysis Lingware resource file.   */
	ret = pico_loadResource(engine->pico_sys, (const pico_Char *) lf.fname_ta, &engine->pico_ta);
	if (PICO_OK != ret) {
		PICO_DBG("Failed to load textana resource for %s [%d]\n", lang, ret);
		goto cleanup;
	}

	/* Load the signal generation Lingware resource file.   */
	ret = pico_loadResource(engine->pico_sys, (const pico_Char *) lf.fname_sg, &engine->pico_sg);
	if (PICO_OK != ret) {
		PICO_DBG("Failed to load siggen resource for %s [%d]\n", lang, ret);
		goto cleanup;
	}

	/* Load the utpp Lingware resource file if exists - NOTE: this file is optional
	   and is currently not used. Loading is only attempted for future compatibility.
	   If this file is not present the loading will still succeed.                      */
	if (lf.fname_utpp) fname_utpp = (const pico_Char *) lf.fname_utpp;
	ret = pico_loadResource(engine->pico_sys, fname_utpp, &engine->pico_utpp);
	if ((PICO_OK != ret) && (ret != PICO_EXC_CANT_OPEN_FILE)) {
		PICO_DBG("Failed to load utpp resource for %s [%d]\n", lang, ret);
		goto cleanup;
	}

	free(engine->current_language);
	engine->current_language = strdup(lang);

	/* Get the text analysis resource name. */
	ret = pico_getResourceName(engine->pico_sys, engine->pico_ta, (char *) resource_name_ta);
	if (PICO_OK != ret) {
		PICO_DBG("Failed to get textana resource name for %s [%d]\n", lang, ret);
		goto cleanup;
	}

	/* Get the signal generation resource name. */
	ret = pico_getResourceName(engine->pico_sys, engine->pico_sg, (char *) resource_name_sg);
	if ((PICO_OK == ret) && (engine->pico_utpp != NULL)) {
		/* Get utpp resource name - optional: see note above.   */
		ret = pico_getResourceName(engine->pico_sys, engine->pico_utpp, (char *) resource_name_utpp);
		if (PICO_OK != ret)  {
			goto cleanup;
		}
	}

	if (PICO_OK != ret) {
		PICO_DBG("Failed to get siggen resource name for %s [%d]\n", lang, ret);
		goto cleanup;
	}

	/* Create a voice definition.   */
	ret = pico_createVoiceDefinition(engine->pico_sys, (const pico_Char *) PICO_VOICE_NAME);
	if (PICO_OK != ret) {
		PICO_DBG("Failed to create voice for %s [%d]\n", lang, ret);
		goto cleanup;
	}

	/* Add the text analysis resource to the voice. */
	ret = pico_addResourceToVoiceDefinition(engine->pico_sys, (const pico_Char *) PICO_VOICE_NAME, resource_name_ta);
	if (PICO_OK != ret) {
		PICO_DBG("Failed to add textana resource to voice for %s [%d]\n", lang, ret);
		goto cleanup;
	}

	/* Add the signal generation resource to the voice. */
	ret = pico_addResourceToVoiceDefinition(engine->pico_sys, (const pico_Char *) PICO_VOICE_NAME, resource_name_sg);
	if ((PICO_OK == ret) && (engine->pico_utpp != NULL)) {
		/* Add utpp resource to voice - optional: see note above.   */
		ret = pico_addResourceToVoiceDefinition(engine->pico_sys, (const pico_Char *) PICO_VOICE_NAME, resource_name_utpp);
		if (PICO_OK != ret) {
			PICO_DBG("Failed to add utpp resource to voice for %s [%d]\n", lang, ret);
			goto cleanup;
		}
	}

	if (PICO_OK != ret) {
		PICO_DBG("Failed to add siggen resource to voice for %s [%d]\n", lang, ret);
		goto cleanup;
	}

	ret = pico_newEngine(engine->pico_sys, (const pico_Char *) PICO_VOICE_NAME, &engine->pico_engine);
	if (PICO_OK != ret) {
		PICO_DBG("Failed to create engine for %s [%d]\n", lang, ret);
		goto cleanup;
	}

	success = true;
	PICO_DBG("%s loaded successfully\n", lang);

cleanup:
	lang_files_release(&lf);
	return success;
}

static bool is_readable(const char *filename)
{
	FILE *fp = NULL;
	if (!filename)
		return false;

	fp = fopen(filename, "rb");
	if (!fp) {
		return false;
	}
	fclose(fp);
	return true;
}

static const char *add_properties(TTS_Engine *engine, const char *text)
{
	const size_t max_tags_len = 256;
	size_t new_len;
	bool set_pitch = (engine->current_pitch != PICO_DEF_PITCH);
	bool set_rate = (engine->current_rate != PICO_DEF_RATE);
	bool set_volume = (engine->current_volume != PICO_DEF_VOL);
	char *new_text = NULL;
	if (!set_pitch && !set_rate && !set_volume)
		return text;

	new_len = strlen(text) + max_tags_len;
	new_text = (char*) malloc(new_len);
#ifdef _WIN32
	_snprintf_s(new_text, new_len, _TRUNCATE,
			 "<speed level='%4d'><pitch level='%4d'><volume level='%4d'>%s</volume></pitch></speed>",
			 engine->current_rate, engine->current_pitch, engine->current_volume, text);
#else
	snprintf(new_text, new_len,
		"<speed level='%4d'><pitch level='%4d'><volume level='%4d'>%s</volume></pitch></speed>",
		engine->current_rate, engine->current_pitch, engine->current_volume, text);
#endif
	return new_text;
}

static int clamp(int val, int min_val, int max_val)
{
	if (val < min_val) {
		val = min_val;
	}
	else if (val > max_val) {
		val = max_val;
	}
	return val;
}
