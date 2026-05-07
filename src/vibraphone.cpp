#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lv2/core/lv2.h"
#include "lv2/atom/atom.h"
#include "lv2/atom/util.h"

static const char* kUri = "https://github.com/AsierT/vibraphone-lv2#vibraphone";

enum PortIndex : uint32_t {
#ifdef VIBRAPHONE_INSERT_PORTS
  IN_L = 0, IN_R, OUT_L, OUT_R, ROOT, SCALE, INTERVAL, HARMONY_LEVEL,
  HARMONY_DIRECTION, SCALE_SNAP, TONE, MALLET_HARDNESS, STRIKE_NOISE,
  BAR_DECAY, VELOCITY_SENS, AMP_ATTACK, AMP_DECAY, AMP_SUSTAIN, AMP_RELEASE,
  TREMOLO_RATE, TREMOLO_DEPTH, WIDTH, GAIN, SPRING_MIX, SPRING_DECAY, SPRING_TONE,
  SPRING_DRIVE, SPRING_SHAKE, DELAY_MIX, DELAY_TIME, DELAY_FEEDBACK,
  TAPE_TONE, WOW_FLUTTER, TAPE_AGE, HEAD_MODE, MIDI_IN
#else
  OUT_L = 0, OUT_R, ROOT, SCALE, INTERVAL, HARMONY_LEVEL, HARMONY_DIRECTION,
  SCALE_SNAP, TONE, MALLET_HARDNESS, STRIKE_NOISE, BAR_DECAY, VELOCITY_SENS,
  AMP_ATTACK, AMP_DECAY, AMP_SUSTAIN, AMP_RELEASE, TREMOLO_RATE, TREMOLO_DEPTH,
  WIDTH, GAIN, SPRING_MIX, SPRING_DECAY, SPRING_TONE, SPRING_DRIVE, SPRING_SHAKE,
  DELAY_MIX, DELAY_TIME, DELAY_FEEDBACK, TAPE_TONE, WOW_FLUTTER, TAPE_AGE,
  HEAD_MODE, MIDI_IN
#endif
};

struct ScaleDef {
  uint8_t count;
  int steps[7];
};

static const ScaleDef kScales[] = {
  {7, {0, 2, 4, 5, 7, 9, 11}},  // major
  {7, {0, 2, 3, 5, 7, 8, 10}},  // natural minor
  {7, {0, 2, 3, 5, 7, 9, 10}},  // dorian
  {7, {0, 1, 3, 5, 7, 8, 10}},  // phrygian
  {7, {0, 2, 4, 6, 7, 9, 11}},  // lydian
  {7, {0, 2, 4, 5, 7, 9, 10}},  // mixolydian
  {7, {0, 2, 3, 5, 7, 8, 11}},  // harmonic minor
  {7, {0, 2, 3, 5, 7, 9, 11}},  // melodic minor
};

struct Voice {
  uint8_t active;
  uint8_t stage;
  int midi_note;
  float freq;
  float amp;
  float env;
  float bar_env;
  float age;
  float phase1;
  float phase2;
  float phase3;
  float phase4;
};

enum {
  kSpringA = 257,
  kSpringB = 389,
  kSpringC = 563
};

struct Plugin {
  float sr;
  Voice voices[2];
  float lfo_phase;
  float wow_phase;
  uint32_t noise;

  float spring_a[kSpringA];
  float spring_b[kSpringB];
  float spring_c[kSpringC];
  uint32_t spring_pos_a;
  uint32_t spring_pos_b;
  uint32_t spring_pos_c;
  float spring_lp_l;
  float spring_lp_r;

  float* delay_l;
  float* delay_r;
  uint32_t delay_size;
  uint32_t delay_pos;
  float delay_lp_l;
  float delay_lp_r;

  const float *in_l, *in_r;
  float *out_l, *out_r;
  const float *root, *scale, *interval, *harmony_level, *harmony_direction, *scale_snap;
  const float *tone, *mallet_hardness, *strike_noise, *bar_decay, *velocity_sens;
  const float *amp_attack, *amp_decay, *amp_sustain, *amp_release;
  const float *tremolo_rate, *tremolo_depth, *width, *gain;
  const float *spring_mix, *spring_decay, *spring_tone, *spring_drive, *spring_shake;
  const float *delay_mix, *delay_time, *delay_feedback, *tape_tone, *wow_flutter, *tape_age, *head_mode;
  const LV2_Atom_Sequence* midi_in;
};

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = 2.0f * kPi;
static constexpr float kRefSampleRate = 48000.0f;
static constexpr uint8_t kStageOff = 0;
static constexpr uint8_t kStageAttack = 1;
static constexpr uint8_t kStageDecay = 2;
static constexpr uint8_t kStageSustain = 3;
static constexpr uint8_t kStageRelease = 4;

static bool finite_float(float x) {
  return __builtin_isfinite(x);
}

static bool finite_double(double x) {
  return __builtin_isfinite(x);
}

static float finite_or(float x, float fallback) {
  return finite_float(x) ? x : fallback;
}

static float clamp(float x, float lo, float hi) {
  x = finite_or(x, lo);
  return x < lo ? lo : (x > hi ? hi : x);
}

static float control_value(const float* value, float fallback, float lo, float hi) {
  return clamp(finite_or(value ? *value : fallback, fallback), lo, hi);
}

static int control_int(const float* value, int fallback, int lo, int hi) {
  const float v = control_value(value, static_cast<float>(fallback), static_cast<float>(lo), static_cast<float>(hi));
  int i = static_cast<int>(v >= 0.0f ? v + 0.5f : v - 0.5f);
  if (i < lo) i = lo;
  if (i > hi) i = hi;
  return i;
}

static float sample_rate(const Plugin* p) {
  return (p && finite_float(p->sr) && p->sr >= 1000.0f && p->sr <= 384000.0f) ? p->sr : kRefSampleRate;
}

static float time_coef_ms(const Plugin* p, float ms) {
  const float sr = sample_rate(p);
  ms = clamp(ms, 0.1f, 10000.0f);
  return finite_or(expf(-1.0f / (0.001f * ms * sr)), 0.0f);
}

static float wrap_positive(float x, float period) {
  if (!finite_float(x) || period <= 0.0f) return 0.0f;
  while (x >= period) x -= period;
  while (x < 0.0f) x += period;
  return x;
}

static float limit_output(float x) {
  return clamp(finite_or(x, 0.0f), -1.0f, 1.0f);
}

static float soft_clip(float x) {
  x = finite_or(x, 0.0f);
  return finite_or(x / (1.0f + fabsf(x)), 0.0f);
}

static float frand(Plugin* p) {
  p->noise = p->noise * 1664525u + 1013904223u;
  return ((p->noise >> 8) & 0xFFFF) / 32768.0f - 1.0f;
}

static int positive_mod_int(int x, int m) {
  const int r = x % m;
  return r < 0 ? r + m : r;
}

static int floor_div_int(int x, int d) {
  int q = x / d;
  const int r = x % d;
  if (r != 0 && ((r < 0) != (d < 0))) --q;
  return q;
}

static float midi_note_to_hz(int note) {
  note = note < 0 ? 0 : (note > 127 ? 127 : note);
  return finite_or(440.0f * powf(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f), 440.0f);
}

static int interval_steps(int interval) {
  static const int steps[] = {0, 1, 2, 3, 4, 5, 6, 7};
  interval = interval < 0 ? 0 : (interval > 7 ? 7 : interval);
  return steps[interval];
}

static int note_to_scale_degree(int midi_note, int root, int scale_index, int snap) {
  const ScaleDef* s = &kScales[scale_index];
  const int semis = midi_note - root;
  int octave = floor_div_int(semis, 12);
  const int chroma = positive_mod_int(semis, 12);

  if (snap == 1) {
    for (int i = 0; i < s->count; ++i) {
      if (s->steps[i] >= chroma) return octave * s->count + i;
    }
    return (octave + 1) * s->count;
  }

  if (snap == 2) {
    for (int i = s->count - 1; i >= 0; --i) {
      if (s->steps[i] <= chroma) return octave * s->count + i;
    }
    return (octave - 1) * s->count + (s->count - 1);
  }

  int best = 0;
  int best_dist = 99;
  for (int i = 0; i < s->count; ++i) {
    int dist = chroma - s->steps[i];
    if (dist < 0) dist = -dist;
    if (dist < best_dist) {
      best = i;
      best_dist = dist;
    }
  }
  return octave * s->count + best;
}

static int scale_degree_to_note(int root, int scale_index, int degree) {
  const ScaleDef* s = &kScales[scale_index];
  const int octave = floor_div_int(degree, s->count);
  const int idx = positive_mod_int(degree, s->count);
  return root + octave * 12 + s->steps[idx];
}

static int harmony_note(int midi_note, int root, int scale_index, int interval, int direction, int snap) {
  const int degree = note_to_scale_degree(midi_note, root, scale_index, snap);
  const int amount = interval_steps(interval);
  return scale_degree_to_note(root, scale_index, degree + (direction == 1 ? -amount : amount));
}

static void reset_voice(Voice* v) {
  if (!v) return;
  v->active = 0;
  v->stage = kStageOff;
  v->midi_note = -1;
  v->freq = 440.0f;
  v->amp = 0.0f;
  v->env = 0.0f;
  v->bar_env = 0.0f;
  v->age = 0.0f;
  v->phase1 = 0.0f;
  v->phase2 = 0.0f;
  v->phase3 = 0.0f;
  v->phase4 = 0.0f;
}

static void clear_delay(Plugin* p) {
  if (!p) return;
  if (p->delay_l) {
    for (uint32_t i = 0; i < p->delay_size; ++i) p->delay_l[i] = 0.0f;
  }
  if (p->delay_r) {
    for (uint32_t i = 0; i < p->delay_size; ++i) p->delay_r[i] = 0.0f;
  }
}

static void reset_state(Plugin* p) {
  if (!p) return;
  reset_voice(&p->voices[0]);
  reset_voice(&p->voices[1]);
  p->lfo_phase = 0.0f;
  p->wow_phase = 0.0f;
  p->noise = 0x68756D31u;
  p->spring_pos_a = 0;
  p->spring_pos_b = 0;
  p->spring_pos_c = 0;
  p->spring_lp_l = 0.0f;
  p->spring_lp_r = 0.0f;
  for (uint32_t i = 0; i < kSpringA; ++i) p->spring_a[i] = 0.0f;
  for (uint32_t i = 0; i < kSpringB; ++i) p->spring_b[i] = 0.0f;
  for (uint32_t i = 0; i < kSpringC; ++i) p->spring_c[i] = 0.0f;
  p->delay_pos = 0;
  p->delay_lp_l = 0.0f;
  p->delay_lp_r = 0.0f;
  clear_delay(p);
}

static bool allocate_delay(Plugin* p) {
  if (!p) return false;
  float sr = sample_rate(p);
  uint32_t size = static_cast<uint32_t>(sr * 2.1f) + 8u;
  if (size < 4096u) size = 4096u;
  if (size > 192000u) size = 192000u;
  p->delay_l = static_cast<float*>(malloc(sizeof(float) * size));
  p->delay_r = static_cast<float*>(malloc(sizeof(float) * size));
  if (!p->delay_l || !p->delay_r) return false;
  p->delay_size = size;
  clear_delay(p);
  return true;
}

static void init_plugin(Plugin* p, float sr) {
  p->sr = sr;
  p->delay_l = nullptr;
  p->delay_r = nullptr;
  p->delay_size = 0;
  p->delay_pos = 0;
  p->in_l = nullptr;
  p->in_r = nullptr;
  p->out_l = nullptr;
  p->out_r = nullptr;
  p->root = nullptr;
  p->scale = nullptr;
  p->interval = nullptr;
  p->harmony_level = nullptr;
  p->harmony_direction = nullptr;
  p->scale_snap = nullptr;
  p->tone = nullptr;
  p->mallet_hardness = nullptr;
  p->strike_noise = nullptr;
  p->bar_decay = nullptr;
  p->velocity_sens = nullptr;
  p->amp_attack = nullptr;
  p->amp_decay = nullptr;
  p->amp_sustain = nullptr;
  p->amp_release = nullptr;
  p->tremolo_rate = nullptr;
  p->tremolo_depth = nullptr;
  p->width = nullptr;
  p->gain = nullptr;
  p->spring_mix = nullptr;
  p->spring_decay = nullptr;
  p->spring_tone = nullptr;
  p->spring_drive = nullptr;
  p->spring_shake = nullptr;
  p->delay_mix = nullptr;
  p->delay_time = nullptr;
  p->delay_feedback = nullptr;
  p->tape_tone = nullptr;
  p->wow_flutter = nullptr;
  p->tape_age = nullptr;
  p->head_mode = nullptr;
  p->midi_in = nullptr;
  reset_state(p);
}

static void trigger_voice(Voice* v, int note, float amp) {
  if (!v) return;
  reset_voice(v);
  v->active = 1;
  v->midi_note = note;
  v->freq = midi_note_to_hz(note);
  v->amp = clamp(amp, 0.0f, 1.0f);
  v->bar_env = v->amp;
  v->stage = kStageAttack;
}

static void trigger_pair(Plugin* p, int note, float velocity) {
  const int root = control_int(p->root, 0, 0, 11);
  const int scale = control_int(p->scale, 0, 0, 7);
  const int interval = control_int(p->interval, 4, 0, 7);
  const int direction = control_int(p->harmony_direction, 0, 0, 1);
  const int snap = control_int(p->scale_snap, 0, 0, 2);
  const float velocity_sens = control_value(p->velocity_sens, 0.75f, 0.0f, 1.0f);
  const float harmony = control_value(p->harmony_level, 0.82f, 0.0f, 1.0f);
  const float amp = clamp((1.0f - velocity_sens) + velocity * velocity_sens, 0.0f, 1.0f);
  const int second = harmony_note(note, root, scale, interval, direction, snap);

  trigger_voice(&p->voices[0], note, amp);
  trigger_voice(&p->voices[1], second, amp * harmony);
}

static void release_pair(Plugin* p, int note) {
  if (!p) return;
  if (p->voices[0].midi_note == note || p->voices[1].midi_note == note) {
    if (p->voices[0].active) p->voices[0].stage = kStageRelease;
    if (p->voices[1].active) p->voices[1].stage = kStageRelease;
  }
}

static void handle_midi(Plugin* p) {
  if (!p->midi_in) return;
  if (p->midi_in->atom.size < 8) return;

  LV2_ATOM_SEQUENCE_FOREACH(p->midi_in, ev) {
    const uint8_t* m = reinterpret_cast<const uint8_t*>(ev + 1);
    if (ev->body.size < 3) continue;
    const uint8_t status = m[0] & 0xF0;
    const int note = static_cast<int>(m[1]);
    const int vel = static_cast<int>(m[2]);

    if (status == 0x90 && vel > 0) {
      trigger_pair(p, note, clamp(static_cast<float>(vel) / 127.0f, 0.0f, 1.0f));
    } else if (status == 0x80 || (status == 0x90 && vel == 0)) {
      release_pair(p, note);
    }
  }
}

static float render_voice(Plugin* p, Voice* v, float tone, float hardness, float strike_noise,
                          float attack_coef, float decay_coef, float sustain, float release_coef,
                          float bar_coef) {
  if (!v || !v->active) return 0.0f;

  const float sr = sample_rate(p);
  sustain = clamp(sustain, 0.0f, 1.0f);

  if (v->stage == kStageAttack) {
    const float attack_step = clamp(1.0f - attack_coef, 0.00001f, 1.0f);
    v->env += (v->amp - v->env) * attack_step;
    if (v->env >= v->amp * 0.995f || v->amp <= 0.0001f) {
      v->env = v->amp;
      v->stage = kStageDecay;
    }
  } else if (v->stage == kStageDecay) {
    const float target = v->amp * sustain;
    const float decay_step = clamp(1.0f - decay_coef, 0.00001f, 1.0f);
    v->env += (target - v->env) * decay_step;
    if (v->env <= target + 0.0005f) {
      v->env = target;
      v->stage = target > 0.0001f ? kStageSustain : kStageRelease;
    }
  } else if (v->stage == kStageSustain) {
    v->env = v->amp * sustain;
  } else if (v->stage == kStageRelease) {
    v->env *= release_coef;
  } else {
    reset_voice(v);
    return 0.0f;
  }

  v->bar_env *= bar_coef;
  v->env = clamp(v->env, 0.0f, 1.0f);
  v->bar_env = clamp(v->bar_env, 0.0f, 1.0f);

  if ((v->env * v->bar_env) < 0.00001f && (v->stage == kStageRelease || sustain <= 0.0001f)) {
    reset_voice(v);
    return 0.0f;
  }

  const float hard = hardness * hardness;
  const float bright = tone * tone;
  const float f1 = clamp(v->freq, 1.0f, sr * 0.45f);
  const float f2 = clamp(v->freq * (2.01f + tone * 0.13f), 1.0f, sr * 0.45f);
  const float f3 = clamp(v->freq * (3.01f + tone * 0.31f), 1.0f, sr * 0.45f);
  const float f4 = clamp(v->freq * (4.18f + tone * 0.42f), 1.0f, sr * 0.45f);

  v->phase1 = wrap_positive(v->phase1 + kTwoPi * f1 / sr, kTwoPi);
  v->phase2 = wrap_positive(v->phase2 + kTwoPi * f2 / sr, kTwoPi);
  v->phase3 = wrap_positive(v->phase3 + kTwoPi * f3 / sr, kTwoPi);
  v->phase4 = wrap_positive(v->phase4 + kTwoPi * f4 / sr, kTwoPi);

  float y = sinf(v->phase1) * 0.72f;
  y += sinf(v->phase2) * (0.16f + 0.18f * bright + 0.12f * hard);
  y += sinf(v->phase3) * (0.08f + 0.18f * bright + 0.16f * hard);
  y += sinf(v->phase4) * (0.03f + 0.10f * bright + 0.20f * hard);

  const float strike_len = 0.018f - hard * 0.012f;
  if (v->age < strike_len) {
    const float strike_env = 1.0f - v->age / strike_len;
    y += frand(p) * strike_env * strike_noise * (0.03f + hard * 0.22f);
  }

  v->age += 1.0f / sr;
  if (!finite_float(v->age) || v->age > 20.0f) v->age = 20.0f;
  return finite_or(y * v->env * v->bar_env, 0.0f);
}

static float spring_tap(float* buffer, uint32_t size, uint32_t* pos, float x, float feedback) {
  float y = buffer[*pos];
  buffer[*pos] = clamp(x + y * feedback, -2.0f, 2.0f);
  *pos = (*pos + 1u) % size;
  return finite_or(y, 0.0f);
}

static void process_spring(Plugin* p, float in_l, float in_r, float* out_l, float* out_r) {
  const float mix = control_value(p->spring_mix, 0.0f, 0.0f, 1.0f);
  if (mix <= 0.0001f) {
    *out_l = in_l;
    *out_r = in_r;
    return;
  }

  const float decay = control_value(p->spring_decay, 0.45f, 0.0f, 1.0f);
  const float tone = control_value(p->spring_tone, 0.55f, 0.0f, 1.0f);
  const float drive = control_value(p->spring_drive, 0.20f, 0.0f, 1.0f);
  const float shake = control_value(p->spring_shake, 0.0f, 0.0f, 1.0f);
  const float feedback = 0.55f + decay * 0.38f;
  const float input = soft_clip((in_l + in_r) * 0.5f * (1.0f + drive * 4.0f)) + frand(p) * shake * 0.004f;

  const float a = spring_tap(p->spring_a, kSpringA, &p->spring_pos_a, input, feedback);
  const float b = spring_tap(p->spring_b, kSpringB, &p->spring_pos_b, input + a * 0.21f, feedback * 0.93f);
  const float c = spring_tap(p->spring_c, kSpringC, &p->spring_pos_c, input - b * 0.17f, feedback * 0.88f);
  float wet_l = a * 0.58f + b * 0.30f - c * 0.24f;
  float wet_r = c * 0.58f + b * 0.30f - a * 0.24f;

  const float alpha = 0.025f + tone * tone * 0.40f;
  p->spring_lp_l += (wet_l - p->spring_lp_l) * alpha;
  p->spring_lp_r += (wet_r - p->spring_lp_r) * alpha;
  wet_l = soft_clip(p->spring_lp_l * (1.0f + drive * 1.5f));
  wet_r = soft_clip(p->spring_lp_r * (1.0f + drive * 1.5f));

  *out_l = finite_or(in_l * (1.0f - mix * 0.30f) + wet_l * mix, 0.0f);
  *out_r = finite_or(in_r * (1.0f - mix * 0.30f) + wet_r * mix, 0.0f);
}

static float read_delay(const float* buffer, uint32_t size, uint32_t pos, float delay_samples) {
  if (!buffer || size < 4u) return 0.0f;
  delay_samples = clamp(delay_samples, 1.0f, static_cast<float>(size - 3u));
  float read = static_cast<float>(pos) - delay_samples;
  while (read < 0.0f) read += static_cast<float>(size);
  while (read >= static_cast<float>(size)) read -= static_cast<float>(size);
  const uint32_t i0 = static_cast<uint32_t>(read);
  const uint32_t i1 = (i0 + 1u) % size;
  const float frac = read - static_cast<float>(i0);
  return finite_or(buffer[i0] + (buffer[i1] - buffer[i0]) * frac, 0.0f);
}

static void add_head(int mode, int head, float value_l, float value_r, float* sum_l, float* sum_r, float* count) {
  bool on = false;
  if (mode == 0 && head == 0) on = true;
  if (mode == 1 && head == 1) on = true;
  if (mode == 2 && head == 2) on = true;
  if (mode == 3 && (head == 0 || head == 1)) on = true;
  if (mode == 4 && (head == 1 || head == 2)) on = true;
  if (mode == 5 && (head == 0 || head == 2)) on = true;
  if (mode == 6) on = true;
  if (!on) return;
  *sum_l += value_l;
  *sum_r += value_r;
  *count += 1.0f;
}

static void process_delay(Plugin* p, float in_l, float in_r, float* out_l, float* out_r) {
  const float mix = control_value(p->delay_mix, 0.0f, 0.0f, 1.0f);
  if (mix <= 0.0001f || !p->delay_l || !p->delay_r || p->delay_size < 4u) {
    *out_l = in_l;
    *out_r = in_r;
    return;
  }

  const float sr = sample_rate(p);
  const float time_ms = control_value(p->delay_time, 360.0f, 40.0f, 1600.0f);
  const float feedback = control_value(p->delay_feedback, 0.35f, 0.0f, 0.92f);
  const float tape_tone = control_value(p->tape_tone, 0.55f, 0.0f, 1.0f);
  const float wow = control_value(p->wow_flutter, 0.15f, 0.0f, 1.0f);
  const float age = control_value(p->tape_age, 0.25f, 0.0f, 1.0f);
  const int mode = control_int(p->head_mode, 6, 0, 6);

  p->wow_phase = wrap_positive(p->wow_phase + kTwoPi * (0.18f + wow * 0.65f) / sr, kTwoPi);
  const float mod = 1.0f + (sinf(p->wow_phase) * 0.018f + sinf(p->wow_phase * 0.37f) * 0.007f) * wow;
  const float base = time_ms * sr * 0.001f * mod;

  float sum_l = 0.0f;
  float sum_r = 0.0f;
  float count = 0.0f;
  add_head(mode, 0, read_delay(p->delay_l, p->delay_size, p->delay_pos, base * 0.50f),
           read_delay(p->delay_r, p->delay_size, p->delay_pos, base * 0.50f), &sum_l, &sum_r, &count);
  add_head(mode, 1, read_delay(p->delay_l, p->delay_size, p->delay_pos, base * 0.75f),
           read_delay(p->delay_r, p->delay_size, p->delay_pos, base * 0.75f), &sum_l, &sum_r, &count);
  add_head(mode, 2, read_delay(p->delay_l, p->delay_size, p->delay_pos, base),
           read_delay(p->delay_r, p->delay_size, p->delay_pos, base), &sum_l, &sum_r, &count);
  if (count > 0.0f) {
    sum_l /= count;
    sum_r /= count;
  }

  const float alpha = 0.015f + tape_tone * tape_tone * (0.50f - age * 0.28f);
  p->delay_lp_l += (sum_l - p->delay_lp_l) * clamp(alpha, 0.01f, 0.50f);
  p->delay_lp_r += (sum_r - p->delay_lp_r) * clamp(alpha, 0.01f, 0.50f);
  float wet_l = soft_clip(p->delay_lp_l * (1.0f + age * 1.6f)) * (1.0f - age * 0.18f);
  float wet_r = soft_clip(p->delay_lp_r * (1.0f + age * 1.6f)) * (1.0f - age * 0.18f);
  wet_l += frand(p) * age * 0.0008f;
  wet_r += frand(p) * age * 0.0008f;

  const float fb_l = wet_l * feedback;
  const float fb_r = wet_r * feedback;
  p->delay_l[p->delay_pos] = clamp(in_l + fb_l, -2.0f, 2.0f);
  p->delay_r[p->delay_pos] = clamp(in_r + fb_r, -2.0f, 2.0f);
  p->delay_pos = (p->delay_pos + 1u) % p->delay_size;

  *out_l = finite_or(in_l * (1.0f - mix * 0.25f) + wet_l * mix, 0.0f);
  *out_r = finite_or(in_r * (1.0f - mix * 0.25f) + wet_r * mix, 0.0f);
}

static LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const*) {
  Plugin* p = static_cast<Plugin*>(malloc(sizeof(Plugin)));
  if (!p) return nullptr;
  const float sr = (finite_double(rate) && rate >= 1000.0 && rate <= 384000.0) ? static_cast<float>(rate) : kRefSampleRate;
  init_plugin(p, sr);
  if (!allocate_delay(p)) {
    free(p->delay_l);
    free(p->delay_r);
    free(p);
    return nullptr;
  }
  reset_state(p);
  return p;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
  Plugin* p = static_cast<Plugin*>(instance);
  if (!p) return;

  switch (port) {
#ifdef VIBRAPHONE_INSERT_PORTS
    case IN_L: p->in_l = static_cast<const float*>(data); break;
    case IN_R: p->in_r = static_cast<const float*>(data); break;
#endif
    case OUT_L: p->out_l = static_cast<float*>(data); break;
    case OUT_R: p->out_r = static_cast<float*>(data); break;
    case ROOT: p->root = static_cast<const float*>(data); break;
    case SCALE: p->scale = static_cast<const float*>(data); break;
    case INTERVAL: p->interval = static_cast<const float*>(data); break;
    case HARMONY_LEVEL: p->harmony_level = static_cast<const float*>(data); break;
    case HARMONY_DIRECTION: p->harmony_direction = static_cast<const float*>(data); break;
    case SCALE_SNAP: p->scale_snap = static_cast<const float*>(data); break;
    case TONE: p->tone = static_cast<const float*>(data); break;
    case MALLET_HARDNESS: p->mallet_hardness = static_cast<const float*>(data); break;
    case STRIKE_NOISE: p->strike_noise = static_cast<const float*>(data); break;
    case BAR_DECAY: p->bar_decay = static_cast<const float*>(data); break;
    case VELOCITY_SENS: p->velocity_sens = static_cast<const float*>(data); break;
    case AMP_ATTACK: p->amp_attack = static_cast<const float*>(data); break;
    case AMP_DECAY: p->amp_decay = static_cast<const float*>(data); break;
    case AMP_SUSTAIN: p->amp_sustain = static_cast<const float*>(data); break;
    case AMP_RELEASE: p->amp_release = static_cast<const float*>(data); break;
    case TREMOLO_RATE: p->tremolo_rate = static_cast<const float*>(data); break;
    case TREMOLO_DEPTH: p->tremolo_depth = static_cast<const float*>(data); break;
    case WIDTH: p->width = static_cast<const float*>(data); break;
    case GAIN: p->gain = static_cast<const float*>(data); break;
    case SPRING_MIX: p->spring_mix = static_cast<const float*>(data); break;
    case SPRING_DECAY: p->spring_decay = static_cast<const float*>(data); break;
    case SPRING_TONE: p->spring_tone = static_cast<const float*>(data); break;
    case SPRING_DRIVE: p->spring_drive = static_cast<const float*>(data); break;
    case SPRING_SHAKE: p->spring_shake = static_cast<const float*>(data); break;
    case DELAY_MIX: p->delay_mix = static_cast<const float*>(data); break;
    case DELAY_TIME: p->delay_time = static_cast<const float*>(data); break;
    case DELAY_FEEDBACK: p->delay_feedback = static_cast<const float*>(data); break;
    case TAPE_TONE: p->tape_tone = static_cast<const float*>(data); break;
    case WOW_FLUTTER: p->wow_flutter = static_cast<const float*>(data); break;
    case TAPE_AGE: p->tape_age = static_cast<const float*>(data); break;
    case HEAD_MODE: p->head_mode = static_cast<const float*>(data); break;
    case MIDI_IN: p->midi_in = static_cast<const LV2_Atom_Sequence*>(data); break;
  }
}

static void activate(LV2_Handle instance) {
  reset_state(static_cast<Plugin*>(instance));
}

static void run(LV2_Handle instance, uint32_t n) {
  Plugin* p = static_cast<Plugin*>(instance);
  if (!p || !p->out_l || !p->out_r) return;

  handle_midi(p);

  const float sr = sample_rate(p);
  const float tone = control_value(p->tone, 0.55f, 0.0f, 1.0f);
  const float hardness = control_value(p->mallet_hardness, 0.35f, 0.0f, 1.0f);
  const float strike = control_value(p->strike_noise, 0.20f, 0.0f, 1.0f);
  const float bar_decay = control_value(p->bar_decay, 0.65f, 0.0f, 1.0f);
  const float amp_attack = control_value(p->amp_attack, 0.0f, 0.0f, 1.0f);
  const float amp_decay = control_value(p->amp_decay, 0.55f, 0.0f, 1.0f);
  const float amp_sustain = control_value(p->amp_sustain, 0.0f, 0.0f, 1.0f);
  const float amp_release = control_value(p->amp_release, 0.35f, 0.0f, 1.0f);
  const float trem_rate = control_value(p->tremolo_rate, 5.5f, 0.1f, 12.0f);
  const float trem_depth = control_value(p->tremolo_depth, 0.35f, 0.0f, 1.0f);
  const float width = control_value(p->width, 0.65f, 0.0f, 1.0f);
  const float gain = control_value(p->gain, 0.70f, 0.0f, 1.0f);

  const float attack_coef = time_coef_ms(p, 0.5f + amp_attack * amp_attack * 1200.0f);
  const float decay_coef = time_coef_ms(p, 80.0f + amp_decay * amp_decay * 7500.0f);
  const float release_coef = time_coef_ms(p, 20.0f + amp_release * amp_release * 2500.0f);
  const float bar_coef = time_coef_ms(p, 220.0f + bar_decay * bar_decay * 9000.0f);
  const float gain_amp = gain * gain * 1.15f;

  for (uint32_t i = 0; i < n; ++i) {
    p->lfo_phase = wrap_positive(p->lfo_phase + kTwoPi * trem_rate / sr, kTwoPi);
    const float lfo = sinf(p->lfo_phase);
    const float trem = 1.0f - trem_depth * 0.5f + trem_depth * 0.5f * (lfo + 1.0f);

    const float a = render_voice(p, &p->voices[0], tone, hardness, strike, attack_coef, decay_coef,
                                 amp_sustain, release_coef, bar_coef);
    const float b = render_voice(p, &p->voices[1], tone, hardness, strike, attack_coef, decay_coef,
                                 amp_sustain, release_coef, bar_coef);
    const float pan = lfo * width * 0.28f;
    float dry_l = (a * (0.75f - pan) + b * (0.75f + pan)) * trem * gain_amp;
    float dry_r = (a * (0.75f + pan) + b * (0.75f - pan)) * trem * gain_amp;

    float spring_l = 0.0f;
    float spring_r = 0.0f;
    process_spring(p, dry_l, dry_r, &spring_l, &spring_r);

    float delay_l = 0.0f;
    float delay_r = 0.0f;
    process_delay(p, spring_l, spring_r, &delay_l, &delay_r);

#ifdef VIBRAPHONE_INSERT_PORTS
    delay_l += p->in_l ? finite_or(p->in_l[i], 0.0f) : 0.0f;
    delay_r += p->in_r ? finite_or(p->in_r[i], 0.0f) : 0.0f;
#endif

    p->out_l[i] = limit_output(delay_l);
    p->out_r[i] = limit_output(delay_r);
  }
}

static void cleanup(LV2_Handle instance) {
  Plugin* p = static_cast<Plugin*>(instance);
  if (p) {
    free(p->delay_l);
    free(p->delay_r);
  }
  free(instance);
}

static const LV2_Descriptor descriptor = {
  kUri, instantiate, connect_port, activate, run, nullptr, cleanup, nullptr
};

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
  return index == 0 ? &descriptor : nullptr;
}
