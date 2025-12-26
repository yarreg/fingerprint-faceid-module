# Fingerprint & Face ID Access Control Module

ESP32-S3 based access control system with fingerprint and face recognition authentication capabilities.

![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-blue)
![MCU](https://img.shields.io/badge/MCU-ESP32--S3-green)
![License](https://img.shields.io/badge/License-CC0%201.0-lightgrey)

## Overview

This project implements a multi-factor access control module designed for secure door access. Users can authenticate using either fingerprint scanning or facial recognition. The system includes a web-based management interface, MQTT integration, and comprehensive event logging.

## Hardware Components

| Component | Description | Interface |
|-----------|-------------|-----------|
| **ESP32-S3FH4R2** | Main microcontroller (4MB Flash, 2MB PSRAM) | - |
| **GROW R502-A** | Fingerprint sensor module with RGB LED | UART (GPIO 10 TX, GPIO 11 RX) |
| **GROW F900** | Face recognition module | UART (GPIO 35 TX, GPIO 34 RX) |
| **VL53L0X** | Time-of-Flight distance sensor | I2C (GPIO 37 SDA, GPIO 36 SCL) |
| **Buzzer** | Audio feedback | GPIO 38 |
| **LED** | Visual indicator for face module | GPIO 18 |

### Pin Configuration

```
┌─────────────────────────────────────────────────────────────┐
│  R502-A Fingerprint Module                                  │
│    TX: GPIO 10  │  RX: GPIO 11  │  EN: GPIO 9  │  IRQ: GPIO 8│
├─────────────────────────────────────────────────────────────┤
│  F900 Face Recognition Module                               │
│    TX: GPIO 35  │  RX: GPIO 34  │  EN: GPIO 21              │
├─────────────────────────────────────────────────────────────┤
│  VL53L0X ToF Sensor                                         │
│    SDA: GPIO 37 │  SCL: GPIO 36 │  XSHUT: GPIO 17 │ IRQ: GPIO 33│
├─────────────────────────────────────────────────────────────┤
│  Peripherals                                                │
│    Buzzer: GPIO 38  │  LED: GPIO 18                         │
└─────────────────────────────────────────────────────────────┘
```

## Features

- **Dual Authentication**: Fingerprint and face recognition support
- **Web Interface**: Full configuration and management via browser
- **Wi-Fi Connectivity**: Access Point (AP) and Station (STA) modes
- **MQTT Integration**: Real-time event notifications to IoT platforms
- **OTA Updates**: Over-the-air firmware updates
- **Event Logging**: Persistent access event history
- **Distance Trigger**: Auto-activation when user approaches (ToF sensor)
- **Audio/Visual Feedback**: Buzzer tones and LED indicators

## Project Structure

```
fingerprint-faceid-module/
├── main/                          # Main application code
│   ├── main.c                     # Application entry point
│   ├── access_control.c           # Authentication logic
│   ├── wifi.c                     # Wi-Fi management
│   ├── web_*_handlers.c           # HTTP API handlers
│   └── include/                   # Header files
├── components/                    # ESP-IDF components
│   ├── r502/                      # R502-A fingerprint driver
│   ├── f900/                      # F900 face recognition driver
│   ├── vl53l0x/                   # VL53L0X ToF sensor driver
│   ├── buzzer/                    # Buzzer audio driver
│   ├── settings/                  # Configuration management
│   ├── tabledb/                   # NVS-based database
│   ├── webserver/                 # HTTP server component
│   ├── mqtt_helper/               # MQTT client wrapper
│   ├── log_redirect/              # Log capture system
│   └── static/files/              # Web interface assets
├── docs/                          # Hardware documentation
│   ├── F900-1.md, F900-2.md       # F900 module specs
│   └── R502.md                    # R502 module specs
└── specs/                         # Software specifications
    ├── REST-API.md                # API documentation
    └── README.ru.md               # Russian documentation
```

## Building & Flashing

### Prerequisites

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- ESP32-S3 development board or custom hardware

### Build Commands

```bash
# Set target to ESP32-S3
idf.py set-target esp32s3

# Configure project (optional)
idf.py menuconfig

# Build firmware
idf.py build

# Flash to device
idf.py -p COM<X> flash

# Monitor serial output
idf.py -p COM<X> monitor

# Build, flash, and monitor
idf.py -p COM<X> flash monitor
```

## Initial Setup

1. **Power On**: Module starts in Access Point mode on first boot
2. **Connect to Wi-Fi**: 
   - SSID: `Door-Module`
   - Password: `12345678`
3. **Open Web Interface**: Navigate to `http://192.168.4.1`
4. **Login**: 
   - Username: `admin`
   - Password: `admin`
5. **Configure Settings**:
   - Set Wi-Fi credentials for your network
   - Configure MQTT broker (optional)
   - Change admin credentials

## Web Interface

The module serves a responsive web interface with the following pages:

| Page | Path | Description |
|------|------|-------------|
| Dashboard | `/` | System overview and status |
| Fingerprints | `/fingerprint.html` | Manage fingerprint enrollments |
| Faces | `/face.html` | Manage face enrollments |
| Camera | `/camera.html` | Live camera feed |
| Settings | `/settings.html` | System configuration |
| Logs | `/log.html` | Event history |
| Update | `/update.html` | OTA firmware update |
| About | `/about.html` | System information |

## REST API

### Enrollment Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| `POST` | `/api/enrollment` | Start enrollment session |
| `GET` | `/api/enrollment` | Get enrollment status |
| `DELETE` | `/api/enrollment` | Cancel enrollment |
| `GET` | `/api/enrollments/{type}` | List enrolled items (fingerprint/face) |
| `DELETE` | `/api/enrollments/{type}/{id}` | Delete enrollment |
| `POST` | `/api/enrollments/{type}/{id}` | Update enrollment |

### System Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| `POST` | `/api/system/reboot` | Reboot device |
| `POST` | `/api/system/update` | Upload OTA firmware |
| `GET` | `/api/settings` | Get all settings |
| `POST` | `/api/config` | Update settings |

## Configuration Options

| Setting | Description | Default |
|---------|-------------|---------|
| `wifi_sta_ssid` | Wi-Fi network name | - |
| `wifi_sta_password` | Wi-Fi password | - |
| `ap_mode_enabled` | Enable AP mode | `true` |
| `ap_ssid` | AP network name | `Door-Module` |
| `ap_password` | AP password | `12345678` |
| `basic_auth_user` | Web login username | `admin` |
| `basic_auth_password` | Web login password | `admin` |
| `mqtt_enabled` | Enable MQTT | `false` |
| `mqtt_uri` | MQTT broker URI | - |
| `distance_threshold` | Activation distance (cm) | `30` |
| `distance_trigger_time` | Trigger duration (sec) | `3` |
| `buzzer_enabled` | Enable buzzer | `true` |
| `led_enabled` | Enable LED | `true` |

## Operation Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    System Startup                           │
│  1. Initialize GPIO, NVS, Wi-Fi                            │
│  2. Connect to stored Wi-Fi or start AP mode               │
│  3. Start ToF sensor continuous measurement                │
│  4. Start web server                                       │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│              Object Detection (ToF Sensor)                  │
│  Continuously measure distance. If object < 30cm for 3s:   │
│  → Enable R502-A (fingerprint) and F900 (face) modules     │
└───────────────────────────┬─────────────────────────────────┘
                            │
              ┌─────────────┴─────────────┐
              ▼                           ▼
┌─────────────────────────┐   ┌─────────────────────────┐
│  Fingerprint Auth       │   │  Face Recognition       │
│  Yellow LED = Ready     │   │  LED ON = Camera active │
│  Green LED = Success    │   │  Match found = Success  │
│  Red LED = Failed       │   │                         │
└───────────┬─────────────┘   └───────────┬─────────────┘
            │                             │
            └─────────────┬───────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                    Result Processing                        │
│  Success: Buzzer chime, MQTT notification, log event       │
│  Failure: Buzzer honk, MQTT notification, log event        │
└─────────────────────────────────────────────────────────────┘
```

## Sound Indicators

| Sound | Pattern | Event |
|-------|---------|-------|
| Short beep | 200ms | Scan step completed |
| Success chime | Pleasant tone | Authentication successful |
| Error honk | Buzz sound | Authentication failed |
| Long beep | 500ms | Operation canceled |

## MQTT Messages

When MQTT is enabled, the module publishes events:

```json
// Successful fingerprint authentication
{
  "fingerprint_access": "ok",
  "user": "John Doe"
}

// Failed fingerprint authentication
{
  "fingerprint_access": "fail"
}

// Successful face authentication
{
  "face_access": "ok", 
  "user": "Jane Doe"
}
```

## Development

### Adding New Components

1. Create directory under `components/<component_name>/`
2. Add source files (`.c`) in root, headers in `include/`
3. Create `CMakeLists.txt` with component registration
4. Include header in main application

### Adding HTTP Handlers

1. If endpoint fits existing category, add to `web_<xxx>_handlers.c`
2. For new categories, create `web_<xxx>_handlers.c`
3. Register handlers in `register_<xxx>_web_handlers()` function
4. Add declaration to `main/include/web_handlers.h`
5. Call registration in `start_and_configure_webserver()`

### Static Web Files

Place files in `components/static/files/`. They are:
- Automatically minified during build
- Embedded into firmware
- Served at `http://<device>/<filename>`

## Documentation

- [F900 Face Module](docs/F900-1.md) - Part 1
- [F900 Face Module](docs/F900-2.md) - Part 2
- [R502 Fingerprint Module](docs/R502.md)
- [REST API Reference](specs/REST-API.md)
- [Russian Documentation](specs/README.ru.md)

## License

This project is released under the Unlicense or CC0 1.0 License.

## Acknowledgments

- Espressif Systems for ESP-IDF framework
- GROW for R502-A and F900 module documentation
- STMicroelectronics for VL53L0X sensor
