// The MIT License (MIT)
//
// Copyright (c) 2014 Nick Penkov <nick at npenkov dot org>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <pebble.h>
#include "logic.h"
#include "sleep_window.h"
#include "sleep_stats.h"
#include "comm.h"
#include "persistence.h"
#include "localize.h"

static uint8_t vib_count;
static bool alarm_in_motion = NO;
static AppTimer *alarm_timer;

const int ALARM_TIME_BETWEEN_ITERATIONS = 5000; // 5 sec
const int ALARM_MAX_ITERATIONS = 10; // Vibrate max 10 times

static GlobalConfig config;
static uint16_t motion_peek_in_min = 0;
static int app_active = NO;

static int16_t last_x = 0;
static int16_t last_y = 0;
static int16_t last_z = 0;

// The interval we collect values
const int ACCEL_STEP_MS = 300;

const int DELTA = 0;
// For debugging purposes - this is the interval that current state is printed in console
const int REPORTING_STEP_MS = 20000;

#define DEEP_SLEEP_THRESHOLD 100
#define REM_SLEEP_THRESHOLD 101
#define LIGHT_THRESHOLD 800

// Start with this value down
#define START_PEEK_MOTION 1000

// The number of value types/thresholds
#define COUNT_TRESHOLDS 5

static int thresholds[COUNT_TRESHOLDS] = { 0, DEEP_SLEEP_THRESHOLD, REM_SLEEP_THRESHOLD, LIGHT_THRESHOLD, 65535 };

const uint8_t LAST_MIN_WAKE = 2;

static SleepData sleep_data;

static SleepPhases current_sleep_phase;

static AppTimer *timer;

#ifdef DEBUG
static AppTimer *timerRep;
#endif

GlobalConfig *get_config() {
    return &config;
}

SleepData *get_sleep_data() {
    return &sleep_data;
}


void set_config_mode(int a_mode) {
    config.mode = a_mode;
}
void set_config_status(int a_status) {
    config.status = a_status;
}
void set_config_start_time(uint8_t a_hour, uint8_t a_min) {
    config.start_wake_hour = a_hour;
    config.start_wake_min = a_min;
}
void set_config_end_time(uint8_t a_hour, uint8_t a_min) {
    config.end_wake_hour = a_hour;
    config.end_wake_min = a_min;
}
void set_config_up_coef(int coef) {
    config.up_coef = coef;
}
void set_config_down_coef(int coef) {
    config.down_coef = coef;
}
void set_config_snooze(char snooze) {
    config.snooze  = snooze;
}


void persist_write_config() {
#ifdef DEBUG
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Persist config with up/down : %d/%d", config.up_coef, config.down_coef);
#endif
    persist_write_data(CONFIG_PERSISTENT_KEY, &config, sizeof(config));
}
void persist_read_config() {
    persist_read_data(CONFIG_PERSISTENT_KEY, &config, sizeof(config));
    if (config.up_coef != UP_COEF_NOTSENSITIVE &&
        config.up_coef != UP_COEF_NORMAL &&
        config.up_coef != UP_COEF_VERYSENSITIVE) {
        config.up_coef = UP_COEF_NORMAL;
    }
    if (config.down_coef != DOWN_COEF_SLOW &&
        config.down_coef != DOWN_COEF_NORMAL &&
        config.down_coef != DOWN_COEF_FAST) {
        config.down_coef = DOWN_COEF_NORMAL;
    }
#ifdef DEBUG
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Read config with up/down : %d/%d", config.up_coef, config.down_coef);
#endif
}

void start_sleep_data_capturing() {
    time_t temp = time(NULL);
    sleep_data.start_time = temp;
    sleep_data.finished = false;
    
    for (int i = 0; i < COUNT_PHASES; i++)
        sleep_data.stat[i] = 0;
    
    //uint8_t *minutes_value;
    sleep_data.count_values = 0;
    sleep_data.minutes_value[sleep_data.count_values] = START_PEEK_MOTION;
    current_sleep_phase = AWAKE;
#ifdef DEBUG
    time_t t1 = sleep_data.start_time;
    struct tm *tt = localtime(&t1);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "* == Start capturing ==");
    APP_LOG(APP_LOG_LEVEL_DEBUG, "* Started:      %02d:%02d", tt->tm_hour, tt->tm_min);
#endif
}

#ifdef DEBUG
static char* decode_phase(int a_phase) {
    switch (a_phase) {
        case DEEP:
            return _("Deep sleep");
        case REM:
            return _("REM sleep");
        case LIGHT:
            return _("Light sleep");
        case AWAKE:
            return _("Awake");
        default:
            return _("unknown");
            break;
    }
}

static void dump_current_state() {
    time_t t1 = sleep_data.start_time;
    struct tm *tt = localtime(&t1);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "* Started:      %02d:%02d", tt->tm_hour, tt->tm_min);
    for (int i = 0; i < COUNT_PHASES; i ++) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "* %s:  %d min", decode_phase(i+1), sleep_data.stat[i]);
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "* Count values: %d", sleep_data.count_values);
}
#endif

void stop_sleep_data_capturing() {
    if (sleep_data.finished == false) {
        time_t temp;
        time(&temp);
        sleep_data.end_time = temp;
        sleep_data.finished = true;
        
        store_data(&sleep_data);
        
#ifdef DEBUG
        APP_LOG(APP_LOG_LEVEL_DEBUG, "* == Stop capturing ==");
        time_t t2 = sleep_data.end_time;
        struct tm *tte = localtime(&t2);
        APP_LOG(APP_LOG_LEVEL_DEBUG, "* Ended:        %02d:%02d", tte->tm_hour, tte->tm_min);
        dump_current_state();
#endif
        show_sleep_stats();
    }
}

void increase_start_hour() {
    if (config.start_wake_hour == 23) {
        config.start_wake_hour = 0;
    } else {
        config.start_wake_hour = config.start_wake_hour + 1;
    }
}
void increase_start_min() {
    if (config.start_wake_min == 59) {
        config.start_wake_min = 0;
    } else {
        config.start_wake_min = config.start_wake_min + 1;
    }
    
}
void increase_end_hour() {
    if (config.end_wake_hour == 23) {
        config.end_wake_hour = 0;
    } else {
        config.end_wake_hour = config.end_wake_hour + 1;
    }
}
void increase_end_min() {
    if (config.end_wake_min == 59) {
        config.end_wake_min = 0;
    } else {
        config.end_wake_min = config.end_wake_min + 1;
    }
    
}

void decrease_start_hour() {
    if (config.start_wake_hour == 0) {
        config.start_wake_hour = 23;
    } else {
        config.start_wake_hour = config.start_wake_hour - 1;
    }
}
void decrease_start_min() {
    if (config.start_wake_min == 0) {
        config.start_wake_min = 59;
    } else {
        config.start_wake_min = config.start_wake_min - 1;
    }
    
}
void decrease_end_hour() {
    if (config.end_wake_hour == 0) {
        config.end_wake_hour = 23;
    } else {
        config.end_wake_hour = config.end_wake_hour - 1;
    }
}
void decrease_end_min() {
    if (config.end_wake_min == 0) {
        config.end_wake_min = 59;
    } else {
        config.end_wake_min = config.end_wake_min - 1;
    }
    
}

static void memo_motion(uint16_t peek) {
    if (peek > motion_peek_in_min)
        motion_peek_in_min = peek;
}


/*
 * Process motion data, and take the peeks of body motion
 */
static void motion_timer_callback(void *data) {
    AccelData accel = (AccelData ) { .x = 0, .y = 0, .z = 0 };
    accel_service_peek(&accel);
    
    // Not interested in values from vibration
    if (accel.did_vibrate) {
        // Log 0 to keep the frequency
        memo_motion(0);
        return;
    }
    int16_t delta_x;
    int16_t delta_y;
    int16_t delta_z;
    
    if (last_x == 0 && last_y == 0 && last_z == 0) {
        delta_x = 0;
        delta_y = 0;
        delta_z = 0;
        // We don't know if there is a motion, when last values are initial
    } else {
        delta_x = abs(accel.x - last_x);
        delta_y = abs(accel.y - last_y);
        delta_z = abs(accel.z - last_z);
        
        // Don't take into account value that are less than delta
        if (delta_x < DELTA)
            delta_x = 0;
        
        if (delta_y < DELTA)
            delta_y = 0;
        
        if (delta_z < DELTA)
            delta_z = 0;
        
        uint16_t delta_value = (delta_x + delta_y + delta_z)/3;
        
        memo_motion(delta_value);
    }
    
    last_x = accel.x;
    last_y = accel.y;
    last_z = accel.z;
    
    timer = app_timer_register(ACCEL_STEP_MS, motion_timer_callback, NULL);
}

#ifdef DEBUG
static void reporting_timer_callback(void *data) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Motion peek: %u for vector: %d/%d/%d", motion_peek_in_min, last_x, last_y, last_z);
    timerRep = app_timer_register(REPORTING_STEP_MS, reporting_timer_callback, NULL);
}
#endif

/*
 * Alarm timer loop
 */
static void recure_alarm() {
    if (vib_count >= ALARM_MAX_ITERATIONS) {
        alarm_in_motion = NO;
        return;
    }
    
    // Vibrate
    vibes_long_pulse();
    
    alarm_timer = app_timer_register(ALARM_TIME_BETWEEN_ITERATIONS, recure_alarm, NULL);
    
    vib_count++;
    
    if (vib_count % 3 == 0) {
        light_enable_interaction();
    }
}

static void execute_alarm() {
#ifdef DEBUG
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Execute alarm");
#endif
    stop_motion_capturing();
    app_active = NO;
    vib_count = 0;
    alarm_in_motion = YES;
    config.status = STATUS_NOTACTIVE;
    recure_alarm();
}

void call_stop_alarm_if_running() {
    if (alarm_in_motion) {
        stop_sleep_data_capturing();
        app_timer_cancel(alarm_timer);
        alarm_in_motion = NO;
        refresh_display();
    }
}

void check_alarm() {
    if (config.mode == MODE_WORKDAY) {
        time_t t1;
        time(&t1);
        struct tm *tt = localtime(&t1);
        int h = tt->tm_hour;
        int m = tt->tm_min;
        
        // We have active alarm
        // so check interval
        if (h >= config.start_wake_hour &&
            h <= config.end_wake_hour) {
            
            bool inTime = YES;
            if (h == config.start_wake_hour) {
                if (m >= config.start_wake_min)
                    inTime = YES;
                else
                    return;
            }
            
            if (h == config.end_wake_hour) {
                if (m <= config.end_wake_min)
                    inTime = YES;
                else
                    return;
            }
            
            if (inTime && current_sleep_phase == LIGHT) {
                execute_alarm();
                return;
            }
            // Check the last delta minutes
            uint8_t delta_time_h = config.end_wake_hour;
            uint8_t delta_time_m = config.end_wake_min;
            if (config.end_wake_min - LAST_MIN_WAKE < 0 ) {
                if (delta_time_h == 0)
                    delta_time_h = 23;
                delta_time_m = 60 + (config.end_wake_min - LAST_MIN_WAKE);
            } else {
                delta_time_m = config.end_wake_min - LAST_MIN_WAKE;
            }
            
            if (delta_time_h < h || (delta_time_h == h && delta_time_m < m)) {
                execute_alarm();
                return;
            }
        }
    }
}

static void persist_motion() {
    if (sleep_data.count_values >= MAX_COUNT-1)
        return;
    uint16_t prev_value = sleep_data.minutes_value[sleep_data.count_values];
    
    int med_val = abs(motion_peek_in_min - prev_value)/2;
    uint16_t median_peek = (motion_peek_in_min - prev_value) > 0
    ? prev_value + (med_val*((float)config.up_coef/10))
    : prev_value - (med_val*((float)config.down_coef/10));
    
    for (int i = 1; i < COUNT_TRESHOLDS; i++) {
        if (median_peek > thresholds[i-1] && median_peek <= thresholds[i]) {
            current_sleep_phase = i;
            break;
        }
    }
    sleep_data.stat[current_sleep_phase-1] += 1;
    
    sleep_data.count_values += 1;
    
    // Store modified motion data
    sleep_data.minutes_value[sleep_data.count_values] = median_peek;
    // Alternative - store original value
    //sleep_data.minutes_value[sleep_data.count_values] = motion_peek_in_min;
    
#ifdef DEBUG
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Persist motion %u/%d/%u - sleep phase: %s", motion_peek_in_min, med_val, median_peek, decode_phase(current_sleep_phase));
    APP_LOG(APP_LOG_LEVEL_DEBUG, "* == Sleep data ==");
    dump_current_state();
#endif
    motion_peek_in_min = 0;
}

/*
 * Here is the main logic of the application. If active - every minute get/calculate and store
 * motion data, check for wake time and trigger alarm if necessary.
 * If not active - listen for configuration/communication events
 */
void minute_timer_tick() {
    if (app_active) {
        persist_motion();
        check_alarm();
    }
}

void notify_status_update(int a_status) {
    if (a_status == STATUS_ACTIVE) {
        vibes_short_pulse();
        start_motion_capturing();
        start_sleep_data_capturing();
        app_active = YES;
    } else if (a_status == STATUS_NOTACTIVE) {
        vibes_double_pulse();
        stop_motion_capturing();
        stop_sleep_data_capturing();
        app_active = NO;
    }
}

void notify_mode_update(int a_mode) {
    // if (a_mode == MODE_WORKDAY) {
    //   vibes_short_pulse();
    // } else if (a_mode == MODE_WEEKEND) {
    //   vibes_double_pulse();
    // }
}

void start_motion_capturing() {
    motion_peek_in_min = 0;
    AppWorkerResult result = app_worker_launch();
    timer = app_timer_register(ACCEL_STEP_MS, motion_timer_callback, NULL);
    
#ifdef DEBUG
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Start motion capturing");
    timerRep = app_timer_register(REPORTING_STEP_MS, reporting_timer_callback, NULL);
#endif
}

void stop_motion_capturing() {
#ifdef DEBUG
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Stop motion capturing");
    app_timer_cancel(timerRep);
#endif
    app_timer_cancel(timer);

    // Stop the background worker
    AppWorkerResult result = app_worker_kill();
}