# GSM2SIP - GSM to SIP Call Bridge

A C++ application that bridges GSM mobile calls to SIP/VOIP systems, enabling back-to-back call handling between your mobile phone and a computer-based softphone.

## Overview

GSM2SIP creates a seamless bridge between your GSM mobile phone and your computer's SIP/VOIP infrastructure. When your phone rings, the system automatically creates a corresponding SIP call to your softphone, allowing you to handle mobile calls through your computer's audio equipment and software.

The system works by connecting your phone via Bluetooth, using oFono to detect GSM calls, and automatically initiating SIP calls to your configured extension. This enables use cases like professional call recording, unified communications, call center integration, and CRM integration.

## Features

- Automatic GSM call detection via oFono
- SIP call bridging to your softphone
- **Caller ID preservation** using P-Asserted-Identity header
- Real-time call state synchronization
- D-Bus integration (sdbus-c++)
- Bluetooth HFP support via BlueALSA
- **Comprehensive Doxygen documentation** for all APIs and internal logic

## Caller ID Preservation

When an incoming GSM call arrives, the system preserves the original caller's phone number and forwards it to your softphone via the SIP `P-Asserted-Identity` header. This ensures your softphone displays who actually called your mobile phone, not your system's number.

```
Incoming GSM Call          SIP Call to Softphone
─────────────────────      ───────────────────────

📱 +1 (555) 123-4567   →   🖥️ SIP Extension 1005
       │                          │
       │  Original caller         │  Displays:
       │  number forwarded        │  "+1 (555) 123-4567"
       │                          │
       └──────────────────────────┘
         P-Asserted-Identity header
```

**Note:** Your SIP server must support the `P-Asserted-Identity` header for this feature to work.

## Architecture

The application connects your GSM phone via Bluetooth to a Linux PC running oFono and BlueALSA. When a call is detected, it automatically initiates a SIP call to your configured softphone extension through your SIP/VOIP server.

```
        ┌─────────────────┐
        │  GsmCallDetector│
        └────────┬────────┘
                 │ updates
                 ↓
        ┌─────────────────┐
        │   CallState     │◄────┐
        └────────┬────────┘     │
                 │ notifies     │
                 ↓              │
        ┌─────────────────┐     │
        │   SipClient     │     │
        └────────┬────────┘     │
                 │              │
        ┌────────┴──────────┐   │
        │ D-Bus Integration │   │
        └───────────────────┘   │
                                │
                           Observer pattern
```

## Building

### Prerequisites

- CMake 3.10 or higher
- C++17 compatible compiler
- sdbus-c++ library
- oFono telephony stack
- BlueALSA (for Bluetooth audio routing)
- nlohmann-json library (for config file parsing)
- PJSIP library (for SIP client functionality)

### Build Instructions

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

```bash
./call_monitor config.json
```

1. Pair your phone via Bluetooth with HFP enabled
2. Wait for SIP registration confirmation
3. When your phone rings, your softphone will automatically ring
4. Answer the softphone to handle the call

**Requirements**: Bluetooth-capable phone, Linux PC, oFono, BlueALSA, SIP server, softphone

## Configuration

Create `config.json`:

```json
{
  "pcm_path": "/org/bluealsa/hci0/dev_11_22_33_44_55_66/hfphf/source",
  "sip": {
    "server": "sip.example.com",
    "port": 52370,
    "user": "your_username",
    "password": "your_password",
    "extension": "1005"
  }
}
```

Find your PCM path: `bluealsa-cli list-pcms`

## BlueALSA Setup

Configure BlueALSA with HFP oFono profile in `/etc/systemd/system/bluealsa.service`:

```ini
[Unit]
Description=BlueALSA Bluetooth Audio Daemon
Requires=bluetooth.service
After=bluetooth.service dbus.service

[Service]
Type=simple
ExecStart=/usr/bin/bluealsa -p a2dp-sink -p hfp-ofono -p hsp-hs
Restart=always
RestartSec=3
User=root
Group=audio

[Install]
WantedBy=multi-user.target
```

Enable: `sudo systemctl enable bluealsa && sudo systemctl start bluealsa`

## Troubleshooting

**No GSM call events**: Check `systemctl status ofono`, verify Bluetooth HFP is enabled, confirm PCM path: `bluealsa-cli list-pcms`

**SIP registration fails**: Verify server accessibility, check credentials in config.json, ensure extension exists

**Build errors**: Install dependencies: `libdbus-1-dev`, `nlohmann-json3-dev`, `libpjproject-dev`
