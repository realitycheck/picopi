#ifndef TTS_ENGINE_H
#define TTS_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
struct sTTS_Engine;
typedef struct sTTS_Engine TTS_Engine;

// Callback used to return audio chunks as they are being synthesized.
// Return false to stop the synthesis or true to continue.
typedef bool (tts_callback_t)(void *user, uint32_t rate, uint32_t format, int channels, uint8_t *audio, uint32_t audio_bytes, bool final);

// Create TextToSpeech engine handle
TTS_Engine *TtsEngine_Create(const char *lang_dir, const char *language, tts_callback_t cb);

int TtsEngine_SetRate(TTS_Engine *engine, int rate);

int TtsEngine_GetRate(const TTS_Engine *engine);

int TtsEngine_SetPitch(TTS_Engine *engine, int pitch);

int TtsEngine_GetPitch(const TTS_Engine *engine);

void TtsEngine_Stop(TTS_Engine *engine);

bool TtsEngine_Speak(TTS_Engine *engine, const char *text, void *userdata);

void TtsEngine_Destroy(TTS_Engine *engine);

#ifdef __cplusplus
}
#endif

#endif
