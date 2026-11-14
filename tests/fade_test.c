#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/fade.h"

#define NS_PER_MS UINT64_C(1000000)

static struct fade_config sample_config(void) {
  return (struct fade_config) {
    .enabled = true,
    .delay_s = 2.0f,
    .duration_ms = 300.0f,
  };
}

static bool config_equals(struct fade_config a, struct fade_config b) {
  return a.enabled == b.enabled
         && a.delay_s == b.delay_s
         && a.duration_ms == b.duration_ms;
}

static void test_defaults_keep_border_permanent(void) {
  struct fade_config config = FADE_CONFIG_DEFAULT;
  assert(!config.enabled);
  assert(config.delay_s == 2.0f);
  assert(config.duration_ms == 300.0f);
  // A disabled fade never lowers the alpha, however long the focus lasts.
  assert(fade_alpha_at(&config, UINT64_C(3600) * 1000 * NS_PER_MS) == 1.0f);
}

static void test_toggle_parsing(void) {
  struct fade_config config = FADE_CONFIG_DEFAULT;
  assert(fade_parse_argument("fade=on", &config) == FADE_ARGUMENT_VALID);
  assert(config.enabled);
  assert(fade_parse_argument("fade=off", &config) == FADE_ARGUMENT_VALID);
  assert(!config.enabled);

  struct fade_config before = config;
  assert(fade_parse_argument("fade=yes", &config) == FADE_ARGUMENT_INVALID);
  assert(fade_parse_argument("fade=", &config) == FADE_ARGUMENT_INVALID);
  assert(fade_parse_argument("fade=onx", &config) == FADE_ARGUMENT_INVALID);
  assert(config_equals(config, before));
}

static void test_delay_parsing_and_bounds(void) {
  struct fade_config config = FADE_CONFIG_DEFAULT;
  assert(fade_parse_argument("fade_delay=1.5", &config)
         == FADE_ARGUMENT_VALID);
  assert(config.delay_s == 1.5f);
  assert(fade_parse_argument("fade_delay=3600", &config)
         == FADE_ARGUMENT_VALID);
  assert(config.delay_s == 3600.0f);
  assert(fade_parse_argument("fade_delay=0.01", &config)
         == FADE_ARGUMENT_VALID);
  assert(config.delay_s == 0.01f);

  struct fade_config before = config;
  static const char* invalid[] = {
    "fade_delay=0",
    "fade_delay=-1",
    "fade_delay=3600.5",
    "fade_delay=",
    "fade_delay=abc",
    "fade_delay=2s",
    "fade_delay=nan",
    "fade_delay=inf",
    "fade_delay=1e40",
  };
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    assert(fade_parse_argument(invalid[i], &config)
           == FADE_ARGUMENT_INVALID);
    assert(config_equals(config, before));
  }
}

static void test_duration_parsing_and_bounds(void) {
  struct fade_config config = FADE_CONFIG_DEFAULT;
  assert(fade_parse_argument("fade_duration=400", &config)
         == FADE_ARGUMENT_VALID);
  assert(config.duration_ms == 400.0f);
  assert(fade_parse_argument("fade_duration=0", &config)
         == FADE_ARGUMENT_VALID);
  assert(config.duration_ms == 0.0f);
  assert(fade_parse_argument("fade_duration=10000", &config)
         == FADE_ARGUMENT_VALID);
  assert(config.duration_ms == 10000.0f);

  struct fade_config before = config;
  static const char* invalid[] = {
    "fade_duration=-1",
    "fade_duration=10001",
    "fade_duration=",
    "fade_duration=fast",
    "fade_duration=300ms",
    "fade_duration=nan",
  };
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    assert(fade_parse_argument(invalid[i], &config)
           == FADE_ARGUMENT_INVALID);
    assert(config_equals(config, before));
  }
}

static void test_unrelated_arguments_are_not_fade_arguments(void) {
  struct fade_config config = FADE_CONFIG_DEFAULT;
  struct fade_config before = config;
  static const char* unrelated[] = {
    "width=5.0",
    "faded=on",
    "fade",
    "fade_delays=2",
    "active_only=on",
  };
  for (size_t i = 0; i < sizeof(unrelated) / sizeof(unrelated[0]); ++i) {
    assert(fade_parse_argument(unrelated[i], &config) == FADE_ARGUMENT_NONE);
    assert(!fade_argument_is_fade(unrelated[i]));
  }
  assert(fade_parse_argument(NULL, &config) == FADE_ARGUMENT_NONE);
  assert(config_equals(config, before));

  assert(fade_argument_is_fade("fade=on"));
  assert(fade_argument_is_fade("fade_delay=2"));
  assert(fade_argument_is_fade("fade_duration=300"));
  // Malformed fade values still belong to the process-wide fade control.
  assert(fade_argument_is_fade("fade_delay=bad"));
}

static void test_parse_edges_keep_config_unchanged(void) {
  struct fade_config config = FADE_CONFIG_DEFAULT;
  struct fade_config before = config;
  static const char* invalid[] = {
    "fade=ON",
    "fade=1",
    "fade= on",
    "fade_delay=-0",
    "fade_delay=+2",
    "fade_delay= 2",
    "fade_delay=2 ",
    "fade_delay=0x10",
    "fade_delay=1e1",
    "fade_delay=1e-50",
    "fade_delay=3600.001",
    "fade_delay=.",
    "fade_duration=-0",
    "fade_duration=-0.1",
    "fade_duration=10000.01",
    "fade_duration=0x10",
  };
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    assert(fade_parse_argument(invalid[i], &config)
           == FADE_ARGUMENT_INVALID);
    assert(config_equals(config, before));
  }

  assert(fade_parse_argument("fade_delay=0.0001", &config)
         == FADE_ARGUMENT_VALID);
  assert(config.delay_s == 0.0001f);
  assert(fade_parse_argument("fade_delay=.5", &config)
         == FADE_ARGUMENT_VALID);
  assert(config.delay_s == 0.5f);
}

static void test_alpha_curve(void) {
  struct fade_config config = sample_config();
  uint64_t delay_ns = fade_delay_ns(&config);
  assert(delay_ns == UINT64_C(2000) * NS_PER_MS);

  assert(fade_alpha_at(&config, 0) == 1.0f);
  assert(fade_alpha_at(&config, delay_ns - 1) == 1.0f);
  assert(fade_alpha_at(&config, delay_ns) == 1.0f);

  float halfway = fade_alpha_at(&config, delay_ns + 150 * NS_PER_MS);
  assert(halfway > 0.49f && halfway < 0.51f);

  float earlier = fade_alpha_at(&config, delay_ns + 50 * NS_PER_MS);
  float later = fade_alpha_at(&config, delay_ns + 250 * NS_PER_MS);
  assert(earlier > halfway && halfway > later);

  assert(fade_alpha_at(&config, delay_ns + 300 * NS_PER_MS) == 0.0f);
  assert(fade_alpha_at(&config, delay_ns + 5000 * NS_PER_MS) == 0.0f);
  assert(fade_alpha_at(&config, UINT64_MAX) == 0.0f);
}

static void test_zero_duration_hides_at_once(void) {
  struct fade_config config = sample_config();
  config.duration_ms = 0.0f;
  uint64_t delay_ns = fade_delay_ns(&config);
  assert(fade_alpha_at(&config, delay_ns - 1) == 1.0f);
  assert(fade_alpha_at(&config, delay_ns) == 0.0f);
}

static void test_tokens_reject_stale_or_retired_borders(void) {
  assert(fade_token_matches(7, 7, true, false, true));
  // A newer focus restarted the cycle.
  assert(!fade_token_matches(7, 8, true, false, true));
  // The border lost focus, is being destroyed, or fade was disabled.
  assert(!fade_token_matches(7, 7, false, false, true));
  assert(!fade_token_matches(7, 7, true, true, true));
  assert(!fade_token_matches(7, 7, true, false, false));
  // A never-scheduled border (generation 0) never matches.
  assert(!fade_token_matches(0, 0, true, false, true));
}

static void test_generation_skips_zero(void) {
  assert(fade_next_generation(0) == 1);
  assert(fade_next_generation(41) == 42);
  assert(fade_next_generation(UINT64_MAX) == 1);
}

int main(void) {
  test_defaults_keep_border_permanent();
  test_toggle_parsing();
  test_delay_parsing_and_bounds();
  test_duration_parsing_and_bounds();
  test_unrelated_arguments_are_not_fade_arguments();
  test_parse_edges_keep_config_unchanged();
  test_alpha_curve();
  test_zero_duration_hides_at_once();
  test_tokens_reject_stale_or_retired_borders();
  test_generation_skips_zero();
  printf("fade tests passed\n");
  return 0;
}
