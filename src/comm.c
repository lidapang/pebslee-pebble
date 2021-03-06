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

#include <stdio.h>
#include <pebble.h>

#include "comm.h"
#include "logic.h"
#include "persistence.h"
#include "syncprogress_window.h"
#include "localize.h"
#include "sleep_window.h"

// ================== Communication ======================
static AppTimer *timerSync;
static AppTimer *timerSend;

static bool sync_start = false;
static bool sync_in_progress = false;

const int SYNC_STEP_MS = 3000;
const int SEND_STEP_MS = 100;
const int MAX_SEND_VALS = 40;

static uint32_t message_outbox_size = 0;

static SendData sendData;

static void send_header_data() {
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);

    Tuplet value_start = TupletInteger(PS_APP_MSG_HEADER_START, sendData.start_time);
    dict_write_tuplet(iter, &value_start);
    Tuplet value_end = TupletInteger(PS_APP_MSG_HEADER_END, sendData.end_time);
    dict_write_tuplet(iter, &value_end);
    Tuplet value_count = TupletInteger(PS_APP_MSG_HEADER_COUNT, sendData.count_values);
    dict_write_tuplet(iter, &value_count);

    dict_write_end(iter);
    app_message_outbox_send();
    return;
}

static void send_timer_callback() {
    if (sendData.currentSendChunk == -1) {
        send_header_data();
        return;
    }
    int tpIndex = (sendData.currentSendChunk * sendData.sendChunkSize);
    if (tpIndex >= sendData.countTuplets) {
        // Finished with sync
        sync_in_progress = false;
        sync_start = false;
        free(sendData.motionData);
        hide_syncprogress_window();
        return;
    }

    int minIndex = tpIndex;
    int maxIndex = MIN(tpIndex + sendData.sendChunkSize, sendData.countTuplets - 1);

    DictionaryIterator *iter;
    AppMessageResult resBegin = app_message_outbox_begin(&iter);
    if (iter == NULL) {
        D("Itarator is NULL.");
    }

    if (resBegin == APP_MSG_OK) {
        D("App message ok.");
    } else if (resBegin == APP_MSG_INVALID_ARGS) {
        D("App message invalid args.");
    } else if (resBegin == APP_MSG_BUSY) {
        D("App message busy.");
    }

    for (int i = 0; i < sendData.sendChunkSize; i++, tpIndex++) {
        if (tpIndex < sendData.countTuplets) {
            Tuplet value = TupletInteger(tpIndex+3, sendData.motionData[tpIndex]);
            DictionaryResult dw = dict_write_tuplet(iter, &value);
            if (dw == DICT_OK) {
            } else if (dw == DICT_NOT_ENOUGH_STORAGE) {
                D("Dict not enught storage.");
            } else if (dw == DICT_INVALID_ARGS) {
                D("Dict invalid args.");
            }
        }
    }
    int finBytes = dict_write_end(iter);
    D("Finalizing msg with %d bytes", finBytes);

    AppMessageResult resSend = app_message_outbox_send();
    D("Result send: %d OK: %d", resSend, resSend == APP_MSG_OK);
}

static void send_last_stored_data() {
    // Generate tuplets
    sendData.countTuplets = count_motion_values();

    D("About to send %d records", sendData.countTuplets);

    int tpIndex = 0;

    // Now read the stats for start and finish
    StatData *lstat_data = read_last_stat_data();
    // Header
    sendData.start_time = lstat_data->start_time;
    sendData.end_time = lstat_data->end_time;
    sendData.count_values = sendData.countTuplets;
    free(lstat_data);
    sendData.motionData = read_motion_data();

    uint32_t size = dict_calc_buffer_size(sendData.countTuplets, sizeof(uint8_t));

    if (size <= message_outbox_size) {
        sendData.sendChunkSize = sendData.countTuplets;
    } else {
        sendData.sendChunkSize = (message_outbox_size / (size / sendData.countTuplets)) - 1; // -1 to be on the safe side
    }
    if (sendData.sendChunkSize > MAX_SEND_VALS || sendData.sendChunkSize <= 0) {
        sendData.sendChunkSize = MAX_SEND_VALS;
    }
    D("Determined chunk size %d for message outbox size %ld ", sendData.sendChunkSize, message_outbox_size);

    sendData.currentSendChunk = -1;
    timerSend = app_timer_register(SEND_STEP_MS, send_timer_callback, NULL);
}

static void sync_timer_callback() {
    if (sync_in_progress)
        return;
    if (sync_start) {
        sync_start = true;
        show_syncprogress_window();
        send_last_stored_data();
        return;
    }
}

void out_sent_handler(DictionaryIterator *sent, void *context) {
    D("out_sent_handler:");
    sendData.currentSendChunk += 1;
    timerSend = app_timer_register(SEND_STEP_MS, send_timer_callback, NULL);
}


void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
    D("out_failed_handler:");

    // Repeat lst chunk - do not increment the currentSendChunk
    timerSend = app_timer_register(SEND_STEP_MS, send_timer_callback, NULL);
}


void in_received_handler(DictionaryIterator *received, void *context) {
    D("in_received_handler:");

    if (sync_in_progress) return;
    if (sync_start) return;


    Tuple *command_tupple = dict_find(received, PS_APP_TO_WATCH_COMMAND);

    if (command_tupple) {
        if(command_tupple->value->uint8 == PS_APP_MESSAGE_COMMAND_START_SYNC) {
            sync_start = true;
            timerSync = app_timer_register(SYNC_STEP_MS, sync_timer_callback, NULL);
        } else if (command_tupple->value->uint8 == PS_APP_MESSAGE_COMMAND_SET_TIME) {

            show_syncprogress_window();

            Tuple *start_time_hour_tupple = dict_find(received, PS_APP_TO_WATCH_START_TIME_HOUR);
            Tuple *start_time_minute_tupple = dict_find(received, PS_APP_TO_WATCH_START_TIME_MINUTE);

            Tuple *end_time_hour_tupple = dict_find(received, PS_APP_TO_WATCH_END_TIME_HOUR);
            Tuple *end_time_minute_tupple = dict_find(received, PS_APP_TO_WATCH_END_TIME_MINUTE);

            D("save start: %d:%d end: %d%d", start_time_hour_tupple->value->uint8, start_time_minute_tupple->value->uint8, end_time_hour_tupple->value->uint8, end_time_minute_tupple->value->uint8);

            set_config_start_time(start_time_hour_tupple->value->uint8, start_time_minute_tupple->value->uint8);
            set_config_end_time(end_time_hour_tupple->value->uint8, end_time_minute_tupple->value->uint8);
            persist_write_config();

            hide_syncprogress_window();
        } else if (command_tupple->value->uint8 == PS_APP_MESSAGE_COMMAND_TOGGLE_SLEEP) {
            toggle_sleep();
        } else if (command_tupple->value->uint8 == PS_APP_MESSAGE_COMMAND_SET_SETTINGS) {

            show_syncprogress_window();

            Tuple *snoozeT = dict_find(received, PS_APP_TO_WATCH_COMMAND + 1);
            Tuple *fallAsT = dict_find(received, PS_APP_TO_WATCH_COMMAND + 2);
            Tuple *sensitivityT = dict_find(received, PS_APP_TO_WATCH_COMMAND + 3);
            Tuple *profileT = dict_find(received, PS_APP_TO_WATCH_COMMAND + 4);
            Tuple *vibrateOnSyncT = dict_find(received, PS_APP_TO_WATCH_COMMAND + 5);

            set_config_snooze(snoozeT->value->uint8);
            set_config_down_coef(fallAsT->value->uint8);
            set_config_up_coef(sensitivityT->value->uint8);
            set_config_active_profile(profileT->value->uint8);
            set_config_vibrate_on_change(vibrateOnSyncT->value->uint8);

            persist_write_config();

            hide_syncprogress_window();
        }
    }
}


void in_dropped_handler(AppMessageResult reason, void *context) {
    D("in_dropped_handler:");
}

void set_outbox_size(int outbox_size) {
    message_outbox_size = outbox_size;
}
