/*
 * Logic for LED Matrix Weather Display
 * Author: Michael Fung
*/

// Load Mongoose OS API
load('api_timer.js');
load('api_gpio.js');
load('api_sys.js');
load('api_mqtt.js');
load('api_config.js');
load('api_log.js');
load('api_math.js');
load('api_file.js');
load('api_rpc.js');
load('api_events.js');

// define variables
let MG_EV_MQTT_CONNACK = 202;
let MG_EV_CLOSE = 5;
let mqtt_connected = false;
let ACTION_PIN = 32;
let ALERT_LED_PIN = 17;
let SHUT_CMD = 1;
let RESUME_CMD = 0;
let clock_sync = false;
let tick_count = 0;
let forced_off = false;
let last_update = 0;
let is_stale = true;
let CELCIUS_SYMBOL = 0x4;  // degree celcius char code
let STALE_SYMBOL = 0x5; // degree celcius staled 
let current_temp = '---';
let current_humid = '---';
let temp_topic = 'weather/hko/tsuenwan/temp';
let humid_topic = 'weather/hko/hk/humid';
let thing_id = Cfg.get('mqtt.client_id');
let hab_state_topic = thing_id + '/state';
let hab_link_topic = thing_id + '/link';
let timer_on_begin = Cfg.get('timer.on_hour') * 60;  // in minutes
let timer_on_end = Cfg.get('timer.off_hour') * 60;
let reminder_msg = null;

// sntp sync event:
// ref: https://community.mongoose-os.com/t/add-sntp-synced-event/1208?u=michaelfung
let MGOS_EVENT_TIME_CHANGED = Event.SYS + 3;

// operation mode
let MODE = {
    TEST: 0,
    NORMAL: 1, // show temp
    HUMID: 2, // show himidity
    INHOUSE: 3, // show in-house weather data
    REMIND: 4,  // show reminder
};
let op_mode = MODE.NORMAL;  // default in normal mode

// touchpad events
let TpadEvent = {
    TOUCH9: Event.baseNumber('TPE'),  // 'TPE' match 'TPAD_EVT_BASE' in main.c
    UNTOUCH9: Event.baseNumber('TPE') + 1,
    LONG1_TOUCH9: Event.baseNumber('TPE') + 2,
    LONG2_TOUCH9: Event.baseNumber('TPE') + 3
};

// reminder schedules
// they must not be too close together to allow user time to acknowlege
let rem_sch = [
    { name: "med", enable: true, msg: "take evening dose", hour: 21, min: 30 }
];

// ffi functions
let show_char = ffi('void f_show_char(int, int)');
let rotate_char = ffi('void f_rotate()');
let print_string = ffi('void f_print_string(char *)');
let scroll_text = ffi('void f_scroll_text(char *)');
let stop_scroll_text = ffi('void f_stop_scroll_text()');
let clear_matrix = ffi('void f_clear_matrix()');
let shutdown_matrix = ffi('void f_shutdown_matrix(int)');
let set_brightness = ffi('void f_set_brightness(int)');
let str2int = ffi('int str2int(char *)');

// calc UTC offset
// NOTE: str2int('08') gives 0
let tz = Cfg.get('timer.tz');
let tz_offset = 0; // in seconds
let tz_sign = tz.slice(0, 1);
tz_offset = (str2int(tz.slice(1, 2)) * 10 * 3600) + (str2int(tz.slice(2, 3)) * 3600) + (str2int(tz.slice(3, 5)) * 60);
if (tz_sign === '-') {
    tz_offset = tz_offset * -1;
}
Log.print(Log.INFO, 'Local time UTC offset: ' + JSON.stringify(tz_offset) + ' seconds');

// notify server of switch state
let update_state = function () {
    let pubmsg = JSON.stringify({
        uptime: Sys.uptime(),
        memory: Sys.free_ram(),
        mode: op_mode,
        reminder: reminder_msg
    });
    let ok = MQTT.pub(hab_state_topic, pubmsg, 1, 1);
    Log.print(Log.INFO, 'Published:' + (ok ? 'OK' : 'FAIL') + ' topic:' + hab_state_topic + ' msg:' + pubmsg);    
};

// check schedule and fire if time reached
let run_sch = function () {
    let local_now = Math.floor(Timer.now()) + tz_offset;
    // calc current time of day from mg_time
    let min_of_day = Math.floor((local_now % 86400) / 60);
    Log.print(Log.DEBUG, "run_sch: Localized current time is " + JSON.stringify(min_of_day) + " minutes of day ");

    // on sch
    if (JSON.stringify(min_of_day) === JSON.stringify(timer_on_begin)) {
        Log.print(Log.INFO, '### run_sch: timer on reached, turn on matrix');
        forced_off = false;
        shutdown_matrix(RESUME_CMD);
        return;
    }

    // off sch
    if (JSON.stringify(min_of_day) === JSON.stringify(timer_on_end)) {
        Log.print(Log.INFO, '### run_sch: timer off reached, turn off matrix');
        if (op_mode === MODE.NORMAL) {
            forced_off = true;
            shutdown_matrix(SHUT_CMD);
        }
        else if (op_mode === MODE.REMIND) {
            Log.print(Log.INFO, '### run_sch: in REMIND mode, skip turn off');
        }
        else {
            // other modes have their own logic
        }
    }

    // reminder sch
    for (let i = 0; i < rem_sch.length; i++) {
        if (JSON.stringify(min_of_day) === JSON.stringify(rem_sch[i].hour * 60 + rem_sch[i].min)) {
            if (rem_sch[i].enable) {
                Log.print(Log.INFO, '### run_sch: fire reminder: ' + rem_sch[i].name);
                show_reminder(rem_sch[i].msg);
                break;  // no two reminder overlap

            } else {
                Log.print(Log.INFO, '### run_sch: reminder disabled: ' + rem_sch[i].name);
            }
        }
    }
};

let show_reminder = function (msg) {
    // *** Switch OPERATION MODE ***
    if (op_mode = MODE.REMIND) {
        stop_scroll_text();
    }
    op_mode = MODE.REMIND;
    clear_matrix();
    shutdown_matrix(RESUME_CMD);
    reminder_msg = msg;
    scroll_text(msg + '   ');
    GPIO.blink(ALERT_LED_PIN, 500, 500);  // blink: on 100ms, off 900ms
    update_state();
};

let ack_reminder = function () {
    GPIO.blink(ALERT_LED_PIN, 0, 0);  // disable blink
    GPIO.write(ALERT_LED_PIN, 1); // turn off
    stop_scroll_text();
    op_mode = MODE.NORMAL;
    reminder_msg = null;
    update_state();
};

let update_temp = function () {
    clear_matrix();

    if (current_temp === 'ERR') {
        Log.print(Log.INFO, "update_temp: eror reading temp");

        show_char(3, '#'.at(0));
        return;
    }

    if (is_stale) {
        Log.print(Log.INFO, "update_temp: stale temp:" + current_temp);
        show_char(3, STALE_SYMBOL);
    }
    else {
        Log.print(Log.INFO, "update_temp: current temp:" + current_temp);
        show_char(3, CELCIUS_SYMBOL);
    }

    show_char(2, current_temp.slice(2, 3).at(0));
    show_char(1, current_temp.slice(1, 2).at(0));
    show_char(0, current_temp.slice(0, 1).at(0));
    rotate_char();  // for the bold font issue
};

let update_humid = function () {
    clear_matrix();

    if (current_humid === 'ERR') {
        Log.print(Log.INFO, "update_humid: eror reading humid");

        show_char(3, '#'.at(0));
        return;
    }
    else {
        Log.print(Log.INFO, "update_humid: current humid:" + current_humid);
        show_char(3, '%'.at(0));
    }

    show_char(2, current_humid.slice(2, 3).at(0));
    show_char(1, current_humid.slice(1, 2).at(0));
    show_char(0, current_humid.slice(0, 1).at(0));
    rotate_char();  // for the bold font issue
};

let switch_to_humid_mode = function () {
    op_mode = MODE.HUMID;
    shutdown_matrix(RESUME_CMD);
    update_humid();
    update_state();
    // auto switch back to normal in 2 sec
    Timer.set(2000, 0 /* once */, function () {
        op_mode = MODE.NORMAL;
        update_temp();
        update_state();
    }, null);
};

let show_lost_conn = function () {
    clear_matrix();
    // use max72xx sys font and print string
    print_string(chr(27) + "-X-" + chr(26) + chr(0));
};

let toggle_onoff = function () {
    forced_off = !forced_off;
    if (forced_off) {
        shutdown_matrix(SHUT_CMD);
    } else {
        shutdown_matrix(RESUME_CMD);
    }
};

// MQTT
MQTT.sub(temp_topic, function (conn, topic, reading) {
    Log.print(Log.DEBUG, 'rcvd temperature reading:' + reading);

    // do a data copy instead of adding a reference to the data:
    let new_temp = '';
    for (let i = 0; i < 3; i++) {
        new_temp = new_temp + reading.slice(i, i + 1);
    }

    Log.print(Log.INFO, "mqttsub:temp is now:" + new_temp);
    last_update = Timer.now();
    is_stale = false;

    if (current_temp !== new_temp) {
        current_temp = '';
        for (let i = 0; i < 3; i++) {
            current_temp = current_temp + reading.slice(i, i + 1);
        }
        if (op_mode === MODE.NORMAL) {
            update_temp();
        }
    }

}, null);

MQTT.sub(humid_topic, function (conn, topic, reading) {
    Log.print(Log.DEBUG, 'rcvd humidity reading:' + reading);

    // do a data copy instead of adding a reference to the data:
    current_humid = '';
    for (let i = 0; i < 3; i++) {
        current_humid = current_humid + reading.slice(i, i + 1);
    }

    Log.print(Log.INFO, "mqttsub:humidity is now:" + current_humid);
    if (op_mode === MODE.HUMID) {
        update_humid();
    }
}, null);

MQTT.setEventHandler(function (conn, ev, edata) {
    if (ev === MQTT.EV_CONNACK) {
        mqtt_connected = true;
        Log.print(Log.INFO, 'MQTT connected');
        if (op_mode === MODE.NORMAL) {
            update_temp();
        }
        // publish to the online topic        
        let ok = MQTT.pub(hab_link_topic, 'ON', 1, 1); // qos=1, retain=1(true)
        Log.print(Log.INFO, 'pub_online_topic:' + (ok ? 'OK' : 'FAIL') + ', msg: ON');        
        update_state();
    }
    else if (ev === MQTT.EV_CLOSE) {
        mqtt_connected = false;
        Log.print(Log.ERROR, 'MQTT disconnected');
        if (op_mode === MODE.NORMAL) {
            show_lost_conn();
        }
    }

}, null);

/* --- RPC Handlers --- */
// SetReminder - instantly switch to REMIND mode and show a reminder message
RPC.addHandler('SetReminder', function (args) {
    if (typeof (args) === 'object' && typeof (args.reminder) === 'string') {
        show_reminder(args.reminder);
        return JSON.stringify({ result: 'OK' });
    } else {
        return { error: -1, message: 'Bad request. Expected: {"reminder":"some reminder message"}' };
    }
});

// set sntp sync flag
Event.addHandler(MGOS_EVENT_TIME_CHANGED, function (ev, evdata, ud) {
    if (Timer.now() > 1577836800 /* 2020-01-01 */) {
        clock_sync = true;
        Log.print(Log.INFO, 'mgos clock event: clock sync ok');
    } else {
        Log.print(Log.INFO, 'mgos clock event: clock not sync yet');
    }
}, null);

// touchpad events handlers:
Event.addHandler(TpadEvent.TOUCH9, function (ev, evdata, ud) {
    Log.print(Log.INFO, 'handling TOUCH9');
    if (op_mode === MODE.REMIND) {
        // treat as an acknowlegement of reminder
        ack_reminder();
        update_temp();
    }
    else if (forced_off) {
        // op_mode = MODE.NORMAL;        
        // update_temp();        
    }
    else if (op_mode === MODE.NORMAL) {
        switch_to_humid_mode();
    }
    else if (op_mode === MODE.HUMID) {
        op_mode = MODE.NORMAL;
        update_temp();
        update_state();
    }
    else if (op_mode === MODE.INHOUSE) {
        op_mode = MODE.NORMAL;
        update_temp();
        update_state();
    }
    else {
        //
    }

}, null);

Event.addHandler(TpadEvent.LONG1_TOUCH9, function (ev, evdata, ud) {
    Log.print(Log.INFO, 'handling LONG1_TOUCH9');
    toggle_onoff();
}, null);


// timer loop to update state and run schedule jobs
let tick_count = 0;
let main_loop_timer = Timer.set(60000 /* 1 min */, Timer.REPEAT, function () {
    if (mqtt_connected && !is_stale && (last_update < (Timer.now() - 1800))) {
        is_stale = true;
        if (op_mode === MODE.NORMAL) {
            update_temp();
        }
    }
    if (clock_sync) run_sch();
    tick_count++;
    if (tick_count % 5 === 0) { /* 5 min */
        tick_count = 0;
        update_state();
    }

}, null);

// setup alert LED
GPIO.set_mode(ALERT_LED_PIN, GPIO.MODE_OUTPUT);
GPIO.write(ALERT_LED_PIN, 1);  // switch led off

// indicate we are up, 2 hearts
show_char(0, 0x3); // heart
show_char(1, 0x3); // heart
rotate_char();
Log.print(Log.WARN, "### init script started ###");
