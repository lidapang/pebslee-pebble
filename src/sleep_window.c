#include "sleep_window.h"
#include <pebble.h>
#include "pebslee.h"
#include "language.h"  

// First time update date field
static int forceUpdateDate = YES;
static int mode = MODE_WORKDAY;
static int status = STATUS_NOTSLEEPING;

// BEGIN AUTO-GENERATED UI CODE; DO NOT MODIFY
static Window *s_window;
static GFont s_res_bitham_42_bold;
static GFont s_res_roboto_condensed_21;
static GFont s_res_bitham_30_black;
static TextLayer *s_tl_time;
static TextLayer *s_tl_date;
static TextLayer *s_tl_up_arrow;
static TextLayer *s_tl_status;
static TextLayer *s_tl_down_arrow;
static TextLayer *s_tl_mode;

static void initialise_ui(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_fullscreen(s_window, false);
  
  s_res_bitham_42_bold = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  s_res_roboto_condensed_21 = fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21);
  s_res_bitham_30_black = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
  // s_tl_time
  s_tl_time = text_layer_create(GRect(-2, 49, 147, 52));
  text_layer_set_text(s_tl_time, "00:00");
  text_layer_set_text_alignment(s_tl_time, GTextAlignmentCenter);
  text_layer_set_font(s_tl_time, s_res_bitham_42_bold);
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_tl_time);
  
  // s_tl_date
  s_tl_date = text_layer_create(GRect(-1, 125, 144, 27));
  text_layer_set_text(s_tl_date, "Wed 30");
  text_layer_set_text_alignment(s_tl_date, GTextAlignmentCenter);
  text_layer_set_font(s_tl_date, s_res_roboto_condensed_21);
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_tl_date);
  
  // s_tl_up_arrow
  s_tl_up_arrow = text_layer_create(GRect(124, 17, 20, 32));
  text_layer_set_background_color(s_tl_up_arrow, GColorClear);
  text_layer_set_text_color(s_tl_up_arrow, GColorWhite);
  text_layer_set_text(s_tl_up_arrow, ">");
  text_layer_set_text_alignment(s_tl_up_arrow, GTextAlignmentRight);
  text_layer_set_font(s_tl_up_arrow, s_res_bitham_30_black);
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_tl_up_arrow);
  
  // s_tl_status
  s_tl_status = text_layer_create(GRect(-1, 100, 144, 26));
  text_layer_set_background_color(s_tl_status, GColorClear);
  text_layer_set_text_color(s_tl_status, GColorWhite);
  text_layer_set_text(s_tl_status, "sleep");
  text_layer_set_text_alignment(s_tl_status, GTextAlignmentCenter);
  text_layer_set_font(s_tl_status, s_res_roboto_condensed_21);
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_tl_status);
  
  // s_tl_down_arrow
  s_tl_down_arrow = text_layer_create(GRect(120, 94, 23, 30));
  text_layer_set_background_color(s_tl_down_arrow, GColorClear);
  text_layer_set_text_color(s_tl_down_arrow, GColorWhite);
  text_layer_set_text(s_tl_down_arrow, ">");
  text_layer_set_text_alignment(s_tl_down_arrow, GTextAlignmentRight);
  text_layer_set_font(s_tl_down_arrow, s_res_bitham_30_black);
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_tl_down_arrow);
  
  // s_tl_mode
  s_tl_mode = text_layer_create(GRect(1, 21, 144, 28));
  text_layer_set_background_color(s_tl_mode, GColorClear);
  text_layer_set_text_color(s_tl_mode, GColorWhite);
  text_layer_set_text(s_tl_mode, "weekend");
  text_layer_set_text_alignment(s_tl_mode, GTextAlignmentCenter);
  text_layer_set_font(s_tl_mode, s_res_roboto_condensed_21);
  layer_add_child(window_get_root_layer(s_window), (Layer *)s_tl_mode);
}

static void destroy_ui(void) {
  window_destroy(s_window);
  text_layer_destroy(s_tl_time);
  text_layer_destroy(s_tl_date);
  text_layer_destroy(s_tl_up_arrow);
  text_layer_destroy(s_tl_status);
  text_layer_destroy(s_tl_down_arrow);
  text_layer_destroy(s_tl_mode);
}
// END AUTO-GENERATED UI CODE

// *********************** Update UI fuctions *********************
static void update_mode() {
  if (mode == MODE_WEEKEND) {
    text_layer_set_text(s_tl_mode, MODE_WEEKEND_STR);
  } else if (mode == MODE_WORKDAY) {
    text_layer_set_text(s_tl_mode, MODE_WORKDAY_STR);
  }
}

static void update_status() {
  if (status == STATUS_SLEEPING) {
    text_layer_set_text(s_tl_status, STATUS_SLEEPING_STR);
  } else if (status == STATUS_NOTSLEEPING) {
    text_layer_set_text(s_tl_status, STATUS_NOTSLEEPING_STR);
  }
}

static void update_time() {
  // Get a tm structure
  time_t temp = time(NULL); 
  struct tm *tick_time = localtime(&temp);

  // Create a long-lived buffer
  static char buffer[] = "00:00";
  static char bufferDate[] = "Mon 00";

  // Write the current hours and minutes into the buffer
  if(clock_is_24h_style() == true) {
    //Use 2h hour format
    strftime(buffer, sizeof("00:00"), "%H:%M", tick_time);
  } else {
    //Use 12 hour format
    strftime(buffer, sizeof("00:00"), "%I:%M", tick_time);
  }
  

  // Display this time on the TextLayer
  text_layer_set_text(s_tl_time, buffer);
  // Update date only when it becomes 00:00
  if ((tick_time->tm_hour == 0 && tick_time->tm_min == 0) || forceUpdateDate ) {
    strftime(bufferDate, sizeof("00:00"), "%a %d", tick_time);
    text_layer_set_text(s_tl_date, bufferDate);
    forceUpdateDate = NO;
  }
}

// *********************** Calculation functions **************************
static void calculate_mode() {
  // Get a tm structure
  time_t temp = time(NULL); 
  struct tm *tick_time = localtime(&temp);
  
  if ((tick_time->tm_wday == 5 && tick_time->tm_hour >= 13) ||   // Friday evening
      (tick_time->tm_wday == 6 && tick_time->tm_hour >= 13) ||   // Saturday
     (tick_time->tm_wday == 0 && tick_time->tm_hour <= 12))     // Sunday morning
     mode = MODE_WEEKEND;
  else
     mode = MODE_WORKDAY; 
}


// *********************** Window and click handlers ***********************
static void handle_window_unload(Window* window) {
  destroy_ui();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

void click_handler_up(ClickRecognizerRef recognizer, void *context) {
  if (mode == MODE_WEEKEND) {
    mode = MODE_WORKDAY;
  } else {
    mode = MODE_WEEKEND;
  }   
  update_mode();
}

void click_handler_down(ClickRecognizerRef recognizer, void *context) {
  if (status == STATUS_SLEEPING) {
    status = STATUS_NOTSLEEPING;
  } else {
    status = STATUS_SLEEPING;
  }   
  update_status();
}


void config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, click_handler_up);
  //window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, click_handler_down);
  //window_single_click_subscribe(BUTTON_ID_BACK, select_click_handler);
}

void show_sleep_window(void) {
  initialise_ui();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .unload = handle_window_unload,
  });
  window_stack_push(s_window, true);
  if (forceUpdateDate) {
    calculate_mode();
  }
  update_mode();
  update_time();
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  window_set_click_config_provider(s_window, config_provider);
}

void hide_sleep_window(void) {
  window_stack_remove(s_window, true);
}


