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

#include "mgos_max7219.h"
#include "fontdata.h"


// DATA structs:
#define ON_BOARD_LED 2
#define NUM_DEVICES 4
bool mqtt_conn_flag = false;
static uint8_t led_timer_ticks = 0;  /* for led blinker use */
const uint8_t nullchars[NUM_DEVICES] = { 0x00, 0x00, 0x00, 0x00 };
uint8_t digits[NUM_DEVICES]; // holds the current shown digits
struct mgos_max7219 *matrix = NULL;

// functions:
void clear_display() {

  // for (int i=0; i<4; i++){
  //   digits[i] = 0x00;
  // }
  memcpy(digits, nullchars, NUM_DEVICES);

  for (int dev_no=0; dev_no<NUM_DEVICES; dev_no++) {
    for (int row=0; row<8; row++) {        
      mgos_max7219_write_raw(matrix, dev_no, row, 0x0);
    }  
  }
}

void show_char(uint8_t device_no, uint8_t c) {
  digits[device_no] = c;
  for (int row = 0; row < 8; row++) {
    mgos_max7219_write_raw(matrix, device_no, row, font8x8_ic8x8u[c][row]);
  }
}

static void mqtt_ev_handler(struct mg_connection *c, int ev, void *p, void *user_data) {
  struct mg_mqtt_message *msg = (struct mg_mqtt_message *) p;
  if (ev == MG_EV_MQTT_CONNACK) {
    LOG(LL_INFO, ("CONNACK: %d", msg->connack_ret_code));
    mqtt_conn_flag = true;

  } else if (ev == MG_EV_CLOSE) {
      mqtt_conn_flag = false;
  }
  (void) user_data;
  (void) c;
}

// ffi functions:
void f_show_char(int device_no, int c) {
  show_char((uint8_t) device_no, (uint8_t) c);
}

void f_clear_matrix() {
  clear_display();
}

void f_set_brightness(int brightness) {
  mgos_max7219_set_intensity(matrix, brightness);
}

int str2int(char *c) {
  return (int) strtol(c,NULL,10);
}

int mqtt_connected(void) {
	return (int) mqtt_conn_flag;
}


void list_fonts() {
  static uint8_t charcode = 0;
  
  ++charcode;
  show_char(0, charcode);
  //show_char2(matrix, 2, charcode);  
}

static void blink_on_board_led_cb(void *arg) {
  static uint8_t remainder;

    if (mqtt_conn_flag) {
        remainder = (++led_timer_ticks % 10);  // every 10*200ms = 2 secs
        if (remainder == 0) {
            led_timer_ticks = 0;
              mgos_gpio_write(ON_BOARD_LED, 0);  // on
        } else if (remainder == 1) {
            mgos_gpio_write(ON_BOARD_LED, 1);  // off
        }
    } else {
        mgos_gpio_toggle(ON_BOARD_LED);
    }

  (void) arg;
}

enum mgos_app_init_result mgos_app_init(void) {
  
  // setup HW
  mgos_gpio_setup_output(ON_BOARD_LED, 0);

  if (!(matrix = mgos_max7219_create(mgos_spi_get_global(), mgos_sys_config_get_max7219_cs_index(),mgos_sys_config_get_max7219_num_devices() ))) {
    LOG(LL_ERROR, ("Could not create MAX7219 display"));
    return MGOS_APP_INIT_ERROR;
  }  
  mgos_max7219_set_intensity(matrix, mgos_sys_config_get_max7219_brightness());
  clear_display();

  mgos_set_timer(200 /* ms */, true /* repeat */, blink_on_board_led_cb, NULL);
  mgos_mqtt_add_global_handler(mqtt_ev_handler, NULL);

  return MGOS_APP_INIT_SUCCESS;
}
