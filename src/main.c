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

#define LED_PIN 2

struct mgos_max7219 *d = NULL;

void clear_display() {
  for (int i=0; i<8; i++) {    
    mgos_max7219_write_raw(d, 0, i, 0x0);
    mgos_max7219_write_raw(d, 1, i, 0x0);
    mgos_max7219_write_raw(d, 2, i, 0x0);
    mgos_max7219_write_raw(d, 3, i, 0x0);        
  }
}

void show_char(uint8_t device_no, uint8_t c) {
  for (int row = 0; row < 8; row++) {
    mgos_max7219_write_raw(d, device_no, row, font8x8_ic8x8u[c][row]);
  }
}

void timer_cb(void *arg) {
  static bool s_tick_tock = false;
  static uint8_t charcode = 0;
  
  s_tick_tock = !s_tick_tock;
  mgos_gpio_toggle(LED_PIN);

  ++charcode;
  show_char(0, charcode);
  //show_char2(d, 2, charcode);
  (void) arg;
}

enum mgos_app_init_result mgos_app_init(void) {
  
  // setup HW
  mgos_gpio_setup_output(LED_PIN, 0);

  if (!(d = mgos_max7219_create(mgos_spi_get_global(), mgos_sys_config_get_max7219_cs_index(),mgos_sys_config_get_max7219_num_devices() ))) {
    LOG(LL_ERROR, ("Could not create MAX7219 display"));
    return MGOS_APP_INIT_ERROR;
  }  
  mgos_max7219_set_intensity(d, 1);
  clear_display();

  // setup timers
  mgos_set_timer(500, true, timer_cb, NULL);

  return MGOS_APP_INIT_SUCCESS;
}
