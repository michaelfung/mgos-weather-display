# Simple weather display

A simple weather display using LED matrix for the elderly. Powered by Mongoose OS.

## Hardware

- 8x32 LED Matrix
- ESP32
- Materials for touchpad

## Service

A MQTT server which provides local weather.

### Example for Hong Kong

Included a perl script to update the district weather at the MQTT server.

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
