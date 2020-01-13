#include "mgos.h"
#include "mgos_config.h"
#include "mgos_app.h"
#include "mgos_gpio.h"
#include "mgos_sys_config.h"
#include "mgos_timers.h"
#include "mgos_hal.h"
#include "mgos_dlsym.h"
#include "mgos_mqtt.h"
#include "mjs.h"
#include <Arduino.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

// DATA structs:
#define ON_BOARD_LED 2
bool mqtt_conn_flag = false;

const uint8_t bold_numeric_font[] =
    {
        'F', 1, 48, 57, 8,

        7, 0x7c, 0xc6, 0xce, 0xde, 0xf6, 0xe6, 0x7c, // 0030 (zero)
        7, 0x30, 0x70, 0x30, 0x30, 0x30, 0x30, 0xfc, // 0031 (one)
        7, 0x78, 0xcc, 0x0c, 0x38, 0x60, 0xc4, 0xfc, // 0032 (two)
        7, 0x78, 0xcc, 0x0c, 0x38, 0x0c, 0xcc, 0x78, // 0033 (three)
        7, 0x1c, 0x3c, 0x6c, 0xcc, 0xfe, 0x0c, 0x1e, // 0034 (four)
        7, 0xfc, 0xc0, 0xf8, 0x0c, 0x0c, 0xcc, 0x78, // 0035 (five)
        7, 0x38, 0x60, 0xc0, 0xf8, 0xcc, 0xcc, 0x78, // 0036 (six)
        7, 0xfc, 0xcc, 0x0c, 0x18, 0x30, 0x30, 0x30, // 0037 (seven)
        7, 0x78, 0xcc, 0xcc, 0x78, 0xcc, 0xcc, 0x78, // 0038 (eight)
        7, 0x78, 0xcc, 0xcc, 0x7c, 0x0c, 0x18, 0x70, // 0039 (nine)
};

// for use with: void mgos_clear_timer(mgos_timer_id id);
static mgos_timer_id scroll_timer_id = 0;
static mgos_timer_id blink_display_timer_id = 0;

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
#define CS_PIN 5 // or SS

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
    uint8_t state = 0;
    uint8_t curLen = 0;
    uint16_t showLen = 0;
    uint8_t cBuf[8];
    int16_t col = ((modEnd + 1) * COL_SIZE) - 1;

    mx.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

    do // finite state machine to print the characters in the space available
    {
        switch (state)
        {
        case 0: // Load the next character from the font table
            // if we reached end of message, reset the message pointer
            if (*pMsg == '\0')
            {
                showLen = col - (modEnd * COL_SIZE); // padding characters
                state = 2;
                break;
            }

            // retrieve the next character form the font file
            showLen = mx.getChar(*pMsg++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
            curLen = 0;
            state++;
            // !! deliberately fall through to next state to start displaying

        case 1: // display the next part of the character
            mx.setColumn(col--, cBuf[curLen++]);

            // done with font character, now display the space between chars
            if (curLen == showLen)
            {
                showLen = CHAR_SPACING;
                state = 2;
            }
            break;

        case 2: // initialize state for displaying empty columns
            curLen = 0;
            state++;
            // fall through

        case 3: // display inter-character spacing or end of message padding (blank columns)
            mx.setColumn(col--, 0);
            curLen++;
            if (curLen == showLen)
                state = 0;
            break;

        default:
            col = -1; // this definitely ends the do loop
        }
    } while (col >= (modStart * COL_SIZE));

    mx.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
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
    static uint8_t state = 0;
    static uint8_t curLen = 0, showLen = 0;
    static uint8_t cBuf[8];
    uint8_t colData = 0;
    static bool lastChar = false;

    // finite state machine to control what we do on the callback
    switch (state)
    {
    case 0: // Load the next character from the font table
        showLen = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
        curLen = 0;
        state++;

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
        // !! deliberately fall through to next state to start displaying

    case 1: // display the next part of the character
        colData = cBuf[curLen++];
        if (curLen == showLen)
        {
            showLen = CHAR_SPACING;
            curLen = 0;
            state = 2;
        }
        break;

    case 2: // display inter-character spacing (blank column)
        colData = 0;
        curLen++;
        if (curLen == showLen)
        {
            state = 0;
            if (lastChar)
            {
                msgShown = true;
            }
        }
        break;

    default:
        state = 0;
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

static void scrollTextLeft(const char *msg)
{
    strcpy(curMessage, msg);
    scroll_timer_id = mgos_set_timer(scroll_delay /* ms */, MGOS_TIMER_REPEAT, scroll_left_cb, NULL);
}

// custom font, use  mx.setFont(nullptr); to reset to default font
static void setBoldNumericFont()
{
    mx.setFont(bold_numeric_font);
}

static void blink_display_all()
{
    blink_display_timer_id = mgos_set_timer(200 /* ms */, MGOS_TIMER_REPEAT, blink_display_cb, NULL);
}

static void testClearDisplay()
{
    mx.clear();
    mx.update();
}

// functions:
static void mqtt_ev_handler(struct mg_connection *c, int ev, void *p, void *user_data)
{
    struct mg_mqtt_message *msg = (struct mg_mqtt_message *)p;
    if (ev == MG_EV_MQTT_CONNACK)
    {
        LOG(LL_INFO, ("CONNACK: %d", msg->connack_ret_code));
        mqtt_conn_flag = true;
    }
    else if (ev == MG_EV_CLOSE)
    {
        mqtt_conn_flag = false;
    }
    (void)user_data;
    (void)c;
}

// ffi functions:
// put a character on a specific device, harcoded for show bold numeric font
extern "C" void f_show_char(int device_no, int c)
{
    int col = ((MAX_DEVICES - device_no) * 8) - 1;

    mx.setChar(col, c);
    mx.transform(MD_MAX72XX::TRC);
    mx.update(device_no);
}

extern "C" void f_clear_matrix()
{
    mx.clear();
    mx.update();
}

extern "C" void f_set_brightness(int brightness)
{
    mx.control(0, MAX_DEVICES - 1, MD_MAX72XX::INTENSITY, brightness);
}

extern "C" void f_shutdown_matrix(int cmd)
{
    mx.control(MD_MAX72XX::SHUTDOWN, MD_MAX72XX::ON); // scroll left
}

extern "C" int str2int(char *c)
{
    return (int)strtol(c, NULL, 10);
}

extern "C" int mqtt_connected(void)
{
    return (int)mqtt_conn_flag;
}

/* --- finally, main entry --- */
extern "C" enum mgos_app_init_result mgos_app_init(void)
{
    mx.begin();

    mx.control(0, MAX_DEVICES - 1,
               MD_MAX72XX::INTENSITY,
               mgos_sys_config_get_max72xx_brightness());

    scroll_delay = mgos_sys_config_get_max72xx_scroll_delay(); // read from cfg

    mx.setShiftDataInCallback(scrollDataSource);
    mx.setShiftDataOutCallback(scrollDataSink);

    // auto update buffer to device off:
    mx.control(0, MAX_DEVICES - 1, MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

    return MGOS_APP_INIT_SUCCESS;
}
