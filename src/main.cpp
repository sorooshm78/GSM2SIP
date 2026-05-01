#include <sdbus-c++/sdbus-c++.h>
#include <iostream>
#include <string>
#include <memory>
#include <fstream>
#include <nlohmann/json.hpp>
#include "CallState.hpp"
#include "GsmCallDetector.hpp"
#include "SipClient.hpp"

using json = nlohmann::json;

/**
 * @brief Main entry point for the GSM2SIP bridge application
 *
 * @details This application bridges GSM calls to SIP calls by:
 * 1. Loading configuration from a JSON file
 * 2. Creating D-Bus connection to monitor oFono call events
 * 3. Setting up GSM call detector to monitor call state
 * 4. Initializing SIP client to make outgoing calls
 * 5. Entering event loop to process D-Bus signals
 *
 * Configuration file format (JSON):
 * @code
 * {
 *   "pcm_path": "/org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX/bluealsa/hci0/pcm16",
 *   "sip": {
 *     "server": "sip.example.com",
 *     "user": "username",
 *     "password": "password",
 *     "port": 5060,
 *     "extension": "1005"
 *   }
 * }
 * @endcode
 *
 * @param argc Argument count (must be 2)
 * @param argv Argument values (argv[0]=program path, argv[1]=config file path)
 * @return 0 on success, 1 on error
 */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <config.json>\n";
        std::cerr << "Example: " << argv[0] << " config.json\n";
        return 1;
    }

    // Read config file
    std::string configPath = argv[1];
    std::ifstream configFile(configPath);
    if (!configFile.is_open()) {
        std::cerr << "Error: Cannot open config file: " << configPath << std::endl;
        return 1;
    }

    json config;
    try {
        configFile >> config;
    } catch (const json::parse_error& e) {
        std::cerr << "Error: Failed to parse config file: " << e.what() << std::endl;
        return 1;
    }

    // Validate required fields
    if (!config.contains("pcm_path") || !config.contains("sip")) {
        std::cerr << "Error: Config file must contain 'pcm_path' and 'sip' sections" << std::endl;
        return 1;
    }

    auto sipConfigJson = config["sip"];
    if (!sipConfigJson.contains("server") || !sipConfigJson.contains("user") || !sipConfigJson.contains("password")) {
        std::cerr << "Error: SIP config must contain 'server', 'user', and 'password' fields" << std::endl;
        return 1;
    }

    const std::string pcmPath = config["pcm_path"];
    const char* service = "org.bluealsa";
    const char* pcmInterface = "org.bluealsa.PCM1";
    const char* propsInterface = "org.freedesktop.DBus.Properties";

    // Parse SIP configuration from config file
    SipConfig sipConfig;
    sipConfig.server = sipConfigJson["server"];
    sipConfig.user = sipConfigJson["user"];
    sipConfig.password = sipConfigJson["password"];
    sipConfig.port = sipConfigJson.value("port", 52370);
    sipConfig.extension = sipConfigJson.value("extension", "1005");

    std::cout << "SIP client enabled: " << sipConfig.user << "@" << sipConfig.server
              << " → extension " << sipConfig.extension << std::endl;

    // Main initialization block
    try {
        auto connection = sdbus::createSystemBusConnection();

        // Create shared CallState
        auto callState = std::make_shared<CallState>();

        // Derive oFono modem path
        std::string modemPathStr;
        size_t hci_pos = pcmPath.find("/hci");
        if (hci_pos != std::string::npos) {
            size_t dev_start = pcmPath.find("/dev_", hci_pos);
            if (dev_start != std::string::npos) {
                size_t dev_end = pcmPath.find('/', dev_start + 5);
                if (dev_end == std::string::npos) dev_end = pcmPath.length();
                std::string hci_segment = pcmPath.substr(hci_pos, dev_start - hci_pos);
                std::string dev_segment = pcmPath.substr(dev_start, dev_end - dev_start);
                modemPathStr = "/hfp/org/bluez" + hci_segment + dev_segment;
            }
        }

        std::cout << "Using oFono modem: " << (modemPathStr.empty() ? "(none)" : modemPathStr) << std::endl;

        // Create and initialize GsmCallDetector
        auto gsmCallDetector = std::make_shared<GsmCallDetector>(callState, *connection, modemPathStr);
        gsmCallDetector->initialize();

        // Create SIP client
        auto sipClient = std::make_shared<SipClient>(callState, sipConfig);

        if (!sipClient->initialize()) {
            std::cerr << "Error: Failed to initialize SIP client" << std::endl;
            return 1;
        }

        // BlueALSA PCM proxy
        sdbus::ServiceName serviceName{service};
        sdbus::ObjectPath objectPath{pcmPath.c_str()};
        auto proxy = sdbus::createProxy(*connection, std::move(serviceName), std::move(objectPath));


        connection->enterEventLoop();

        // Cleanup SIP client
        sipClient.reset();
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
