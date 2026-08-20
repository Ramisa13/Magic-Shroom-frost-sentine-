# Frost Sentinel — "Magic Shroom" 🍄

An ESP32-based frost-warning device for the Garden Spine IoT garden network (ITEE Summer Programme), disguised as a mushroom mascot. A thermistor/DHT11 sensor watches for frost conditions; when the temperature crosses a threshold, a single servo-driven hand rises as a visible warning flag, an OLED face shows an alarmed expression, and an active buzzer sounds — all reported live over MQTT.

**Device ID:** `fs-01` · **Zone:** `outdoor-1` · **Type:** `frost-node`

<!-- Add a photo or short GIF of the finished mushroom here — this is the first thing anyone sees. -->

## Features

- **Frost hysteresis logic** — separate enter/leave temperature thresholds so the warning state doesn't flicker right at the boundary
- **One servo, one gesture** — a single hand rises on warning and returns to rest on recovery, moved in small steps rather than an instant jump
- **On-device threshold menu** — a joystick lets you view and adjust the upper/lower temperature limits live, with a long-press to exit back to the main display
- **OLED status face** — animated eyes that blink, with an alarmed expression and warning icon during a frost event
- **MQTT reporting** — publishes temperature, humidity, and status to the Garden Spine network over TLS, and can subscribe to a neighboring node's readings
- **Active buzzer alert** — audible warning alongside the visual gesture
- **Distance-triggered welcome wave** — an HC-SR04 ultrasonic sensor watches the distance to whatever is in front of it; a change of more than 10 cm from its last reading triggers three servo waves between 45° and 90°, giving the mushroom a welcoming gesture. Invalid (no-echo) readings are ignored rather than treated as a change. The feature can be enabled or disabled from the on-device menu.

## Hardware

| Component | Notes |
|---|---|
| ESP32 DevKit | Elegoo Super Starter Kit |
| DHT11 | Temperature/humidity sensor |
| Thermistor + 10kΩ resistor | Charter-specified sensor (see Known Limitations) |
| SG90 servo | Drives the single warning gesture |
| Active buzzer | Drive-on-power, no pitch control |
| SSD1306 OLED (128×64, I2C) | Status display |
| KY-023 joystick module | On-device threshold menu |
| 6×1.5V battery pack | Replaced the original 9V battery, which could not supply enough current for the full system |
| HC-SR04 ultrasonic distance sensor | Detects a distance change and triggers the optional welcome-wave gesture |

## Wiring

| Signal | Pin |
|---|---|
| DHT11 data | GPIO4 |
| Thermistor (ADC) | GPIO34 |
| Servo signal | GPIO13 |
| Buzzer + | GPIO25 |
| Joystick Y-axis | GPIO35 |
| Joystick button | GPIO27 |
| OLED SDA / SCL | GPIO21 / GPIO22 |
| HC-SR04 TRIG | GPIO26 |
| HC-SR04 ECHO | GPIO14 (through a voltage divider, see below) |

Servo power comes from a separate 5V supply sharing ground with the ESP32 — not from the board's own 3V3/5V pins.

The HC-SR04 runs on 5V and its ECHO output idles at 5V, which exceeds the ESP32's 3.3V-only GPIO input tolerance. Put a voltage divider (e.g. 1kΩ in series, then 2kΩ to GND) between ECHO and GPIO14 to bring the pulse down to a safe ~3.3V. TRIG is driven by the ESP32 at 3.3V, which the HC-SR04 reads fine as logic-high, so no divider is needed on that line.

<!-- Add your wiring diagram image here, e.g. docs/wiring-diagram.png -->

## How it works

1. The DHT11 sensor is read at a fixed interval and the measured temperature is compared against the current upper and lower temperature limits.
2. A hysteresis state machine determines whether the device is in `warning` or `ok`, preventing the warning state from flickering near the temperature boundaries.
3. When enabled, the HC-SR04 is sampled periodically; if a valid reading differs from the last known-good reading by more than 10 cm, a three-wave welcome gesture starts. The servo moves between 45° and 90°, with the waving motion temporarily taking priority over temperature-based servo control.
4. When no welcome wave is active, the servo moves smoothly toward its temperature-controlled position, one degree at a time, independent of the sensor read interval.
5. The OLED redraws on its own fast timer so the eye-blink animation stays smooth regardless of sensor timing.
6. Temperature, humidity, and status are published to the Garden Spine MQTT broker over TLS on a slower interval.

## CAD / Enclosure

The mushroom enclosure is adapted from a public model on Printables, scaled
and modified to fit the ESP32, servo, breadboard, and battery:

**Mushroom House LED Lamp Decoration**
https://www.printables.com/model/1741928-mushroom-house-led-lamp-decoration

Check the listing for current license terms before reuse or redistribution.
The `cad/` folder in this repo holds only original additions on top of that
base model (the hand/flag piece, mounting notes, and any modifications made),
not a copy of the base file itself.

## Project structure

```
firmware/
  frost_sentinel.ino     — main sketch
  config.example.h       — credential template (copy to config.h, which is gitignored)
cad/
  — original design additions (hand/flag piece, mounting notes)
docs/
  — wiring diagram, notes
```

## Setup

1. Copy `firmware/config.example.h` to `firmware/config.h` and fill in your own Wi-Fi/MQTT credentials from your course credential slip.
2. Install the required Arduino libraries: `Adafruit_GFX`, `Adafruit_SSD1306`, `DHT sensor library`, `ESP32Servo`, and `GardenSpine`.
3. Flash `firmware/frost_sentinel.ino` to an ESP32 board.
4. Open Serial Monitor at 115200 baud to confirm Wi-Fi and MQTT connect.

## Known limitations

- Currently reads temperature from the DHT11 rather than the thermistor named in the original charter ("Thermistor Threshold Study") — wiring in the thermistor and calibrating it against the DHT11 is still open work.
- Default thresholds are a starting point, not calibrated frost values — adjust via the on-device menu and validate against real paired sensor data before treating them as final.
- The remote greenhouse-node subscription is implemented and receiving data but not yet surfaced anywhere in the UI.

## Credits

- Built for the Garden Spine IoT programme (ITEE Summer Programme), gardenspine.ikapo.fi.
- Enclosure adapted from a public mushroom house model on Printables — see the CAD / Enclosure section above for the link and license note.

## License

Firmware and code in this repository are MIT licensed — see [LICENSE](LICENSE). The enclosure design is derived from a third-party model under its own license terms (see CAD / Enclosure above) and is not covered by the MIT license.
