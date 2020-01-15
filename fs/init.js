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
load('api_esp32_touchpad.js');

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
let timer_on_begin = Cfg.get('timer.on_hour') * 60;  // in minutes
let timer_on_end = Cfg.get('timer.off_hour') * 60;

// ffi functions
let show_char = ffi('void f_show_char(int, int)');
let rotate_char = ffi('void f_rotate()');
let print_string = ffi('void f_print_string(char *)');
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

// check schedule and fire if time reached
let run_sch = function () {
    let local_now = Math.floor(Timer.now()) + tz_offset;
    // calc current time of day from mg_time
    let min_of_day = Math.floor((local_now % 86400) / 60);
    Log.print(Log.DEBUG, "run_sch: Localized current time is " + JSON.stringify(min_of_day) + " minutes of day ");

    if (JSON.stringify(min_of_day) === JSON.stringify(timer_on_begin)) {
        Log.print(Log.INFO, '### run_sch: timer on reached, turn on matrix');
        forced_off = false;
        shutdown_matrix(RESUME_CMD);
        // update_display();
        return;
    }

    if (JSON.stringify(min_of_day) === JSON.stringify(timer_on_end)) {
        Log.print(Log.INFO, '### run_sch: timer off reached, turn off matrix');
        forced_off = true;
        shutdown_matrix(SHUT_CMD);
        //clear_matrix();
        return;
    }
};

let update_display = function () {
    // if (forced_off) {
    //     Log.print(Log.INFO, 'update_display: forced off, skip');
    //     return;
    // }

    clear_matrix();

    if (current_temp === 'ERR') {
        Log.print(Log.INFO, "update_display: eror reading temp");
        
        show_char(3, '#'.at(0));
        return;
    }

    if (is_stale) {
        Log.print(Log.INFO, "update_display: stale temp:" + current_temp);
        show_char(3, STALE_SYMBOL);
    }
    else {
        Log.print(Log.INFO, "update_display: current temp:" + current_temp);
        show_char(3, CELCIUS_SYMBOL);
    }

    show_char(2, current_temp.slice(2, 3).at(0));
    show_char(1, current_temp.slice(1, 2).at(0));
    show_char(0, current_temp.slice(0, 1).at(0));
    rotate_char();  // for the bold font issue
};

let show_lost_conn = function () {
    clear_matrix();
    // use max72xx sys font and print string
    print_string(chr(27) + "-X-" + chr(26));
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
    current_temp = '';
    for (let i = 0; i < 3; i++) {
        current_temp = current_temp + reading.slice(i, i + 1);
    }

    Log.print(Log.INFO, "mqttsub:temp is now:" + current_temp);
    last_update = Sys.uptime();
    is_stale = false;
    update_display();
}, null);

MQTT.setEventHandler(function (conn, ev, edata) {
    if (ev === MQTT.EV_CONNACK) {
        mqtt_connected = true;
        Log.print(Log.INFO, 'MQTT connected');
        update_display();
    }
    else if (ev === MQTT.EV_CLOSE) {
        mqtt_connected = false;
        Log.print(Log.ERROR, 'MQTT disconnected');
        show_lost_conn();
    }

}, null);

// check sntp sync, to be replaced by sntp event handler after implemented by OS
let clock_check_timer = Timer.set(10000, true /* repeat */, function () {
    if (Timer.now() > 1575763200 /* 2018-12-08 */) {
        clock_sync = true;
        Timer.del(clock_check_timer);
        Log.print(Log.INFO, 'clock_check_timer: clock sync ok');
    } else {
        Log.print(Log.INFO, 'clock_check_timer: clock not sync yet');
    }
}, null);

// use touch sensor to toggle display
let ts = TouchPad.GPIO[ACTION_PIN];
TouchPad.init();
TouchPad.setVoltage(TouchPad.HVOLT_2V5, TouchPad.LVOLT_0V8, TouchPad.HVOLT_ATTEN_1V5);
TouchPad.config(ts, 0);

// timer loop to update state and run schedule jobs
let main_loop_timer = Timer.set(1000 /* 1 sec */, Timer.REPEAT, function () {
    tick_count++;

    let tv = TouchPad.read(ts);
    if (tv < 3000) {
        Log.print(Log.INFO, 'touchpad pressed, value: ' + JSON.stringify(tv));
        toggle_onoff();
    }

    // every minute
    if (tick_count % 60 === 0) {
        tick_count = 0;
        if (mqtt_connected && !is_stale && (last_update < (Sys.uptime() - 1800))) {
            is_stale = true;
            update_display();
        }
        if (clock_sync) run_sch();
    }

}, null);

// setup alert LED
GPIO.set_mode(ALERT_LED_PIN, GPIO.MODE_OUTPUT);
GPIO.write(ALERT_LED_PIN, 1);
// GPIO.blink(ALERT_LED_PIN, 100, 900);  // blink: on 100ms, off 900ms


// indicate we are up, 2 hearts
show_char(0, 0x3); // heart
show_char(1, 0x3); // heart
rotate_char(); 
Log.print(Log.WARN, "### init script started ###");
