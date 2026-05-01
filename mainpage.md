# GSM2SIP - GSM to SIP Call Bridge {#mainpage}

## Overview {#overview}

GSM2SIP creates a seamless bridge between your GSM mobile phone and your computer's
SIP/VOIP infrastructure. When your phone rings, the system automatically creates a
corresponding SIP call to your softphone, allowing you to handle mobile calls through
your computer's audio equipment and software.

## Features {#features}

- Automatic GSM call detection via oFono
- SIP call bridging to your softphone
- **Caller ID preservation** using P-Asserted-Identity header
- Real-time call state synchronization
- D-Bus integration (sdbus-c++)
- Bluetooth HFP support via BlueALSA
- Comprehensive Doxygen documentation for all APIs

## Caller ID Preservation {#caller_id}

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

## Architecture {#architecture}

The application uses the Observer pattern for call state management:

- @ref CallState - Central state management (Subject in Observer pattern)
- @ref GsmCallDetector - GSM call detection via oFono D-Bus (Observer)
- @ref SipClient - SIP client implementation (Observer)
- @ref SipCall - Individual SIP call handling
- @ref SipAccount - SIP account management
- @ref ICallObserver - Abstract observer base class

## API Documentation {#documentation}

For detailed API documentation, see:
- @ref CallState
- @ref GsmCallDetector
- @ref SipClient
- @ref SipCall
- @ref SipAccount
- @ref ICallObserver
- @ref SipConfig

## Quick Start {#usage}

```bash
mkdir build && cd build
cmake ..
make
./call_monitor config.json
```

For configuration details, see README.md
