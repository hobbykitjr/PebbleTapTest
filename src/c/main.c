/**
 * Tap Diagnostic — Tests accelerometer tap detection on Pebble
 *
 * Displays live accel data, counts tap service events,
 * detects raw acceleration spikes, and logs results.
 * Share the on-screen summary to report issues.
 *
 * Controls:
 *   UP: cycle through screens (Live / Log / Summary)
 *   DOWN: reset counters
 *   SELECT: currently does nothing (reserved)
 *   BACK: exit app
 *
 * Open source: https://github.com/hobbykitjr/PebbleTapTest
 */

#include <pebble.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// SCREENS
// ============================================================================
enum { SCREEN_LIVE, SCREEN_LOG, SCREEN_SUMMARY, SCREEN_COUNT };

// ============================================================================
// GLOBALS
// ============================================================================
static Window *s_win;
static Layer *s_canvas;
static int s_screen = SCREEN_LIVE;

// Accel data state
static int16_t s_accel_x = 0, s_accel_y = 0, s_accel_z = 0;
static int s_data_samples = 0;       // total samples received
static int s_data_batches = 0;       // total batches received

// Tap service state
static int s_tap_count = 0;          // firmware tap events
static int s_tap_last_axis = -1;     // last tap axis (0=X,1=Y,2=Z)
static int s_tap_last_dir = 0;       // last tap direction

// Raw spike detection
#define SPIKE_THRESHOLDS 4
static const int16_t THRESHOLDS[SPIKE_THRESHOLDS] = {800, 1200, 1800, 2500};
static int s_spike_counts[SPIKE_THRESHOLDS] = {0, 0, 0, 0};
static int16_t s_peak_ever = 0;      // highest spike seen
static int16_t s_prev_x = 0, s_prev_y = 0, s_prev_z = 0;

// Custom tap detection (same as BaseCamp)
#define TAP_THRESHOLD    1800
#define TAP_COOLDOWN_MS  400
static uint32_t s_last_raw_tap = 0;
static int s_raw_tap_count = 0;      // our custom detection count

// Log buffer (last N events)
#define LOG_LINES 8
#define LOG_LINE_LEN 32
static char s_log[LOG_LINES][LOG_LINE_LEN];
static int s_log_idx = 0;

// Timing
static uint32_t s_start_time = 0;
static uint32_t s_last_tap_time_ms = 0;

// ============================================================================
// HELPERS
// ============================================================================
static uint32_t now_ms(void) {
  time_t t; uint16_t ms;
  time_ms(&t, &ms);
  return (uint32_t)(t * 1000 + ms);
}

static void log_add(const char *msg) {
  strncpy(s_log[s_log_idx], msg, LOG_LINE_LEN - 1);
  s_log[s_log_idx][LOG_LINE_LEN - 1] = '\0';
  s_log_idx = (s_log_idx + 1) % LOG_LINES;
}

static int elapsed_sec(void) {
  return (int)((now_ms() - s_start_time) / 1000);
}

// ============================================================================
// ACCEL DATA HANDLER
// ============================================================================
static void accel_data_handler(AccelData *data, uint32_t num_samples) {
  s_data_batches++;
  for(uint32_t i = 0; i < num_samples; i++) {
    s_data_samples++;
    if(data[i].did_vibrate) continue;

    s_accel_x = data[i].x;
    s_accel_y = data[i].y;
    s_accel_z = data[i].z;

    // Compute delta from previous sample
    int16_t dx = data[i].x - s_prev_x;
    int16_t dy = data[i].y - s_prev_y;
    int16_t dz = data[i].z - s_prev_z;
    int16_t adx = dx < 0 ? -dx : dx;
    int16_t ady = dy < 0 ? -dy : dy;
    int16_t adz = dz < 0 ? -dz : dz;
    int16_t peak = adx > ady ? adx : ady;
    if(adz > peak) peak = adz;

    s_prev_x = data[i].x;
    s_prev_y = data[i].y;
    s_prev_z = data[i].z;

    // Track peak ever
    if(peak > s_peak_ever) s_peak_ever = peak;

    // Count spikes at each threshold
    for(int t = 0; t < SPIKE_THRESHOLDS; t++) {
      if(peak >= THRESHOLDS[t]) s_spike_counts[t]++;
    }

    // Custom tap detection
    if(peak >= TAP_THRESHOLD) {
      uint32_t n = now_ms();
      if(n - s_last_raw_tap >= TAP_COOLDOWN_MS) {
        s_last_raw_tap = n;
        s_raw_tap_count++;
        { char lb[LOG_LINE_LEN];
          snprintf(lb, sizeof(lb), "RAW @%ds pk=%d", elapsed_sec(), (int)peak);
          log_add(lb); }
      }
    }
  }
  if(s_screen == SCREEN_LIVE && s_canvas) layer_mark_dirty(s_canvas);
}

// ============================================================================
// TAP SERVICE HANDLER
// ============================================================================
static void tap_handler(AccelAxisType axis, int32_t direction) {
  s_tap_count++;
  s_tap_last_axis = (int)axis;
  s_tap_last_dir = (int)direction;
  s_last_tap_time_ms = now_ms();
  { char lb[LOG_LINE_LEN];
    snprintf(lb, sizeof(lb), "TAP @%ds axis=%d dir=%d", elapsed_sec(), (int)axis, (int)direction);
    log_add(lb); }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

// ============================================================================
// DRAW: LIVE SCREEN
// ============================================================================
static void draw_live(GContext *ctx, int w, int h) {
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GFont f_md = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_lg = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  int pad = PBL_IF_ROUND_ELSE(28, 4);
  int y = pad;
  char buf[48];

  // Title
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "TAP DIAGNOSTIC", f_md,
    GRect(0, y, w, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  y += 24;

  // Live accel values
  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, GColorCyan);
  #endif
  snprintf(buf, sizeof(buf), "X:%d Y:%d Z:%d", s_accel_x, s_accel_y, s_accel_z);
  graphics_draw_text(ctx, buf, f_sm,
    GRect(pad, y, w - pad*2, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  y += 18;

  // Data flow indicator
  snprintf(buf, sizeof(buf), "Samples:%d  Batches:%d", s_data_samples, s_data_batches);
  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, buf, f_sm,
    GRect(pad, y, w - pad*2, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  y += 22;

  // Tap counts - big and prominent
  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, GColorGreen);
  #else
  graphics_context_set_text_color(ctx, GColorWhite);
  #endif
  snprintf(buf, sizeof(buf), "FW Tap: %d", s_tap_count);
  graphics_draw_text(ctx, buf, f_lg,
    GRect(pad, y, w - pad*2, 28), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  y += 30;

  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, GColorYellow);
  #endif
  snprintf(buf, sizeof(buf), "Raw Tap: %d", s_raw_tap_count);
  graphics_draw_text(ctx, buf, f_lg,
    GRect(pad, y, w - pad*2, 28), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  y += 30;

  // Peak
  graphics_context_set_text_color(ctx, GColorWhite);
  snprintf(buf, sizeof(buf), "Peak: %dmG", (int)s_peak_ever);
  graphics_draw_text(ctx, buf, f_md,
    GRect(pad, y, w - pad*2, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  y += 24;

  // Navigation hint
  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, "UP:next  DN:reset", f_sm,
    GRect(0, h - PBL_IF_ROUND_ELSE(28, 16), w, 16),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: LOG SCREEN
// ============================================================================
static void draw_log(GContext *ctx, int w, int h) {
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GFont f_md = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  int pad = PBL_IF_ROUND_ELSE(28, 4);
  int y = pad;

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "EVENT LOG", f_md,
    GRect(0, y, w, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  y += 26;

  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, GColorCyan);
  #endif

  // Show log entries newest first
  for(int i = 0; i < LOG_LINES; i++) {
    int idx = (s_log_idx - 1 - i + LOG_LINES) % LOG_LINES;
    if(s_log[idx][0] == '\0') continue;
    graphics_draw_text(ctx, s_log[idx], f_sm,
      GRect(pad, y, w - pad*2, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    y += 16;
    if(y > h - 20) break;
  }

  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, "UP:next  DN:reset", f_sm,
    GRect(0, h - PBL_IF_ROUND_ELSE(28, 16), w, 16),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: SUMMARY SCREEN (screenshot-friendly for sharing)
// ============================================================================
static void draw_summary(GContext *ctx, int w, int h) {
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GFont f_md = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  int pad = PBL_IF_ROUND_ELSE(30, 6);
  int y = pad;
  char buf[48];

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "TAP REPORT", f_md,
    GRect(0, y, w, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  y += 24;

  // Time elapsed
  snprintf(buf, sizeof(buf), "Time: %ds", elapsed_sec());
  graphics_draw_text(ctx, buf, f_sm,
    GRect(pad, y, w - pad*2, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += 16;

  // Data flow
  snprintf(buf, sizeof(buf), "Accel data: %s", s_data_samples > 0 ? "YES" : "NO");
  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, s_data_samples > 0 ? GColorGreen : GColorRed);
  #endif
  graphics_draw_text(ctx, buf, f_sm,
    GRect(pad, y, w - pad*2, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += 16;

  // Samples
  graphics_context_set_text_color(ctx, GColorWhite);
  snprintf(buf, sizeof(buf), "Samples: %d", s_data_samples);
  graphics_draw_text(ctx, buf, f_sm,
    GRect(pad, y, w - pad*2, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += 18;

  // FW tap
  snprintf(buf, sizeof(buf), "FW tap events: %d", s_tap_count);
  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, s_tap_count > 0 ? GColorGreen : GColorRed);
  #endif
  graphics_draw_text(ctx, buf, f_sm,
    GRect(pad, y, w - pad*2, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += 16;

  // Raw tap
  snprintf(buf, sizeof(buf), "Raw tap (1800): %d", s_raw_tap_count);
  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, s_raw_tap_count > 0 ? GColorGreen : GColorRed);
  #endif
  graphics_draw_text(ctx, buf, f_sm,
    GRect(pad, y, w - pad*2, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += 18;

  // Spike breakdown
  graphics_context_set_text_color(ctx, GColorWhite);
  snprintf(buf, sizeof(buf), "Spikes >800: %d", s_spike_counts[0]);
  graphics_draw_text(ctx, buf, f_sm,
    GRect(pad, y, w - pad*2, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += 16;
  snprintf(buf, sizeof(buf), "Spikes >1200: %d", s_spike_counts[1]);
  graphics_draw_text(ctx, buf, f_sm,
    GRect(pad, y, w - pad*2, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += 16;
  snprintf(buf, sizeof(buf), "Spikes >1800: %d", s_spike_counts[2]);
  graphics_draw_text(ctx, buf, f_sm,
    GRect(pad, y, w - pad*2, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += 16;
  snprintf(buf, sizeof(buf), "Spikes >2500: %d", s_spike_counts[3]);
  graphics_draw_text(ctx, buf, f_sm,
    GRect(pad, y, w - pad*2, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += 18;

  // Peak
  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, GColorYellow);
  #endif
  snprintf(buf, sizeof(buf), "Peak delta: %dmG", (int)s_peak_ever);
  graphics_draw_text(ctx, buf, f_md,
    GRect(pad, y, w - pad*2, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += 24;

  // Hint
  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, "Screenshot this!", f_sm,
    GRect(0, h - PBL_IF_ROUND_ELSE(28, 16), w, 16),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ============================================================================
// CANVAS
// ============================================================================
static void canvas_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int w = b.size.w, h = b.size.h;

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, w, h), 0, GCornerNone);

  switch(s_screen) {
    case SCREEN_LIVE:    draw_live(ctx, w, h); break;
    case SCREEN_LOG:     draw_log(ctx, w, h); break;
    case SCREEN_SUMMARY: draw_summary(ctx, w, h); break;
  }
}

// ============================================================================
// TIMER - refresh live screen periodically
// ============================================================================
static AppTimer *s_refresh_timer = NULL;

static void refresh_cb(void *data) {
  s_refresh_timer = NULL;
  if(s_canvas) layer_mark_dirty(s_canvas);
  s_refresh_timer = app_timer_register(200, refresh_cb, NULL);
}

// ============================================================================
// BUTTONS
// ============================================================================
static void up_click(ClickRecognizerRef ref, void *ctx) {
  (void)ref; (void)ctx;
  s_screen = (s_screen + 1) % SCREEN_COUNT;
  layer_mark_dirty(s_canvas);
}

static void down_click(ClickRecognizerRef ref, void *ctx) {
  (void)ref; (void)ctx;
  // Reset all counters
  s_tap_count = 0;
  s_raw_tap_count = 0;
  s_data_samples = 0;
  s_data_batches = 0;
  s_peak_ever = 0;
  memset(s_spike_counts, 0, sizeof(s_spike_counts));
  memset(s_log, 0, sizeof(s_log));
  s_log_idx = 0;
  s_start_time = now_ms();
  log_add("RESET");
  layer_mark_dirty(s_canvas);
}

static void back_click(ClickRecognizerRef ref, void *ctx) {
  (void)ref; (void)ctx;
  window_stack_pop(true);
}

static void click_config(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

// ============================================================================
// WINDOW
// ============================================================================
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, canvas_proc);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  if(s_refresh_timer) { app_timer_cancel(s_refresh_timer); s_refresh_timer = NULL; }
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

// ============================================================================
// INIT / DEINIT
// ============================================================================
static void init(void) {
  s_start_time = now_ms();
  memset(s_log, 0, sizeof(s_log));

  s_win = window_create();
  window_set_background_color(s_win, GColorBlack);
  window_set_click_config_provider(s_win, click_config);
  window_set_window_handlers(s_win, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_win, true);

  // Subscribe to both services
  accel_tap_service_subscribe(tap_handler);
  accel_data_service_subscribe(5, accel_data_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_25HZ);

  log_add("START");

  // Periodic refresh
  s_refresh_timer = app_timer_register(200, refresh_cb, NULL);
}

static void deinit(void) {
  accel_data_service_unsubscribe();
  accel_tap_service_unsubscribe();
  window_destroy(s_win);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
