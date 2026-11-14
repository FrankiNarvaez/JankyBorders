#pragma once

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Process-wide fade-out of the focused border. The border stays fully
// visible for delay_s seconds after it gains focus, then its alpha falls
// linearly to zero over duration_ms. Only a new focus shows it again.
struct fade_config {
  bool enabled;
  float delay_s;
  float duration_ms;
};

#define FADE_DEFAULT_DELAY_S 2.0f
#define FADE_DEFAULT_DURATION_MS 300.0f
#define FADE_MAX_DELAY_S 3600.0f
#define FADE_MAX_DURATION_MS 10000.0f
// Frame interval used while the alpha is ramping down (about 60 Hz).
#define FADE_STEP_NS UINT64_C(16000000)

#define FADE_CONFIG_DEFAULT { .enabled = false,                    \
                              .delay_s = FADE_DEFAULT_DELAY_S,     \
                              .duration_ms = FADE_DEFAULT_DURATION_MS }

enum fade_argument_result {
  FADE_ARGUMENT_NONE = 0,
  FADE_ARGUMENT_VALID,
  FADE_ARGUMENT_INVALID,
};

static inline bool fade_parse_bounded_float(const char* number,
                                            float minimum,
                                            bool minimum_inclusive,
                                            float maximum,
                                            float* value) {
  // Plain decimals only: no sign, whitespace, exponent, hex, nan or inf.
  if (!number || *number == '\0'
      || strspn(number, "0123456789.") != strlen(number)
      || strspn(number, ".") == strlen(number)) {
    return false;
  }

  errno = 0;
  char* end = NULL;
  float parsed = strtof(number, &end);
  if (errno == ERANGE
      || !end
      || end == number
      || *end != '\0'
      || !isfinite(parsed)
      || parsed > maximum
      || (minimum_inclusive ? parsed < minimum : parsed <= minimum)) {
    return false;
  }

  *value = parsed;
  return true;
}

// Returns NONE for arguments that are not fade options. INVALID leaves the
// configuration unchanged.
static inline enum fade_argument_result fade_parse_argument(
    const char* argument,
    struct fade_config* config) {
  static const char toggle[] = "fade=";
  static const char delay[] = "fade_delay=";
  static const char duration[] = "fade_duration=";
  if (!argument || !config) return FADE_ARGUMENT_NONE;

  if (strncmp(argument, toggle, sizeof(toggle) - 1) == 0) {
    const char* value = argument + sizeof(toggle) - 1;
    if (strcmp(value, "on") == 0) {
      config->enabled = true;
      return FADE_ARGUMENT_VALID;
    }
    if (strcmp(value, "off") == 0) {
      config->enabled = false;
      return FADE_ARGUMENT_VALID;
    }
    return FADE_ARGUMENT_INVALID;
  }

  if (strncmp(argument, delay, sizeof(delay) - 1) == 0) {
    return fade_parse_bounded_float(argument + sizeof(delay) - 1,
                                    0.0f,
                                    false,
                                    FADE_MAX_DELAY_S,
                                    &config->delay_s)
           ? FADE_ARGUMENT_VALID
           : FADE_ARGUMENT_INVALID;
  }

  if (strncmp(argument, duration, sizeof(duration) - 1) == 0) {
    return fade_parse_bounded_float(argument + sizeof(duration) - 1,
                                    0.0f,
                                    true,
                                    FADE_MAX_DURATION_MS,
                                    &config->duration_ms)
           ? FADE_ARGUMENT_VALID
           : FADE_ARGUMENT_INVALID;
  }

  return FADE_ARGUMENT_NONE;
}

static inline bool fade_argument_is_fade(const char* argument) {
  struct fade_config scratch = FADE_CONFIG_DEFAULT;
  return fade_parse_argument(argument, &scratch) != FADE_ARGUMENT_NONE;
}

static inline uint64_t fade_delay_ns(const struct fade_config* config) {
  if (!config || config->delay_s <= 0.0f) return 0;
  return (uint64_t)((double)config->delay_s * 1e9);
}

// Alpha of the focused border elapsed_ns after it gained focus.
static inline float fade_alpha_at(const struct fade_config* config,
                                  uint64_t elapsed_ns) {
  if (!config || !config->enabled) return 1.0f;

  uint64_t delay = fade_delay_ns(config);
  if (elapsed_ns < delay) return 1.0f;

  double duration_ns = (double)config->duration_ms * 1e6;
  if (duration_ns <= 0.0) return 0.0f;

  double progress = (double)(elapsed_ns - delay) / duration_ns;
  if (progress >= 1.0) return 0.0f;
  return (float)(1.0 - progress);
}

// Delayed fade steps never retain a border pointer. They carry the window ID
// and the generation that was current when the focus began; any newer focus,
// loss of focus, destruction or disabling of fade invalidates them.
static inline bool fade_token_matches(uint64_t token_generation,
                                      uint64_t border_generation,
                                      bool focused,
                                      bool destroying,
                                      bool enabled) {
  return token_generation != 0
         && token_generation == border_generation
         && focused
         && !destroying
         && enabled;
}

static inline uint64_t fade_next_generation(uint64_t generation) {
  return generation == UINT64_MAX ? 1 : generation + 1;
}
