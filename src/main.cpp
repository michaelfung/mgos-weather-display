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
#include "bold_numeric_font.h"

// DATA structs:
#define ON_BOARD_LED 2
bool mqtt_conn_flag = false;

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
    LOG(LL_DEBUG, ("setBoldNumericFont"));
    mx.setFont(bold_numeric_font);
}

// ffi functions:
// put a character on a specific device buffer
extern "C" void f_show_char(int device_no, int c)
{
    int col = ((MAX_DEVICES - device_no) * 8) - 1;
    setBoldNumericFont();
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

extern "C" void f_scroll_text(char *msg)
{
    mx.setFont(nullptr); // back to sys font
    scrollTextLeft(msg);
}

extern "C" void f_stop_scroll_text(char *msg)
{
    mgos_clear_timer(scroll_timer_id);
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
    mx.begin();

    LOG(LL_INFO,("max72xx device count:%d, col count: %d", mx.getDeviceCount(), mx.getColumnCount()));

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

    return MGOS_APP_INIT_SUCCESS;
}
