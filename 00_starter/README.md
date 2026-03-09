# 00_starter - ESP32 Template Starter Project

This is the minimal starter template for creating new ESP32 projects using the shared template system.

## Quick Start

1. **Copy this directory** to create a new project:
   ```bash
   cp -r 00_starter XX_myproject
   ```

2. **Update platformio.ini**:
   - Change `hostname` to a unique identifier (e.g., `esp32_XX`)
   - Set appropriate `board` type (`esp32cam`, `esp32dev`, etc.)
   - Update `upload_port` for USB uploads
   - Add any project-specific build flags or library dependencies

3. **Run `make links`** from the repository root to create symlinks to the template:
   ```bash
   cd /path/to/ESP32_projects
   make links
   ```

4. **Build and upload**:
   ```bash
   cd XX_myproject
   pio run                    # Build only
   pio run --target upload    # OTA upload (default)
   pio run -e esp32usb --target upload  # USB upload
   pio device monitor         # Serial monitor
   ```

## Project Structure

```
XX_myproject/
├── platformio.ini    # Project configuration (edit this!)
├── src/
│   └── main.cpp      # Your application code
├── data/             # SPIFFS files (HTML, etc.) - symlinked
├── include/          # Header files - symlinked to template
└── tools/            # Build tools - symlinked
```

## Template Modules

The template system provides these modules (via symlinks):

| Module | Purpose |
|--------|---------|
| `tpl_system` | Core system, FreeRTOS tasks, deep sleep |
| `tpl_wifi` | WiFi connection + OTA updates |
| `tpl_webserver` | HTTP server with standard endpoints |
| `tpl_websocket` | WebSocket server for real-time data |
| `tpl_net_watchdog` | Network health monitoring |
| `tpl_command` | Command dispatch framework |
| `tpl_telegram` | Telegram bot integration |
| `tpl_esp_camera` | Camera support (ESP32-CAM only) |
| `tpl_broadcast` | UDP broadcast messaging |

## Common Build Flags

Add these to `platformio.ini` as needed:

```ini
build_flags =
    # Board type
    -DIS_ESP32CAM          # ESP32-CAM board
    -DIS_ESP32CYD          # Cheap Yellow Display

    # Features
    -DHAS_STEPPERS         # Enable stepper motor support
    -DMAX_ON_TIME_S=60     # Auto-restart after 60 seconds

    # Telegram bot
    -DBOTtoken=MyBot_token # Bot token variable name

    # Debug
    -DCORE_DEBUG_LEVEL=5   # Maximum debug output
    -DCORE_DEBUG_LEVEL=1   # Minimal debug output
```

## Standard HTTP Endpoints

The webserver provides these built-in endpoints:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/digital` | GET | Read all GPIO states |
| `/digitalwrite?pin=X&value=0\|1` | GET | Set GPIO output |
| `/analog?pin=X` | GET | Read analog value |
| `/restart` | GET | Restart the device |
| `/deepsleep` | GET | Enter deep sleep |
| `/allowsleep` | GET | Allow deep sleep |
| `/nosleep` | GET | Prevent deep sleep |
| `/stack` | GET | Show stack usage |
| `/cpu` | GET | Show CPU load |
| `/update` | POST | OTA firmware upload |

## WebSocket Commands

Send JSON messages to control the device:

```json
{"pin_to_high": 13}      // Set GPIO high
{"pin_to_low": 13}       // Set GPIO low
{"as_input_pullup": 12}  // Set as input with pullup
{"sleep": true}          // Allow deep sleep
{"save_config": true}    // Save config to SPIFFS
```

## Adding Custom Code

### Custom Command Handler

```cpp
void execute(enum Command command) {
  switch (command) {
    case CmdIdle:
      break;
    // Add custom commands here
    default:
      break;
  }
}

// In setup():
tpl_command_setup(execute);
```

### Custom WebSocket Data

```cpp
void add_ws_info(DynamicJsonDocument* json) {
  (*json)["my_sensor"] = readSensor();
}

// In setup():
tpl_websocket_setup(add_ws_info, NULL);
```

### Custom WebSocket Command Handler

```cpp
void process_json(DynamicJsonDocument* json) {
  if (json->containsKey("my_command")) {
    // Handle custom command
  }
}

// In setup():
tpl_websocket_setup(NULL, process_json);
```

## Deep Sleep

To enable deep sleep, change `tpl_system_setup(0)` to `tpl_system_setup(seconds)`:

```cpp
void setup() {
  tpl_system_setup(60);  // Sleep for 60 seconds
  // ... rest of setup
}
```

Deep sleep requires:
- `tpl_config.allow_deepsleep = true`
- No OTA in progress
- All communication tasks idle

## Troubleshooting

### Build fails with "template.h not found"
Run `make links` from the repository root.

### WiFi doesn't connect
Check `wifi_secrets.cpp` in `.private/` directory has correct credentials.

### OTA upload fails
- Ensure device is on same network
- Check mDNS: `ping esp32_XX.local`
- Verify OTA port (default: 3232)

### Serial monitor shows garbage
- Check baud rate is 115200
- Try pressing reset on the device

## Related Documentation

- [Template System](../prj_template/) - Core template modules
- [CLAUDE.md](../CLAUDE.md) - Build system and conventions
- [PlatformIO Docs](https://docs.platformio.org/)
