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

/*
 * get event values, lookup mongoose.h:
 *
 * #define MG_MQTT_CMD_CONNACK 2
 * #define MG_MQTT_EVENT_BASE 200
 *
 * #define MG_EV_CLOSE 5
 *
 * #define MG_EV_MQTT_CONNACK (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_CONNACK)
 *
*/

// define variables
let MG_EV_MQTT_CONNACK = 202;
let MG_EV_CLOSE = 5;
let mqtt_connected = false;
let ACTION_PIN = 32;
let clock_sync = false;
let tick_count = 0;
let forced_off = false;
let CELCIUS_SYMBOL = 128;  // degree celcius char code
let current_temp = null;
let current_humid = null;
let temp_topic = 'weather/hko/tsuenwan/temp';
let timer_on_begin = Cfg.get('timer.on_hour');
let timer_on_end = Cfg.get('timer.off_hour');

// ffi functions
let show_char = ffi('void f_show_char(int, int)');
let clear_matrix = ffi('void f_clear_matrix()');
let set_brightness = ffi('void f_set_brightness(int)');
let str2int = ffi('int str2int(char *)');

// calc UTC offset
// NOTE: str2int('08') gives 0
let tz = Cfg.get('timer.tz');
let tz_offset = 0; // in seconds
let tz_sign = tz.slice(0,1);
tz_offset = (str2int(tz.slice(1,2)) * 10 * 3600) + (str2int(tz.slice(2,3)) * 3600) + (str2int(tz.slice(3,5)) * 60);
if (tz_sign === '-') {
    tz_offset = tz_offset * -1;
}
Log.print(Log.INFO, 'Local time UTC offset: ' + JSON.stringify(tz_offset) + ' seconds');


// check schedule and fire if time reached
let run_sch = function () {
    let local_now = Math.floor(Timer.now()) + tz_offset;
    // calc current time of day from mg_time
    let min_of_day = Math.floor((local_now % 86400) / 60);
    Log.print(Log.INFO, "run_sch: Localized current time is " + JSON.stringify(min_of_day) + " minutes of day ");

    if (JSON.stringify(min_of_day) === JSON.stringify(timer_on_begin)) {
        Log.print(Log.INFO, '### run_sch: timer on reached, turn on matrix');
        forced_off = false;
        update_display();
        return;
    }

    if (JSON.stringify(min_of_day) === JSON.stringify(timer_on_end)) {
        Log.print(Log.INFO, '### run_sch: timer off reached, turn off matrix');
        forced_off = true;
        clear_matrix();
        return;
    }
};

let update_display = function () {
    if (forced_off) {
        Log.print(Log.INFO, 'update_display: forced off, skip');
        return;
    }
    let temp_str = '??';

    if (current_temp !== null && current_temp < 100 && current_temp > -100) {        
        temp_str = JSON.stringify(current_temp);
        Log.print(Log.INFO, 'update_display: temp_str set to: ' + temp_str);
    }
    show_char(0, CELCIUS_SYMBOL);
    show_char(1, temp_str.at(-1));
    show_char(2, temp_str.at(-2));
    show_char(3, temp_str.at(-3));
};

let toggle_onoff = function () {
    forced_off = !forced_off;
    if (forced_off) { 
        clear_matrix();
    } else {
        update_display();
    }
};

// MQTT
GPIO.set_button_handler(ACTION_PIN, GPIO.PULL_UP, GPIO.INT_EDGE_NEG, 500, function (x) {
    Log.print(Log.INFO, 'action button pressed');
    toggle_onoff();
}, true);

MQTT.sub(temp_topic, function (conn, topic, reading) {
    Log.print(Log.INFO, 'rcvd temperature reading:' + reading);
    current_temp = Math.floor(reading);  // decimal is not good for elderly
    update_display();
}, null);

// check sntp sync, to be replaced by sntp event handler after implemented by OS
let clock_check_timer = Timer.set(30000, true /* repeat */, function () {
    if (Timer.now() > 1575763200 /* 2018-12-08 */) {
        clock_sync = true;
        Timer.del(clock_check_timer);
        Log.print(Log.INFO, 'clock_check_timer: clock sync ok');
    } else {
        Log.print(Log.INFO, 'clock_check_timer: clock not sync yet');
    }
}, null);


// timer loop to update state and run schedule jobs
let main_loop_timer = Timer.set(1000 /* 1 sec */, true /* repeat */, function () {
    tick_count++;
    if ((tick_count % 60) === 0) { /* 1 min */
        if (clock_sync) run_sch();

        // reset tick count
        tick_count = 0;
    }
}, null);


show_char(3, 'R'.at(0));
show_char(2, 'D'.at(0));
show_char(1, 'Y'.at(0));
show_char(0, '!'.at(0));

Log.print(Log.WARN, "### init script started ###");
