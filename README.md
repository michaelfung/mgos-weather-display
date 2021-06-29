# Simple weather display

A simple weather display using LED matrix for the elderly. Powered by Mongoose OS.

## Features

- display temperature by default
- display humidity when action touchpad is touched
- display can be turned off by *long touch*
- display can be turned on/off by a preset schedule
- display reminder message at preset time daily, e.g. take medical dose
- display reminder message for Google Calendar Events with the help of an external script

## Hardware

- 8x32 LED Matrix
- ESP32
- Materials for touchpad

## Weather Service

A MQTT server which provides local weather.

### Example for Hong Kong

Included a perl script to update the district weather at the MQTT server.

## Setup reminder for Google Calendar Events

Steps:

Use GCP to create a service account and download the JSON file.

Go to calendar settings and share the calendar with the email address of the service account.

Run the **get-calendar-events.pl** perl script periodically.

## Set display on/off schedule

Example, set on at 6AM and off at 10PM:

    mos --port "http://wmatrix.lan/rpc" config-set timer.off_hour=22 timer.on_hour=6

Disable the schedule by setting on/off hour to negative number:

    mos --port "http://wmatrix.lan/rpc" config-set timer.off_hour=-1 timer.on_hour=-1

## Test

Test display temperature update:

    mosquitto_pub -d -h hab2.lan -t "weather/hko/tsuenwan/temp" -m " 19"

Test RPC handlers:

    mos --port "http://wmatrix.lan/rpc" call SetReminder '{"reminder":"test rpc call  "}'

## Build Notes

To compile with custom library, add it to `mos.yml` like:

```
  - origin: https://github.com/michaelfung/max7219-spi
    name: max7219-spi
    version: master
```

Then, build with option **--lib** :

    --lib max7219-spi:/somewhere/max7219-spi
