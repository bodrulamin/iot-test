# ESP32 WiFi Provisioning System

## Overview
This project implements a complete WiFi provisioning system with a captive portal for ESP32. When no WiFi credentials are saved, the device creates an Access Point and serves a web interface for WiFi configuration.

## Features

### 1. **Automatic WiFi Management**
- Checks for saved credentials on boot
- Automatically connects if credentials exist
- Falls back to AP mode if no credentials or connection fails

### 2. **Access Point Mode**
- **SSID**: `ESP32-Setup`
- **Password**: `12345678`
- **IP Address**: `192.168.4.1`
- DNS server for captive portal (auto-redirects all requests)

### 3. **Web Interface**

#### Main Page (`http://192.168.4.1/`)
- Scan WiFi Networks button
- Navigation menu

#### WiFi Scan Page (`/scan`)
- Lists all available WiFi networks with signal strength
- Click on network to auto-fill SSID
- Password input field
- Connect button

#### Device Info Page (`/info`)
- Chip model and cores
- Hardware revision
- WiFi features
- MAC address
- Current IP address
- Free heap memory
- Saved WiFi SSID

### 4. **Credential Storage**
- Uses NVS (Non-Volatile Storage) to save WiFi credentials
- Credentials persist across reboots
- Automatic reconnection on startup

### 5. **Auto-restart After Configuration**
- Device restarts 3 seconds after new credentials are saved
- Automatically connects to the configured network

## Files Created

1. **wifi_manager.h** - WiFi manager interface
2. **wifi_manager.c** - Main WiFi provisioning logic
3. **dns_server.h** - DNS server interface
4. **dns_server.c** - Captive portal DNS server
5. **main.c** - Main application
6. **CMakeLists.txt** - Build configuration

## How to Use

### Building and Flashing

```bash
# Build the project
source ~/esp/v5.5.1/esp-idf/export.sh
idf.py build

# Flash to ESP32
idf.py -p /dev/ttyUSB0 flash monitor
```

### First Time Setup

1. Power on the ESP32
2. Connect to WiFi network: `ESP32-Setup` (password: `12345678`)
3. Browser should open automatically, or navigate to `http://192.168.4.1`
4. Click "Scan WiFi Networks"
5. Click on your WiFi network from the list
6. Enter your WiFi password
7. Click "Connect"
8. ESP32 will restart and connect to your WiFi

### Subsequent Boots

- ESP32 automatically connects to saved WiFi
- If connection fails, it falls back to AP mode
- Web interface remains accessible for reconfiguration

## Configuration

You can modify these settings in `wifi_manager.h`:

```c
#define AP_SSID "ESP32-Setup"          // Change AP name
#define AP_PASSWORD "12345678"         // Change AP password
#define MAX_RETRY 5                    // WiFi connection retry attempts
```

## Build Output

- Binary size: ~820 KB (0xc8050 bytes)
- Free partition space: 22% (~228 KB)
- Bootloader size: ~26 KB

## Captive Portal Support

The system includes handlers for common captive portal detection URLs:
- `/generate_204` - Android (Google)
- `/gen_204` - Android alternative
- `/hotspot-detect.html` - iOS/macOS (Apple)
- `/success.txt` - Windows
- `/ncsi.txt` - Windows Network Connectivity Status Indicator
- `/connecttest.txt` - Windows 10/11
- `/chat` - Microsoft Teams/Windows
- `/redirect` - Generic redirect check

These URLs automatically redirect to the configuration page (HTTP 302), making the captive portal work seamlessly across all devices and triggering automatic popup notifications.

## Troubleshooting

1. **Cannot connect to ESP32-Setup**
   - Make sure password is `12345678`
   - Check that ESP32 is powered on and LED is active

2. **Web page not loading**
   - Navigate directly to `http://192.168.4.1`
   - Clear browser cache
   - Most devices should automatically open the captive portal

3. **WiFi connection fails**
   - Verify WiFi password is correct
   - Check WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)
   - Ensure WiFi signal strength is adequate

4. **ESP32 stuck in AP mode**
   - Saved credentials may be incorrect
   - Reconnect to AP and reconfigure

5. **Guru Meditation Error / Crash**
   - Fixed in latest version with captive portal handlers
   - Rebuild and reflash if using older version

## Technical Details

### Components Used
- ESP-IDF WiFi driver
- ESP HTTP Server
- NVS Flash for credential storage
- LwIP networking stack
- FreeRTOS for task management
- Custom DNS server for captive portal

### Memory Usage
- Heap memory available: Displayed on device info page
- Flash partitions: Standard ESP32 layout
- NVS partition: Stores WiFi credentials

## Next Steps

After the WiFi is configured, you can:
1. Add your application logic to `blink_example_main.c`
2. Access device info at `http://<device-ip>/info`
3. Reconfigure WiFi at any time via the web interface
4. Implement additional web pages as needed

## Notes

- The web interface uses responsive design for mobile compatibility
- All DNS queries are redirected to the device (captive portal)
- WiFi credentials are stored securely in NVS
- The system automatically handles connection failures
