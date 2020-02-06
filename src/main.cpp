#include "mgos.h"
#include "mgos_config.h"
#include "mgos_app.h"
#include "mgos_gpio.h"
#include "mgos_sys_config.h"
#include "mgos_timers.h"
#include "mgos_hal.h"
#include "mgos_dlsym.h"
#include "mgos_mqtt.h"
#include "mgos_system.h"
#include "mgos_time.h"
#include "mgos_event.h"
#include "mjs.h"
#include "driver/touch_pad.h"
#include <Arduino.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include "bold_numeric_font.h"

/* --- DATA structs --- */
#define ON_BOARD_LED 2
bool mqtt_conn_flag = false;
// configurables
uint16_t touch_poll_freq;
uint16_t long1_duration;
uint16_t long2_duration;
uint16_t touch_threshold;
// touch event
//static int64_t last_touch_event = 0;  // us

static uint8_t scroll_state = 0, scroll_curLen = 0, scroll_showLen = 0;
static uint8_t print_state = 0, print_curLen = 0;
static uint16_t print_showLen = 0;

// for use with: void mgos_clear_timer(mgos_timer_id id);
static mgos_timer_id scroll_timer_id = 0;
static mgos_timer_id blink_display_timer_id = 0;

// register our touchpad event
#define TPAD_EVT_BASE MGOS_EVENT_BASE('T', 'P', 'E')
enum tpad_event
{
    TOUCH9 = TPAD_EVT_BASE,
    UNTOUCH9,
    LONG1_TOUCH9,
    LONG2_TOUCH9
};

static void poll_touchpad_cb(void *arg)
{
    static uint16_t touch_value;
    static uint16_t touch_duration = 0; // in ms
    static bool touched = false;
    static bool touch_emitted = false;
    static bool long1_touch_emitted = false;
    static bool long2_touch_emitted = false;
    // int64_t now = mgos_uptime_micros();

    touch_pad_read(TOUCH_PAD_NUM9, &touch_value);
    LOG(LL_DEBUG, ("[%4d]", touch_value));

    if (touch_value < touch_threshold)
    {
        LOG(LL_INFO, ("touched"));
        touched = true;
        touch_duration += touch_poll_freq;

        if (touch_duration >= long2_duration)
        {
            // emit long2 touch
            if (!long2_touch_emitted)
            {
                long2_touch_emitted = true;
                LOG(LL_INFO, ("emit long2 touch event"));
                mgos_event_trigger(LONG2_TOUCH9, NULL);
                //        last_touch_event = now;
            }
        }
        else if (touch_duration >= long1_duration)
        {
            // emit long1 touch
            if (!long1_touch_emitted)
            {
                long1_touch_emitted = true;
                LOG(LL_INFO, ("emit long1 touch event"));
                mgos_event_trigger(LONG1_TOUCH9, NULL);
                //        last_touch_event = now;
            }
        }
        else
        {
            // emit touch event
            if (!touch_emitted)
            {
                touch_emitted = true;
                LOG(LL_INFO, ("emit touch event"));
                mgos_event_trigger(TOUCH9, NULL);
                //        last_touch_event = now;
            }
        }
    }
    else
    {
        LOG(LL_DEBUG, ("!t"));

        if (touched) // if untouch
        {
            LOG(LL_INFO, ("released"));
            mgos_event_trigger(UNTOUCH9, NULL);
            touched = false;
            touch_duration = 0;
            touch_emitted = false;
            long1_touch_emitted = false;
            long2_touch_emitted = false;
        }
    }
    (void)arg;
}

#define PRINT_CALLBACK 0

#define PRINT(s, v)         \
    {                       \
        Serial.print(F(s)); \
        Serial.print(v);    \
    }

// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers will probably not work with your hardware and may
// need to be adapted
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CLK_PIN 18  // or SCK
#define DATA_PIN 23 // or MOSI
#define CS_PIN 5    // or SS

// SPI hardware interface
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// Global message buffers shared by Serial and Scrolling functions
#define BUF_SIZE 75
char curMessage[BUF_SIZE];
char newMessage[BUF_SIZE];
bool newMessageAvailable = false;
bool msgShown = false;

static uint16_t scroll_delay = 100; // in milliseconds

// Text parameters
#define CHAR_SPACING 1 // pixels between characters

void printText(uint8_t modStart, uint8_t modEnd, char *pMsg)
// Print the text string to the LED matrix modules specified.
// Message area is padded with blank columns after printing.
{
    uint8_t cBuf[8];
    int16_t col = ((modEnd + 1) * COL_SIZE) - 1;

    //mx.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

    do // finite print_state machine to print the characters in the space available
    {
        switch (print_state)
        {
        case 0: // Load the next character from the font table
            // if we reached end of message, reset the message pointer
            if (*pMsg == '\0')
            {
                print_showLen = col - (modEnd * COL_SIZE); // padding characters
                print_state = 2;
                break;
            }

            // retrieve the next character from the font file
            print_showLen = mx.getChar(*pMsg++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
            print_curLen = 0;
            print_state++;
            // !! deliberately fall through to next print_state to start displaying

        case 1: // display the next part of the character
            mx.setColumn(col--, cBuf[print_curLen++]);

            // done with font character, now display the space between chars
            if (print_curLen == print_showLen)
            {
                print_showLen = CHAR_SPACING;
                print_state = 2;
            }
            break;

        case 2: // initialize print_state for displaying empty columns
            print_curLen = 0;
            print_state++;
            // fall through

        case 3: // display inter-character spacing or end of message padding (blank columns)
            mx.setColumn(col--, 0);
            print_curLen++;
            if (print_curLen == print_showLen)
                print_state = 0;
            break;

        default:
            col = -1; // this definitely ends the do loop
        }
    } while (col >= (modStart * COL_SIZE));

    // mx.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

void scrollDataSink(uint8_t dev, MD_MAX72XX::transformType_t t, uint8_t col)
// Callback function for data that is being scrolled off the display
{
#if PRINT_CALLBACK
    Serial.print("\n cb ");
    Serial.print(dev);
    Serial.print(' ');
    Serial.print(t);
    Serial.print(' ');
    Serial.println(col);
#endif
}

uint8_t scrollDataSource(uint8_t dev, MD_MAX72XX::transformType_t t)
// Callback function for data that is required for scrolling into the display
{
    static char *p = curMessage;
    static uint8_t cBuf[8];
    uint8_t colData = 0;
    static bool lastChar = false;

    // finite scroll_state machine to control what we do on the callback
    switch (scroll_state)
    {
    case 0: // Load the next character from the font table
        scroll_showLen = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
        scroll_curLen = 0;
        scroll_state++;

        // if we reached end of message, reset the message pointer
        if (*p == '\0')
        {
            lastChar = true;
            p = curMessage;          // reset the pointer to start of message
            if (newMessageAvailable) // there is a new message waiting
            {
                strcpy(curMessage, newMessage); // copy it in
                newMessageAvailable = false;
            }
        }
        else
        {
            lastChar = false;
        }
        // !! deliberately fall through to next scroll_state to start displaying

    case 1: // display the next part of the character
        colData = cBuf[scroll_curLen++];
        if (scroll_curLen == scroll_showLen)
        {
            scroll_showLen = CHAR_SPACING;
            scroll_curLen = 0;
            scroll_state = 2;
        }
        break;

    case 2: // display inter-character spacing (blank column)
        colData = 0;
        scroll_curLen++;
        if (scroll_curLen == scroll_showLen)
        {
            scroll_state = 0;
            if (lastChar)
            {
                msgShown = true;
            }
        }
        break;

    default:
        scroll_state = 0;
    }

    return (colData);
}

static void scroll_left_cb(void *arg)
{
    mx.transform(MD_MAX72XX::TSL); // scroll left
    (void)arg;
}

static void blink_display_cb(void *arg)
{
    static uint8_t shut = 0;
    shut = 1 - shut;
    mx.control(MD_MAX72XX::SHUTDOWN, shut); // scroll left
    (void)arg;
}

static void scrollTextLeft()
{
    //strcpy(curMessage, msg);
    scroll_timer_id = mgos_set_timer(scroll_delay /* ms */, MGOS_TIMER_REPEAT, scroll_left_cb, NULL);
}

// custom font, use  mx.setFont(nullptr); to reset to default font
static void setBoldNumericFont()
{
    LOG(LL_DEBUG, ("setBoldNumericFont"));
    mx.setFont(bold_numeric_font);
}

/* --- ffi functions --- */

// put a character on a specific device buffer
extern "C" void f_show_char(int device_no, int c)
{
    int col = ((MAX_DEVICES - device_no) * 8) - 1;
    // setBoldNumericFont();
    mx.setChar(col, c);
    LOG(LL_DEBUG, ("show char code %d at col %d", c, col));
    //mx.transform(MD_MAX72XX::TRC);
    //mx.transform(device_no, MD_MAX72XX::TRC);
    //mx.update();
}

// rotate all device buffer and display
extern "C" void f_rotate()
{
    mx.transform(MD_MAX72XX::TRC);
    mx.update();
}

// print a string with max length of BUF_SIZE
extern "C" void f_print_string(char *msg)
{
    int len = strlen(msg);
    if (len > BUF_SIZE)
        len = BUF_SIZE - 1;
    strncpy(curMessage, msg, len);
    mx.setFont(nullptr);
    mx.clear();
    print_state = 0;
    print_curLen = 0;
    print_showLen = 0;

    printText(0, MAX_DEVICES - 1, curMessage);
    mx.update();
    setBoldNumericFont();
}

extern "C" void f_scroll_text(char *msg)
{
    int len = strlen(msg);
    if (len > BUF_SIZE)
        len = BUF_SIZE - 1;
    strncpy(curMessage, msg, len);
    mx.setFont(nullptr); // back to sys font
    mx.clear();
    mx.control(0, MAX_DEVICES - 1, MD_MAX72XX::UPDATE, MD_MAX72XX::ON);

    scroll_state = 0;
    scroll_curLen = 0;
    scroll_showLen = 0;
    scrollTextLeft();
}

extern "C" void f_stop_scroll_text()
{
    mgos_clear_timer(scroll_timer_id);
    mx.control(0, MAX_DEVICES - 1, MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
    setBoldNumericFont();
}

// blink display with <interval> ms, set <interval> to stop blink
extern "C" void f_blink_display_all(int interval)
{
    if (interval > 0)
    {
        if (interval < 100)
            interval = 100;
        blink_display_timer_id = mgos_set_timer(interval /* ms */, MGOS_TIMER_REPEAT, blink_display_cb, NULL);
    }
    else
    {
        mgos_clear_timer(blink_display_timer_id);
        mx.control(MD_MAX72XX::SHUTDOWN, MD_MAX72XX::OFF);
    }
}

extern "C" void f_clear_matrix()
{
    LOG(LL_DEBUG, ("clear matrix"));
    mx.clear();
    mx.update();
}

extern "C" void f_set_brightness(int brightness)
{
    mx.control(0, MAX_DEVICES - 1, MD_MAX72XX::INTENSITY, brightness);
}

// shutdown or resume display if <cmd> set to 1
extern "C" void f_shutdown_matrix(int cmd)
{
    if (cmd > 0)
    {
        mx.control(MD_MAX72XX::SHUTDOWN, MD_MAX72XX::ON); // shut it
    }
    else
    {
        mx.control(MD_MAX72XX::SHUTDOWN, MD_MAX72XX::OFF); // resume
    }
}

extern "C" int str2int(char *c)
{
    return (int)strtol(c, NULL, 10);
}

/* --- finally, main entry --- */
extern "C" enum mgos_app_init_result mgos_app_init(void)
{

    // load configurables:
    touch_poll_freq = mgos_sys_config_get_touch_poll_freq();
    long1_duration = mgos_sys_config_get_touch_long1();
    long2_duration = mgos_sys_config_get_touch_long2();
    touch_threshold = mgos_sys_config_get_touch_threshold();

    mx.begin();

    LOG(LL_INFO, ("max72xx device count:%d, col count: %d", mx.getDeviceCount(), mx.getColumnCount()));

    mx.control(0, MAX_DEVICES - 1,
               MD_MAX72XX::INTENSITY,
               mgos_sys_config_get_max72xx_brightness());

    scroll_delay = mgos_sys_config_get_max72xx_scroll_delay(); // read from cfg

    mx.setShiftDataInCallback(scrollDataSource);
    mx.setShiftDataOutCallback(scrollDataSink);

    // set auto update buffer to device to OFF:
    mx.control(0, MAX_DEVICES - 1, MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
    // default to bold numeric font as it is used most
    setBoldNumericFont();

    // display boot up logo, one heart
    f_show_char(0, 0x03); // heart
    f_rotate();

    // Initialize touch pad peripheral.
    // The default fsm mode is software trigger mode.
    touch_pad_init();
    // Set reference voltage for charging/discharging
    // In this case, the high reference valtage will be 2.7V - 1V = 1.7V
    // The low reference voltage will be 0.5
    // The larger the range, the larger the pulse count value.
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
    touch_pad_config(TOUCH_PAD_NUM9, 0);

    mgos_event_register_base(TPAD_EVT_BASE, "touchpad module");
    mgos_set_timer(touch_poll_freq, MGOS_TIMER_REPEAT, poll_touchpad_cb, NULL);

    return MGOS_APP_INIT_SUCCESS;
}
