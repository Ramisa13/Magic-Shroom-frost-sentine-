// Copy this file to config.h and fill in your own values.
// config.h is the only file holding your credentials — never commit it.
// It is already excluded in .gitignore.

#pragma once

// Leave these four as-is unless your course network differs.
#define SPINE_WIFI_SSID "panoulu"
#define SPINE_WIFI_PASSWORD ""
#define SPINE_MQTT_HOST "gardenspine.ikapo.fi"
#define SPINE_MQTT_PORT 8883

// From your credential slip. The MQTT username is the same as the device ID.
#define SPINE_DEVICE_ID "fs-01"
#define SPINE_MQTT_USERNAME "fs-01"
#define SPINE_MQTT_PASSWORD "YOUR_MQTT_PASSWORD_HERE"

// From your charter page. These must match the registry exactly.
#define SPINE_ZONE "outdoor-1"
#define SPINE_DEVICE_TYPE "frost-node"

// Your own version string. Bump it so you can tell which build is running.
#define SPINE_FIRMWARE_VERSION "1.0.0"
