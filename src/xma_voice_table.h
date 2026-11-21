// The voice table: which mono XMA sound is a voice (VOICE VOLUME, voice_gain) and which an effect (SFX VOLUME,
// sfx_gain). Voices and effects are both mono 48 kHz XMA in FMOD's FSB4 banks (DataXENON/Sound/*.bnk), so the stream
// parameters cannot tell them apart; the first 2048-byte XMA packet of a sample can (FSB4 sample data is raw XMA
// packets). At start the table is built from the banks' headers and the first packet of each mono sample; at each
// sound start the SDK's XMA decoder (switch/sdk-edits-base.py, src/audio/xma_context.cpp) looks its first packet up.
// Plain C stdio and dirent: the Switch NRO and the host test (tests/xma_voice_table_tests.cpp) build it the same.
#pragma once

#include <stddef.h>
#include <stdint.h>

extern "C" {

// The classes (rex_hcu_xma_gain in src/settings_menu.cpp takes them).
enum { HCU_XMA_SFX = 0, HCU_XMA_MUSIC = 1, HCU_XMA_VOICE = 2 };

// The default voice families of the SE_* banks (sample names that start with one of them), when settings.txt has no
// voice_prefixes line. Every sample of a Voice_* bank is a voice, whatever the list holds.
extern const char* const hcu_voice_default_prefixes;

// Once, before the game's audio starts (switch/hcu_nx_base.cpp): every *.bnk in sound_dir (ending in '/'), read only
// its sample headers and each mono sample's first packet. settings_path: the settings.txt whose last
// voice_prefixes=A,B,... line replaces the default list (an empty value: no SE sample is a voice). Prints one [audio]
// line (keys, voices, conflicts, ms). Returns the number of keys; 0 (no banks): every mono sound stays sfx.
int hcu_voice_table_build(const char* sound_dir, const char* settings_path);

// The key of a sample's first 2048-byte packet (a 64-bit hash of all of it).
uint64_t hcu_xma_packet_key(const uint8_t* packet);

// The class of a key, or -1 when the table does not have it. *name: "bank/sample.wav"; *shared: 1 when other samples
// (other names) start with the same packet (the class is then voice only if all of them are voices, or one is in a
// Voice_* bank).
int hcu_voice_lookup(uint64_t key, const char** name, int* shared);

// The SDK's XMA decoder at a sound start: fresh = PrepareDecoder returned 1 (a new codec: a new sound), else the
// buffer's first frame with the codec kept (a stream's next buffer, a loop to the start, or a new sound in a context
// the game did not clear). packet: the raw guest packet being decoded; cls / *key: the context's latched class and
// key. Returns the new class: stereo is music; a fresh miss is sfx; a non-fresh miss, or the same key again, keeps
// cls. Logs the start (rate-limited) and counts it for the [audio] line.
int rex_hcu_xma_start(const uint8_t* packet, int fresh, int stereo, int cls, uint64_t* key, uint32_t packet_index,
                      uint32_t bit_offset);

// This second's start counters for the [audio] line (" starts N hit N miss N ..."), then 0 again. Returns the length
// (0: no starts this second).
int hcu_voice_stats(char* buf, size_t size);
}
