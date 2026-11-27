// C ABI around the C++ engine. Swift (and any other host) talks to the DSP only through this header.
// Realtime rules: vv_engine_set_param/get_param are safe from any thread; everything else except create/destroy/prepare
// must be called from the audio thread.
#ifndef VV_ENGINE_H
#define VV_ENGINE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct VVEngine VVEngine;

VVEngine* vv_engine_create(double sample_rate);              // allocates everything (not realtime safe)
void      vv_engine_destroy(VVEngine* e);
double    vv_engine_sample_rate(VVEngine* e);
void      vv_engine_prepare(VVEngine* e, double sample_rate); // call when the host changes sample rate (not realtime safe)
void      vv_engine_reset(VVEngine* e);

uint32_t  vv_param_count(void);
uint32_t  vv_param_id_at(uint32_t index);                    // index 0..count-1 -> persistent id
const char* vv_param_address(uint32_t id);
void      vv_engine_set_param(VVEngine* e, uint32_t id, float value);
float     vv_engine_get_param(VVEngine* e, uint32_t id);
/* Mixer window meters: peak (linear, 0..~4, ~300 ms fall-off) of track `index` (0..23: WT1 WT2 FMA FMB FM6OP SMP1..16 PRC1 PRC2 SID). Safe from any thread. */
/* Layer SOLO / MUTE (web buttons). Bit l = layer l: 0 WT, 1 FM, 2 SMP, 3 PRO, 4 SID. */
void      vv_engine_set_solo_mute(VVEngine* e, int solo_mask, int mute_mask);
/* ---- Authoring tools (non-realtime; call from the UI or a worker thread, never from the audio callback) ----
   Wavetable frames are 1024 floats. Each wt function returns the number of frames written (0 = failure; err gets the message). */
int       vv_wt_harmonics(const float* amps, int n, float* out1024);
int       vv_wt_formula(const char* expr, int frames, float* out, int max_frames, char* err, int err_cap);
int       vv_wt_curve(const float* pts_xycxcy, int n_pts, float* out1024);           /* LFO-style bezier curve (x,y,cx,cy per point) -> one frame, 0..1 -> -1..1 */
int       vv_wt_slice(const float* mono, int len, int frames, float* out, int max_frames);   /* imported audio sliced into frames (web sliceBufferIntoWavetableFrames) */
/* Audio handles for the sample tools. */
typedef struct VVAudio VVAudio;
VVAudio*  vv_audio_create(int sample_rate, int channels, int length, const float* left, const float* right);   /* right may be NULL (mono) */
VVAudio*  vv_audio_from_wav(const uint8_t* wav, size_t length);
void      vv_audio_free(VVAudio* a);
int       vv_audio_length(const VVAudio* a);
int       vv_audio_channels(const VVAudio* a);
int       vv_audio_sample_rate(const VVAudio* a);
void      vv_audio_read(const VVAudio* a, int channel, float* dst);
size_t    vv_audio_to_wav16(const VVAudio* a, uint8_t* buf, size_t cap);   /* returns the byte size; writes only if cap is large enough */
VVAudio*  vv_audio_stretch(const VVAudio* a, int mode /*0 ratio 1 pitch 2 fit-to-beats*/, double ratio, double semitones, double beats, double bpm, int quality /*0 perc 1 normal 2 smooth*/);
VVAudio*  vv_audio_warp(const VVAudio* a, const double* markers_orig_now, int n_markers);   /* pairs (orig 0..1, now 0..1) */
VVAudio*  vv_audio_from_image(const uint8_t* rgba, int w, int h, int sample_rate, double duration, int note, int harmonics, double tilt, double gamma, int invert, int loop, int rgb_stereo);   /* image -> audio: columns = time, rows = harmonics */
VVAudio*  vv_audio_spectral(const VVAudio* a, int granular, int log_scale, int window /*0 hann 1 hamming 2 blackman 3 rect*/, const double* bands, int n_bands);
/* Chord analyzer: writes the current chord name (UTF-8, NUL-terminated) and returns its length; "" until a chord has been recognised. UI thread only. */
int       vv_engine_chord_label(VVEngine* e, char* buf, int cap);
float     vv_engine_mixer_level(VVEngine* e, int index);
float     vv_engine_lfo_phase(VVEngine* e, int index);      /* R11: lock-free LFO phase 0..1 snapshot (any thread) */
float     vv_engine_lfo_rate_hz(VVEngine* e, int index);    /* R11: LFO rate for UI extrapolation between ticks; 0 while held */
/* Scope lanes: capture oscillator `group` of `layer` (0 WT, 1 FM, 2 SMP, 3 PRO, 4 SID; FM groups are the two engines) when its bit is set in `groups`; read the newest n (<= 2048) samples. */
void     vv_engine_scope_tap_mask(VVEngine* e, int layer, unsigned groups);
void     vv_engine_set_display_latency(VVEngine* e, int samples); /* output latency (samples): sequencer / roll / drum playheads read this far in the past so the picture matches the sound */
int      vv_engine_scope_tap_read(VVEngine* e, int layer, int group, float* out, int n);
float     vv_engine_sample_head(VVEngine* e, int slot);   /* sampler playhead of a slot as a fraction of its source buffer, -1 when idle (any thread) */
// Host automation: set_param only stores + flags (render-thread safe). Call pump_params ~30 Hz from a non-realtime timer to apply
// changes to the preset. sync_params_from_preset moves host-visible values to the loaded preset; then re-read with get_param.
int       vv_engine_pump_params(VVEngine* e);
int       vv_engine_sync_params_from_preset(VVEngine* e);

void      vv_engine_note_on(VVEngine* e, int note, float velocity);
void      vv_engine_note_off(VVEngine* e, int note);
/* MPC pads: slot >= 0 plays that sampler slot at its original pitch (layer 2 = SMP); slot < 0 plays the layer at pitchNote. pad = 0..15. */
void      vv_engine_pad_slice_on(VVEngine* e, int slot, int slice, int pad, float velocity);   /* MPC pad plays transient `slice` (1-based, 0 = whole) of sampler slot `slot`; release with vv_engine_pad_off(e, 2, pad) */
void      vv_engine_pad_on(VVEngine* e, int layer, int slot, int pad, int pitchNote, float velocity);
void      vv_engine_pad_off(VVEngine* e, int layer, int pad);
/* Sample-editor audition: slot 0..15, slice 0 = whole start..end region, 1..N = that transient slice (with its reverse / pan). Release with vv_engine_audition_off. */
void      vv_engine_audition_slice(VVEngine* e, int slot, int slice, float velocity);
void      vv_engine_audition_off(VVEngine* e);
void      vv_engine_all_notes_off(VVEngine* e);
/* MIDI CC to parameter (web CC map + MIDI LEARN). Incoming CCs are only recorded on the audio thread: call vv_engine_poll_cc from a normal timer (~30 Hz). */
int       vv_engine_poll_cc(VVEngine* e);                       /* returns the number of CCs handled */
void      vv_engine_cc_learn_arm(VVEngine* e, const char* path, double lo, double hi);   /* the next CC (not 1 / 7) maps to path, scaled lo..hi */
void      vv_engine_cc_learn_cancel(VVEngine* e);
int       vv_engine_cc_learned(VVEngine* e);                    /* CC number mapped by the last learn, -1 = none yet */
int       vv_engine_cc_last(VVEngine* e);                       /* last CC number received, -1 = none */
void      vv_engine_cc_learn_clear(VVEngine* e);
size_t    vv_engine_cc_learn_json(VVEngine* e, char* buf, size_t cap);   /* {"<cc>":{"path":..,"lo":..,"hi":..}} */
int       vv_engine_cc_learn_set_json(VVEngine* e, const char* json);
void      vv_engine_midi(VVEngine* e, const uint8_t* bytes, int length);
void      vv_engine_set_transport(VVEngine* e, double bpm, int playing);
/* Plug-in host transport (AUv3): call once per render block. valid = 0 returns to the standalone transport. beat is the host playhead in quarter notes;
   the piano roll, automation lanes, arpeggiator and sequencers follow the host's play state, tempo and position. */
double    vv_engine_transport_beat(VVEngine* e);   /* current transport playhead, quarter notes (UI display) */
void      vv_engine_set_host_transport(VVEngine* e, int valid, int playing, double beat, double bpm);

// ---- full instrument (Synth + arpeggiator + sequencer). Loading calls are NOT realtime safe (call from a background queue);
//      they are staged and picked up by the next render. Everything below is thread safe.
int       vv_engine_load_preset_json(VVEngine* e, const char* json, size_t length);   // 1 ok, 0 parse failure. The JSON is the web app's snapshotState() format
int       vv_engine_load_wavetable(VVEngine* e, const char* name, const uint8_t* vvwt, size_t length); // .vvwt bytes
int       vv_engine_load_sample_wav(VVEngine* e, const char* name, const uint8_t* wav, size_t length); // .wav bytes (8/16/24/32-bit, float)
// Live edit by dot path into the preset JSON, e.g. path "layers.WT.oscs.0.level", value_json "0.5" / "true" / "\"sawBright\"" / "[1,2]".
// Applied without resetting the sound. Returns 1 on success.
int       vv_engine_set_value(VVEngine* e, const char* path, const char* value_json);
// Several edits in one preset update: object_json is {"path": value, ...}. Returns 1 on success.
int       vv_engine_set_values(VVEngine* e, const char* object_json);
// Copies the JSON text of a path (or the whole preset when path is "") into buf; returns the full length (call with buf=NULL to size it).
size_t    vv_engine_get_value(VVEngine* e, const char* path, char* buf, size_t buf_len);
// Global (non-preset) settings: "masterVolume" "masterTone" "wtFmMix" "sampleMix" "sampleMixOn" "bendRange". Returns 1 if the name is known.
/* XFX MODE: a different sound on every key. Edits extra.xfx of the working preset (the web app's own JSON, so presets travel both ways).
   action: "enable" {on} | "bank" {index 0..7} | "rename" {name} | "setOverride" {note,path,value} | "clearOverride" {note,path}
           | "layer" {note,layer:"WT"|"FM"|"SMP"|"PRO"|"SID",state:true|false|null} | "clearKey" {note} | "copyKey" {from,to} | "clearBank" {} | "reset" {}
   path = "<LAYER>.<param path>" e.g. "WT.env.rep", "WT.oscs.0.filter.cutoff". Returns 1 ok / 0 rejected. Not realtime-safe (main thread). */
int       vv_engine_xfx_action(VVEngine* e, const char* action, const char* args_json);
size_t    vv_engine_xfx_export(VVEngine* e, char* buf, size_t buf_len);   /* extra.xfx as JSON ("null" when none); returns the full length */
size_t    vv_engine_export_preset(VVEngine* e, char* buf, size_t buf_len);   /* the engine's current preset JSON (web snapshotState format, incl. extra.clips); returns the full length */
int       vv_engine_set_global(VVEngine* e, const char* name, double value);
// Chord maker: action = "generate" "mutate" "revoice" "rotl" "rotr" "reverse" "fit" "lock" "reroll" (index = chord for lock / reroll; seed 0 = random). Returns 1 on success.
int       vv_engine_chord_action(VVEngine* e, const char* action, int index, uint32_t seed);
// Preset generator: action "generate" (category Arp/Bass/Key/Lead/Pad/FX) or "modify" (~10 % variation). seed 0 = fresh random. Returns 1 on success.
int       vv_engine_preset_action(VVEngine* e, const char* action, const char* category, uint32_t seed);
/* GENERATE with the keyboard scale: scale_on = Scale lock is on and a scale is chosen; scale_mask bit i = interval i above scale_root is in the scale.
   Writes the variant name and the layer to show (WT FM SMP PRO SID) into the two buffers. Returns 1 on success. */
int       vv_engine_preset_generate(VVEngine* e, const char* category, uint32_t seed, int scale_on, int scale_root, uint32_t scale_mask, const char* scale_label,
                                    char* out_name, int name_cap, char* out_view, int view_cap);
// JSON array [{"name","deg","n","rest","locked"}] of the current chord progression (same sizing convention as vv_engine_get_value).
size_t    vv_engine_chord_info(VVEngine* e, char* buf, size_t buf_len);
int       vv_engine_chord_playing(VVEngine* e);                                       // index of the sounding chord, -1 when none
int       vv_engine_seq_step(VVEngine* e, int layer);                                 // playhead (0-based step index) for UI display
int       vv_engine_seq_slot(VVEngine* e, int layer);                                 // pattern slot currently playing
int       vv_engine_seq_running(VVEngine* e);
int       vv_engine_drum_step(VVEngine* e, int lane);                                 // drum sequencer playhead for lane 0..15 (-1 before the first step)
void      vv_engine_set_macro(VVEngine* e, int index, float value01);                 // index 0..3
void      vv_engine_set_xy(VVEngine* e, int index, float value01);                    // XY pad position as a modulation source: index = pad (0..3) * 2 + axis (0 = X, 1 = Y)
void      vv_engine_set_modwheel(VVEngine* e, float value01);
void      vv_engine_set_pitchbend(VVEngine* e, float value);                          // -1..1
void      vv_engine_set_master_limiter(VVEngine* e, int on, double thresholdDb, double ceilingDb, double releaseMs, double kneeDb, double softClip01);
/* Battery Saving Mode (not part of a preset). profile 0 off / 1 Light / 2 Balanced / 3 Maximum Efficiency; flags: 1 voices, 2 DSP, 4 effects, 8 modulation, 16 visualisation.
   Thread-safe, applied at the next audio quantum. get_power_stats: out[0..3] = voices reaped, FX stage-blocks skipped, layer-EQ blocks skipped, quanta rendered; out[4] = UI refresh divider (>= 1). */
void      vv_engine_set_power(VVEngine* e, int profile, int flags);
void      vv_engine_get_power_stats(VVEngine* e, double out[5]);
void      vv_engine_get_loudness(VVEngine* e, double out[6]);                            // momentary, short-term, integrated LUFS, peak L, peak R dBFS, max GR dB (-1000 = none yet)
void      vv_engine_reset_loudness(VVEngine* e);
void      vv_engine_set_midi_channel(VVEngine* e, int ch);                            // 0 = Omni, 1..16; ignored while MPE is on
void      vv_engine_mpe_glide(VVEngine* e, int note, double semitones);                  // MPE touch-keyboard drag: continuous pitch offset of a held note
void      vv_engine_set_mpe(VVEngine* e, int on);                                     // per-note expression on channels 2..16
void      vv_engine_set_arp(VVEngine* e, int on, int latch);
// Arp-aware RANDOMISE: fills out[0..count) with melodic MIDI notes in [lo, hi] that fit what the arpeggiator is playing (its chord tones, as transposed, + a scale that contains them;
// strong[i] != 0 marks strong beats, may be NULL; kbMask = keyboard scale as a 12-bit pitch-class mask or 0). Writes a short description (e.g. "C minor") to name. Lock-free; any thread.
// Returns 1 when fitted, 0 when the arp is not playing (keep the normal random behaviour).
int       vv_engine_arp_fit(VVEngine* e, int count, const uint8_t* strong, int kbMask, uint32_t seed, int lo, int hi, int* out, char* name, int nameCap);
/* Piano-roll RECORD (the roll's REC button). set(1) arms: the user's note-on / note-off (keyboard, MIDI), pitch bend and mod wheel are stamped with the heard beat (output latency removed)
   while the transport runs. drain_json (UI thread, ~30 Hz) returns finished data: {"notes":[{t,l,p,v}],"bend":[[beat,v]],"bendCut":[[a,b]],"mod":[..],"modCut":[..],"count":n,"armed":0|1}.
   Merge: append the notes; for each lane delete the OLD points inside every Cut span, add the new points, sort by beat (r.lanes.bend / r.lanes.mod). Loop start / end and roll end are in beats
   (loopOn ? loopStart*4 : 0, loopOn ? max(loopStart*4+4, loopEnd*4) : bars*4, bars*4); grid 0 = no quantise. After set(0), call drain once more with final = 1 to flush held notes. Returns 0 on error. */
void      vv_engine_roll_rec_set(VVEngine* e, int on);
int       vv_engine_roll_rec_armed(VVEngine* e);
size_t    vv_engine_roll_rec_drain_json(VVEngine* e, double loopStartBeats, double loopEndBeats, double rollEndBeats, double gridBeats, int final_, char* buf, size_t cap);
void      vv_engine_start_sequencer(VVEngine* e, int running);
void      vv_engine_set_polyphony(VVEngine* e, int voices, int legato);
void      vv_engine_release_layer_hold(VVEngine* e, int layer);
int       vv_engine_has_preset(VVEngine* e);

// Optional host/microphone audio (mono = right NULL) for the Hybrid Vocoder's external modulator / carrier. Call from the audio thread before vv_engine_render.
void      vv_engine_feed_input(VVEngine* e, const float* left, const float* right, int frames);
void      vv_engine_render(VVEngine* e, float* left, float* right, int frames);

/* DAW recorder + click track (standalone use). Records the master output; the click is added after the recorder tap so it is never recorded.
   vv_daw_drain MUST be called regularly (e.g. 10-20 Hz) from one non-audio thread and returns the take length in seconds; a take is limited to 10 minutes.
   vv_daw_take_wav resamples to sample_rate (0 = engine rate) and writes a stereo PCM WAV (bit_depth 16 or 24); call with buf NULL to get the byte size.
   state: 0 idle, 1 recording, 2 stopped. click mode: 0 off, 1 beep, 2 rimshot. */
void      vv_daw_start(VVEngine* e);
void      vv_daw_stop(VVEngine* e);
int       vv_daw_state(VVEngine* e);
double    vv_daw_drain(VVEngine* e);
int       vv_daw_dropped(VVEngine* e);
void      vv_daw_set_click(VVEngine* e, int mode);
size_t    vv_daw_take_wav(VVEngine* e, int bit_depth, int sample_rate, uint8_t* buf, size_t cap);
void      vv_daw_clear_take(VVEngine* e);

/* Sample recorder (web RECORD tool). prepare allocates (call from a non-audio thread); start/stop/state/frames/peak are lock-free; take copies the result out.
   source 0 = master output, 1 = external input (what the host/mic delivers through vv_engine_feed_input, blocks up to 8192 frames). arm = wait for sound above threshold.
   state: 0 idle, 1 armed, 2 recording, 3 finished (stopped or buffer full). */
int       vv_engine_rec_prepare(VVEngine* e, int sample_rate, double max_seconds, int source, int arm, double threshold);
void      vv_engine_rec_start(VVEngine* e);
void      vv_engine_rec_stop(VVEngine* e);
int       vv_engine_rec_state(VVEngine* e);
int       vv_engine_rec_frames(VVEngine* e);
float     vv_engine_rec_peak(VVEngine* e);
VVAudio*  vv_engine_rec_take(VVEngine* e, int sample_rate);   /* NULL if nothing recorded; the caller frees it with vv_audio_free */

/* ---- Vektur Void (hidden Easter-egg game). UI thread only; never touches the audio engine or any parameter. -----------------------------
   vv_void_snapshot fills `out` with floats: header (30), 9 counts, then the entity records (layout mirrors Core/include/vv/vektur_void.hpp).
   header:  0 mode (0 title,1 play,2 levelclear,3 continue,4 gameover,5 victory) 1 t 2 modeT 3 score 4 level 5 spares 6 continues 7 wLvl 8 shipX 9 shipY
            10 alive 11 inv 12 shield 13 shake 14 bannerT 15 contT 16 typed 17 bossPresent 18 bossX 19 bossY 20 bossW 21 bossH 22 bossHits 23 bossMax
            24 bossFlash 25 bossDying 26 bossT 27 bossFinal 28 bossCore 29 bossTint (0 green, 1 orange, 2 red)
   counts:  bullets, bombs, blasts, enemies, ebul, pick, parts, beams, obstacles
   records: bullet(x y w h) bomb(x y r) blast(x y r t) enemy(type x y r t flash) ebul(x y r col) pick(kind x y) part(x y life t) beam(y t)
            obstacle(kind x width nparts, then nparts * (x y w h))
   Returns the number of floats written, or -1 if `cap` is too small (16384 is always enough). */
typedef struct VVVoid VVVoid;
VVVoid*   vv_void_create(uint32_t seed);
void      vv_void_destroy(VVVoid* g);
void      vv_void_step(VVVoid* g, double dt, double dx, double dy, int plasma, int bomb, int start);   /* dt = 1/60 */
int       vv_void_snapshot(VVVoid* g, float* out, int cap);
int       vv_void_banner(VVVoid* g, char* out, int cap);    /* current banner text (UTF-8, NUL-terminated); returns its length */
void      vv_void_set_god(VVVoid* g, int on);               /* testing aid: invulnerable ship */


// ---- LUA layer (scripted oscillators): info / validation / telemetry for the editor UI (non-audio thread). All return the JSON length (<= cap-1 copied), or -1 on bad arguments.
int         vv_engine_lua_info(VVEngine* e, int slot, char* buf, int cap);                   // {"ok","error","line","col","name","category","author","desc","setupInstr","tables","params":[{id,label,min,max,def,step,unit,group,kind,options[]}]}
int         vv_engine_lua_validate(VVEngine* e, const char* src, int seed, char* buf, int cap);   // same JSON; compiles a copy, installs nothing
int         vv_engine_lua_stats(VVEngine* e, int slot, char* buf, int cap);                  // {"voices","instr","faults","error","log"}
int         vv_engine_lua_set_param(VVEngine* e, int slot, int index, float value);          // live script parameter (also persisted via set_value layers.LUA.oscs.N.lua.params.<id>)
int         vv_engine_lua_factory_count(void);
const char* vv_engine_lua_factory_id(int i);
const char* vv_engine_lua_factory_src(int i);

/* ---- Clip launcher: 8 banks x 8 slots (slot = bank * 8 + index, 0..63) of loopable note + automation clips. Launch / stop are quantised on the engine transport clock.
   vv_clip_op(op, args_json): "new"{slot,layer,bars} "duplicate"{slot} "delete"{slot} "move"{from,to,mode 0 move/1 copy/2 swap} "paste"{slot,json} "export"{slot} (text in `out`)
   "rename"{slot,name} "bankName"{bank,name} "bars"{slot,bars 1..64} "layer"{slot,layer 0..5} "color"{slot,color} "transpose"{slot,delta} "setTranspose"{slot,value}
   "quantiseNotes"{slot} "clearAuto"{slot} "quant"{q 0..7} "mode"{m 0 replace/1 overdub} "select"{slot} "bank"{bank} "factory"{bank} "undo" "occupied"{slot} "fromRoll"{slot,layer}
   "toRoll"{slot} "recordBegin"{slot} "recordEnd" "autoWrite"{slot,key,phase,value} "autoEnd"{slot,key} "load"{json}.  Returns 1 on success, 0 when refused / nothing happened.
   The whole clip state is part of the preset JSON (extra.clips), so presets, AUv3 state and exportPreset carry it. */
int      vv_clip_op(VVEngine* e, const char* op, const char* args_json, char* out, size_t out_cap);
size_t   vv_clip_state_json(VVEngine* e, char* buf, size_t cap);   /* returns the full length; the buffer gets a (truncated) copy */
void     vv_clip_launch(VVEngine* e, int slot);
void     vv_clip_stop(VVEngine* e, int slot, int immediate);
void     vv_clip_stop_all(VVEngine* e);
uint64_t vv_clip_playing_mask(VVEngine* e);          /* bit n = slot n is playing */
uint64_t vv_clip_queued_start_mask(VVEngine* e);
uint64_t vv_clip_queued_stop_mask(VVEngine* e);
int      vv_clip_recording_slot(VVEngine* e);        /* -1 when not recording */
float    vv_clip_phase(VVEngine* e, int slot);       /* beats into the clip loop, -1 when not playing */
int      vv_clip_drain_recording(VVEngine* e);       /* merge notes the audio thread recorded into the clips (call ~10 Hz while recording); returns notes merged */

/* Sampler Round Robin: the take (slot index 0..15) slot `slot` played last, -1 when none yet. UI indication. */
int      vv_engine_smp_rr_last(VVEngine* e, int slot);
void     vv_engine_smp_rr_reset(VVEngine* e);   /* start every Round Robin rotation again from the first sample */


/* Notices from the last vv_engine_load_preset_json (one per line: skipped unknown effects, repaired ranges, newer schema...). Returns the full length. */
size_t      vv_engine_load_warnings(VVEngine* e, char* buf, size_t cap);

/* ---- IR convolution reverb library (process-wide: shared by every engine, the app and the AUv3 extension each have their own copy and rescan the shared folder) ----
   Factory IRs are generated (original, deterministic); user IRs are imported WAV / AIFF files, identified by a content hash so a preset reference survives a re-import. */
int         vv_ir_category_count(void);
const char* vv_ir_category_name(int i);
int         vv_ir_factory_count(void);
const char* vv_ir_factory_id(int i);         /* "f:hall-concert" */
const char* vv_ir_factory_name(int i);
const char* vv_ir_factory_category(int i);
/* import a file: returns 1 on success (id in out_id; out_msg = warnings), 0 on failure (out_msg = the reason). replace_id: keep that id/slot (REPLACE). */
int         vv_ir_import_file(const char* path, const char* name, const char* replace_id, char* out_id, int id_cap, char* out_msg, int msg_cap);
int         vv_ir_import_bytes(const uint8_t* data, size_t n, const char* name, const char* replace_id, char* out_id, int id_cap, char* out_msg, int msg_cap);
int         vv_ir_remove(const char* id);
int         vv_ir_scan_dir(const char* dir);   /* registers every WAV / AIFF in a folder (the shared "IRs" folder); returns how many */
size_t      vv_ir_user_json(char* buf, size_t cap);      /* [{"id","name","sr","seconds"}] */
size_t      vv_ir_missing_json(char* buf, size_t cap);   /* ids that presets asked for but the library does not have (the stage plays the default hall meanwhile) */
int         vv_ir_wait_ready(int timeout_ms);            /* blocks until every IR stage has finished preparing its IR (offline render / tests); 1 = ready */
/* params_json: the stage's params (irId, start, end, lenPct, earlyLate, damping, reverse, normalise, irGain). Renders the audition burst through the shaped IR into L / R; returns the frame count (0 on error). */
int         vv_ir_audition(const char* params_json, float* L, float* R, int max_frames, double sr);
/* envelope of the source IR for the display (bins values 0..1), returns the IR length in seconds (0 on error) */
double      vv_ir_envelope(const char* ir_id, float* out, int bins, double sr);
double      vv_ir_auto_trim(const char* ir_id, double sr, double* start, double* end);

#ifdef __cplusplus
}
#endif
#endif
