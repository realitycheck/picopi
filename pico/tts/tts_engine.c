#include "tts_engine.h"

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

#define PICO_MEM_SIZE       2 * 1024 * 1024
/* speaking rate    */
#define PICO_MIN_RATE        20
#define PICO_MAX_RATE       500
#define PICO_DEF_RATE       100
/* speaking pitch   */
#define PICO_MIN_PITCH       50
#define PICO_MAX_PITCH      200
#define PICO_DEF_PITCH      100

#define MAX_OUTBUF_SIZE     128
#define SYNTH_BUFFER_SIZE   (128 * 1024)

static const char * PICO_VOICE_NAME                = "PicoVoice";

/* supported voices */
static const char * pico_languages[]      = { "en-US",            "en-GB",            "de-DE",            "es-ES",            "fr-FR",            "it-IT" };
static const char * pico_ta_files[]       = { "en-US_ta.bin",     "en-GB_ta.bin",     "de-DE_ta.bin",     "es-ES_ta.bin",     "fr-FR_ta.bin",     "it-IT_ta.bin" };
static const char * pico_sq_files[]       = { "en-US_lh0_sg.bin", "en-GB_kh0_sg.bin", "de-DE_gl0_sg.bin", "es-ES_zl0_sg.bin", "fr-FR_nk0_sg.bin", "it-IT_cm0_sg.bin" };
static const char * pico_utpp_files[]     = { "en-US_utpp.bin",   "en-GB_utpp.bin",   "de-DE_utpp.bin",   "es-ES_utpp.bin",   "fr-FR_utpp.bin",   "it-IT_utpp.bin" };
static const int pico_num_voices          = 6;

struct sTTS_Engine {
	tts_callback_t * synth_callback;
	void *          pico_mem_pool;
	pico_System     pico_sys;
	pico_Resource   pico_ta;
	pico_Resource   pico_sq;
	pico_Resource   pico_utpp;
	pico_Engine     pico_engine;
	char *  current_language;
	int     current_rate;
	int     current_pitch;
	char * languages_path;
	uint8_t *synthesis_buffer;
	bool    synthesis_abort_flag;
};

/* Local helper functions */
static int find_language_index(const char *lang);
static bool is_readable(const pico_Char *filename);
static char * path_join(const char *dir, const char *filename);
static bool load_language(TTS_Engine *engine, const char *lang);
static const char *add_properties(TTS_Engine *engine, const char *text);

TTS_Engine *TtsEngine_Create(const char *lang_dir, const char *language, tts_callback_t cb)
{
	if (!cb || !language || !lang_dir || strlen(lang_dir) <= 0) {
		PICO_DBG("%s: Invalid parameter\n", __FUNCTION__);
		return NULL;
	}

	TTS_Engine *engine = (TTS_Engine *) calloc(1, sizeof(TTS_Engine));
	engine->current_pitch = PICO_DEF_PITCH;
	engine->current_rate = PICO_DEF_RATE;

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
	if (rate >= PICO_MIN_RATE && rate <= PICO_MAX_RATE) {
		engine->current_rate = rate;
	}
	return engine->current_rate;
}

int TtsEngine_GetRate(const TTS_Engine *engine)
{
	assert(engine);
	return engine->current_rate;
}

int TtsEngine_SetPitch(TTS_Engine *engine, int pitch)
{
	assert(engine);
	if (pitch >= PICO_MIN_PITCH && pitch <= PICO_MAX_PITCH) {
		engine->current_pitch = pitch;
	}
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
	assert(engine);

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

	if (engine->pico_sq) {
		pico_unloadResource(engine->pico_sys, &engine->pico_sq);
		engine->pico_sq = NULL;
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
	pico_Char resource_name_sq[PICO_MAX_RESOURCE_NAME_SIZE];
	pico_Char resource_name_utpp[PICO_MAX_RESOURCE_NAME_SIZE];
	pico_Char *fname_ta = NULL, *fname_sq = NULL, *fname_utpp = NULL;

	int lang_index = find_language_index(lang);
	if (lang_index < 0) {
		PICO_DBG("Unsupported language: %s\n", lang);
		goto cleanup;
	}

	engine->current_language = strdup(lang);
	fname_ta = (pico_Char *) path_join(engine->languages_path, pico_ta_files[lang_index]);
	fname_sq = (pico_Char *) path_join(engine->languages_path, pico_sq_files[lang_index]);
	fname_utpp = (pico_Char *) path_join(engine->languages_path, pico_utpp_files[lang_index]);

	if (!is_readable(fname_ta)) {
		PICO_DBG("%s is not readable.\n", engine->picoTaFileName);
		goto cleanup;
	}

	if (!is_readable(fname_sq)) {
		PICO_DBG("%s is not readable.\n", engine->picoSgFileName);
		goto cleanup;
	}

	/* Load the text analysis Lingware resource file.   */
	ret = pico_loadResource(engine->pico_sys, fname_ta, &engine->pico_ta);
	if (PICO_OK != ret) {
		PICO_DBG("Failed to load textana resource for %s [%d]\n", pico_languages[lang_index], ret);
		goto cleanup;
	}

	/* Load the signal generation Lingware resource file.   */
	ret = pico_loadResource(engine->pico_sys, fname_sq, &engine->pico_sq);
	if (PICO_OK != ret) {
		PICO_DBG("Failed to load siggen resource for %s [%d]\n", pico_languages[lang_index], ret);
		goto cleanup;
	}

	/* Load the utpp Lingware resource file if exists - NOTE: this file is optional
	   and is currently not used. Loading is only attempted for future compatibility.
	   If this file is not present the loading will still succeed.                      */
	ret = pico_loadResource(engine->pico_sys, fname_utpp, &engine->pico_utpp);
	if ((PICO_OK != ret) && (ret != PICO_EXC_CANT_OPEN_FILE)) {
		PICO_DBG("Failed to load utpp resource for %s [%d]\n", pico_languages[lang_index], ret);
		goto cleanup;
	}

	/* Get the text analysis resource name.     */
	ret = pico_getResourceName(engine->pico_sys, engine->pico_ta, (char *) resource_name_ta);
	if (PICO_OK != ret) {
		PICO_DBG("Failed to get textana resource name for %s [%d]\n", pico_languages[lang_index], ret);
		goto cleanup;
	}

	/* Get the signal generation resource name. */
	ret = pico_getResourceName(engine->pico_sys, engine->pico_sq, (char *) resource_name_sq);
	if ((PICO_OK == ret) && (engine->pico_utpp != NULL)) {
		/* Get utpp resource name - optional: see note above.   */
		ret = pico_getResourceName(engine->pico_sys, engine->pico_utpp, (char *) resource_name_utpp);
		if (PICO_OK != ret)  {
			goto cleanup;
		}
	}

	if (PICO_OK != ret) {
		PICO_DBG("Failed to get siggen resource name for %s [%d]\n", pico_languages[lang_index], ret);
		goto cleanup;
	}

	/* Create a voice definition.   */
	ret = pico_createVoiceDefinition(engine->pico_sys, (const pico_Char *) PICO_VOICE_NAME);
	if (PICO_OK != ret) {
		PICO_DBG("Failed to create voice for %s [%d]\n", pico_languages[lang_index], ret);
		goto cleanup;
	}

	/* Add the text analysis resource to the voice. */
	ret = pico_addResourceToVoiceDefinition(engine->pico_sys, (const pico_Char *) PICO_VOICE_NAME, resource_name_ta);
	if (PICO_OK != ret) {
		PICO_DBG("Failed to add textana resource to voice for %s [%d]\n", pico_languages[lang_index], ret);
		goto cleanup;
	}

	/* Add the signal generation resource to the voice. */
	ret = pico_addResourceToVoiceDefinition(engine->pico_sys, (const pico_Char *) PICO_VOICE_NAME, resource_name_sq);
	if ((PICO_OK == ret) && (engine->pico_utpp != NULL)) {
		/* Add utpp resource to voice - optional: see note above.   */
		ret = pico_addResourceToVoiceDefinition(engine->pico_sys, (const pico_Char *) PICO_VOICE_NAME, resource_name_utpp);
		if (PICO_OK != ret) {
			PICO_DBG("Failed to add utpp resource to voice for %s [%d]\n", pico_languages[lang_index], ret);
			goto cleanup;
		}
	}

	if (PICO_OK != ret) {
		PICO_DBG("Failed to add siggen resource to voice for %s [%d]\n", pico_languages[lang_index], ret);
		goto cleanup;
	}

	ret = pico_newEngine(engine->pico_sys, (const pico_Char *) PICO_VOICE_NAME, &engine->pico_engine);
	if (PICO_OK != ret) {
		PICO_DBG("Failed to create engine for %s [%d]\n", pico_languages[lang_index], ret);
		goto cleanup;
	}

	success = true;
	PICO_DBG("%s loaded successfully\n", engine->current_language);

cleanup:
	free(fname_ta);
	free(fname_sq);
	free(fname_utpp);
	return success;
}

static int find_language_index(const char *lang)
{
	int i;
	for (i = 0; i < pico_num_voices; i++) {
		if (strcasecmp(lang, pico_languages[i]) == 0) {
			return i;
		}
	}
	return -1;
}

static char * path_join(const char *dir, const char *filename)
{
	size_t len1 = strlen(dir);
	size_t len2 = strlen(filename);
	size_t buf_size = len1 + len2 + 2;
	char * buf = (char *) malloc(buf_size);
	char * p = buf;
	memcpy(buf, dir, len1 + 1);
	p += len1;

	// Add dir separator if needed
	if (len1 > 0 &&  dir[len1-1] != '/')
		strcat(p, "/");
	strcat(p, filename);
	return buf;
}

static bool is_readable(const pico_Char *filename)
{
	FILE *fp = fopen((const char *)filename, "rb");
	if (fp) {
		fclose(fp);
		return true;
	}
	return false;
}

static const char *add_properties(TTS_Engine *engine, const char *text)
{
	const size_t max_tags_len = 512;
	bool set_pitch = (engine->current_pitch != PICO_DEF_PITCH);
	bool set_rate = (engine->current_rate != PICO_DEF_RATE);
	if (!set_pitch && !set_rate)
		return text;

	size_t new_len = strlen(text) + max_tags_len;
	char *new_text = (char*) malloc(new_len);
	snprintf(new_text, new_len,
			 "<speed level='%4d'><pitch level='%4d'>%s</pitch></speed>",
			 engine->current_rate, engine->current_pitch, text);
	return new_text;
}
