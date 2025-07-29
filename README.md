# WOPR Display - ESPHome Component

*"The only winning move is not to play"*

This ESPHome custom component recreates the iconic blinking lights display from the WOPR (War Operation Plan Response) computer in the 1983 movie **WarGames**. Experience the mesmerizing random light patterns that captivated audiences worldwide, now seamlessly integrated with Home Assistant through ESPHome.

![WOPR Lights](https://gifs.cackhanded.net/wargames/lights.gif)

## Why ESPHome?

This component leverages ESPHome's powerful ecosystem:

- 🏠 **Home Assistant Integration**: Native support with automatic discovery
- 📱 **Mobile Control**: Control via Home Assistant app or web interface  
- 🔄 **OTA Updates**: Update firmware wirelessly
- 📊 **State Management**: Built-in persistence using ESP32's NVS (no SPIFFS needed)
- 🎛️ **YAML Configuration**: Easy setup and customization
- 🤖 **Automation Ready**: Integrate with Home Assistant automations
- 📈 **Monitoring**: Built-in logging and diagnostics

## Features

- 🎯 **Authentic WOPR Experience**: Movie-accurate random light patterns
- 🎛️ **Home Assistant Switch**: Turn on/off via HA interface
- 🔧 **Configurable**: Matrix count, brightness, timing intervals
- 💾 **State Persistence**: Remembers on/off state across reboots
- 🌐 **Network Ready**: Full WiFi integration via ESPHome
- 📟 **Scalable**: Support for 1-32 daisy chained LED matrices

## Hardware Requirements

- ESP32 development board
- 1-32x MAX7219 8x8 LED matrix modules (daisy chained)
- Connecting wires

## Wiring

| ESP32 Pin | MAX7219 Pin | Description |
|-----------|-------------|-------------|
| GPIO 19   | DIN         | Data Input  |
| GPIO 18   | CLK         | Clock       |
| GPIO 5    | CS          | Chip Select |
| 5V        | VCC         | Power       |
| GND       | GND         | Ground      |

*Note: CS pin can be customized in YAML configuration*

## Installation

### Prerequisites

1. **ESPHome Installed**: Follow the [ESPHome installation guide](https://esphome.io/guides/installing_esphome.html)
2. **Home Assistant** (optional but recommended)

### Setup Instructions

1. **Clone this repository**:
   ```bash
   git clone https://github.com/will-tm/esphome-wopr.git
   cd esphome-wopr
   ```

2. **Create your secrets file** (`secrets.yaml`):
   ```yaml
   # WiFi credentials
   wifi_ssid: "Your_WiFi_SSID"
   wifi_password: "Your_WiFi_Password"
   
   # API encryption key (generate with: esphome wizard)
   api_encryption_key: "your_32_char_base64_key_here"
   
   # OTA password
   ota_password: "your_ota_password"
   
   # Fallback AP password
   ap_password: "your_fallback_password"
   ```

3. **Customize the configuration**:
   Edit `wopr_display_example.yaml` and adjust:
   - `num_matrices`: Number of LED matrices (1-32)
   - `brightness`: LED brightness (0-15) 
   - `cs_pin`: Chip select GPIO pin
   - `min_interval_ms` / `max_interval_ms`: Animation timing

4. **Compile and upload**:
   ```bash
   esphome compile wopr_display_example.yaml
   esphome upload wopr_display_example.yaml
   ```

5. **Add to Home Assistant**:
   - The device will be auto-discovered
   - Go to Settings → Devices & Services → ESPHome
   - Click "Configure" on the discovered device

## Configuration Options

```yaml
switch:
  - platform: wopr_display
    name: "WOPR Animation"           # Display name in Home Assistant
    id: wopr_animation               # Internal ID for automations
    cs_pin: GPIO5                    # Chip select pin
    num_matrices: 12                 # Number of matrices (1-32)
    brightness: 2                    # LED brightness (0-15)
    min_interval_ms: 333            # Minimum animation interval
    max_interval_ms: 1332           # Maximum animation interval
```

## Usage

### Home Assistant

Once configured, you'll have:

- **Switch Entity**: `switch.wopr_animation` 
- **Controls**: Toggle on/off via HA interface
- **State Persistence**: Remembers setting across reboots
- **Automation Support**: Use in HA automations

### Example Automations

```yaml
# Turn on at sunset
automation:
  - alias: "WOPR Sunset Activation"
    trigger:
      - platform: sun
        event: sunset
    action:
      - switch.turn_on: wopr_animation

# Turn off at bedtime  
  - alias: "WOPR Bedtime"
    trigger:
      - platform: time
        at: "23:00:00"
    action:
      - switch.turn_off: wopr_animation
```

## Troubleshooting

### Common Issues

1. **Component not found**:
   - Ensure the `components/` folder is in your ESPHome project directory
   - Check that all files are present and properly named

2. **SPI errors**:
   - Verify wiring connections
   - Ensure SPI pins don't conflict with other components
   - Check power supply capacity for multiple matrices

3. **Home Assistant discovery**:
   - Verify API encryption key matches
   - Check WiFi connectivity
   - Restart Home Assistant if needed

### Debugging

Enable verbose logging:
```yaml
logger:
  level: DEBUG
  logs:
    wopr_display: DEBUG
```

## Matrix Configuration Examples

### Small Display (4 matrices)
```yaml
switch:
  - platform: wopr_display
    name: "Mini WOPR"
    cs_pin: GPIO5
    num_matrices: 4
    brightness: 5
```

### Large Display (24 matrices)  
```yaml
switch:
  - platform: wopr_display
    name: "Mega WOPR"
    cs_pin: GPIO5
    num_matrices: 24
    brightness: 1
    min_interval_ms: 200
    max_interval_ms: 800
```

## License

This project is licensed under the MIT License - see the [LICENSE](../LICENSE) file for details.
