#pragma once

#include "CallState.hpp"
#include <pjsua2.hpp>
#include <string>
#include <memory>
#include <atomic>

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Configuration structure for SIP client settings
 *
 * @details Holds all necessary parameters for connecting to a SIP server,
 * including authentication credentials and connection settings.
 */
struct SipConfig {
    std::string server;       ///< SIP server domain or IP address
    std::string user;         ///< SIP username/extension for authentication
    std::string password;     ///< SIP password for authentication
    uint16_t port = 5060;     ///< SIP server port (default: 5060)
    std::string extension = "1022"; ///< Default extension to call when GSM call is detected

    /**
     * @brief Validates the SIP configuration
     *
     * @details Checks if all required fields (server, user, password) are non-empty
     *
     * @return true if configuration is valid, false otherwise
     */
    bool isValid() const {
        return !server.empty() && !user.empty() && !password.empty();
    }
};

// ============================================================================
// Constants
// ============================================================================

/**
 * @brief Namespace containing SIP-related constants
 *
 * @details Defines operational limits and timing parameters for SIP operations
 */
namespace SipConstants {
    constexpr int MAX_CALLS = 1;            ///< Maximum number of simultaneous SIP calls allowed
    constexpr int LOG_LEVEL = 4;            ///< PJSIP log level (0-5, where 5 is most verbose)
    constexpr int CALL_CHECK_INTERVAL_MS = 100; ///< Polling interval for call state checks in milliseconds
}

// ============================================================================
// SipCall - Handles individual SIP calls
// ============================================================================

/**
 * @brief Represents a single SIP call and manages its lifecycle
 *
 * @details Extends PJSUA2's Call class to handle SIP call events, state transitions,
 * and media management. Integrates with the shared CallState to synchronize
 * SIP call state with GSM call state.
 *
 * The class handles:
 * - Call state changes (incoming, active, disconnected, etc.)
 * - Audio media connection and routing
 * - Bidirectional state synchronization with GSM calls
 */
class SipCall : public pj::Call {
public:
    /**
     * @brief Constructs a SIP call associated with an account
     *
     * @param account The SIP account that owns this call
     */
    SipCall(pj::Account& account) : pj::Call(account) {}

    /**
     * @brief Callback invoked when the call state changes
     *
     * @details Handles PJSIP call state transitions and updates the shared CallState
     * accordingly. Maps PJSIP states to GSM-compatible states.
     *
     * @param prm Parameter containing call state information
     */
    void onCallState(pj::OnCallStateParam& prm) override;

    /**
     * @brief Callback invoked when the call media state changes
     *
     * @details Handles audio media initialization and connection when media becomes active
     *
     * @param prm Parameter containing media state information
     */
    void onCallMediaState(pj::OnCallMediaStateParam& prm) override;

    /**
     * @brief Sets the shared call state for synchronization
     *
     * @param state Shared pointer to the CallState object
     */
    void setState(std::shared_ptr<CallState> state) { m_state = state; }

    /**
     * @brief Checks if the call is in a specific state
     *
     * @details Safely queries the current call state and compares it to the target state
     *
     * @param state The PJSIP invite session state to check against
     * @return true if the call is in the specified state, false otherwise or on error
     */
    bool isState(pjsip_inv_state state) const;

private:
    /**
     * @brief Connects audio media for the active call
     *
     * @details Establishes audio transmission between capture device and the call media,
     * and from call media to playback device. Uses NULL audio device for BlueALSA integration.
     *
     * @throws pj::Error if audio media connection fails
     */
    void connectAudio();

    std::shared_ptr<CallState> m_state; ///< Shared call state for synchronization with GSM
};

// ============================================================================
// SipAccount - Manages SIP registration
// ============================================================================

/**
 * @brief Represents a SIP account and handles registration with the SIP server
 *
 * @details Extends PJSUA2's Account class to manage SIP registration lifecycle.
 * Monitors registration state changes and logs status updates.
 *
 * Registration states include:
 * - Registered: Successfully authenticated with SIP server
 * - Unregistered: Not registered or registration failed
 */
class SipAccount : public pj::Account {
public:
    /**
     * @brief Constructs a SIP account with shared state
     *
     * @param state Shared pointer to CallState for potential future integration
     */
    SipAccount(std::shared_ptr<CallState> state) : m_state(state) {}

    /**
     * @brief Callback invoked when registration state changes
     *
     * @details Handles registration success/failure events and logs the current status.
     * Useful for monitoring connectivity to the SIP server.
     *
     * @param prm Parameter containing registration state information
     */
    void onRegState(pj::OnRegStateParam& prm) override;

private:
    std::shared_ptr<CallState> m_state; ///< Shared call state (currently unused but reserved for future use)
};

// ============================================================================
// SipClient - Main SIP client implementation
// ============================================================================

/**
 * @brief Main SIP client that manages SIP communication and bridges GSM calls to SIP
 *
 * @details Implements the Observer pattern to react to GSM call state changes and
 * automatically initiate SIP calls to a configured extension when a GSM call is detected.
 *
 * Key responsibilities:
 * - Initialize and manage PJSIP library lifecycle
 * - Register with SIP server using provided credentials
 * - Monitor GSM call state changes via CallState observer pattern
 * - Automatically make SIP calls to configured extension on incoming GSM calls
 * - Manage audio routing (uses NULL device for BlueALSA integration)
 * - Handle SIP call state changes and cleanup
 *
 * Integration flow:
 * 1. GSM call detected (incoming) → SipClient makes SIP call to extension
 * 2. SIP call answered → SipClient signals GSM call to be answered
 * 3. GSM call disconnected → SipClient hangs up SIP call
 * 4. SIP call disconnected → SipClient signals GSM call to be rejected
 *
 * Thread safety: Uses atomic flag for running state. PJSIP operations are thread-safe.
 */
class SipClient : public ICallObserver {
public:
    /**
     * @brief Constructs a SIP client with the given configuration
     *
     * @details Validates the configuration and registers as an observer of CallState.
     * Throws std::invalid_argument if configuration is invalid.
     *
     * @param state Shared pointer to CallState for monitoring GSM call events
     * @param config SIP configuration containing server, credentials, and extension
     * @throws std::invalid_argument if configuration is invalid
     */
    SipClient(std::shared_ptr<CallState> state, const SipConfig& config);

    /**
     * @brief Destructor - cleans up PJSIP resources
     *
     * @details Unregisters from CallState observation and performs cleanup
     */
    ~SipClient();

    /**
     * @brief Initializes the SIP client and establishes connection to SIP server
     *
     * @details Performs the following initialization steps:
     * 1. Creates and configures PJSIP endpoint
     * 2. Sets NULL audio device for BlueALSA integration
     * 3. Creates UDP transport
     * 4. Creates and registers SIP account
     *
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize();

    /**
     * @brief Checks if the SIP client is currently running
     *
     * @return true if client is initialized and running, false otherwise
     */
    bool isRunning() const { return m_running.load(); }

    /**
     * @brief Observer callback for GSM call state changes
     *
     * @details Reacts to GSM call state transitions:
     * - Incoming: Makes SIP call to configured extension
     * - Answer/Reject: Logs the action (handled by other observers)
     * - Disconnected: Cleans up SIP call resources
     *
     * @param state The CallState with updated information
     */
    void onCallStateChanged(const CallState& state) override;

private:
    /**
     * @brief Initializes the PJSIP library and endpoint
     *
     * @details Creates PJSIP endpoint, configures max calls and logging level,
     * sets NULL audio device, creates UDP transport, and starts the library.
     *
     * @return true if PJSIP initialization succeeded, false otherwise
     */
    bool initPjsip();

    /**
     * @brief Creates and configures the SIP account
     *
     * @details Creates a SipAccount with authentication credentials from config
     * and initiates registration with the SIP server.
     *
     * @return true if account creation succeeded, false otherwise
     */
    bool createAccount();

    /**
     * @brief Cleans up PJSIP resources
     *
     * @details Performs the following cleanup steps:
     * 1. Hangs up any active call
     * 2. Unregisters SIP account
     * 3. Destroys PJSIP endpoint
     *
     * Safe to call multiple times (checks running flag).
     */
    void cleanup();

    /**
     * @brief Initiates a SIP call to the specified extension
     *
     * @details Creates a SipCall object and initiates an outgoing call to the
     * target URI. Caller ID headers are set to forward the original GSM caller's number.
     *
     * Preconditions:
     * - SIP account must be registered
     * - No active call should exist
     *
     * @param extension The extension number to call
     * @return true if call initiation succeeded, false otherwise
     */
    bool makeCall(const std::string& extension);

    /**
     * @brief Validates if a new call can be initiated
     *
     * @details Checks registration status and ensures no active call exists.
     * Logs specific error messages for each validation failure.
     *
     * @return true if call can be initiated, false otherwise
     */
    bool canInitiateCall() const;

    /**
     * @brief Creates call parameters with caller ID header
     *
     * @details Builds PJSIP call operation parameters and optionally adds
     * P-Asserted-Identity header with the original GSM caller's number.
     *
     * @return CallOpParam configured for the outgoing call
     */
    pj::CallOpParam createCallParams() const;

    /**
     * @brief Hangs up the current active SIP call
     *
     * @details Safely terminates the active call and resets the call pointer.
     * No-op if no call is active.
     */
    void hangupCall();

    /**
     * @brief Checks if the SIP account is registered with the server
     *
     * @details Queries the account's registration status from PJSIP
     *
     * @return true if registered and active, false otherwise
     */
    bool isRegistered() const;

    /**
     * @brief Builds a SIP URI from a target extension
     *
     * @details Constructs a SIP URI in the format: sip:[target@]server:port
     *
     * @param target The extension or user (empty string for registrar URI)
     * @return Complete SIP URI string
     */
    std::string buildUri(const std::string& target) const;

    std::shared_ptr<CallState> m_state; ///< Shared call state for GSM integration
    SipConfig m_config;                  ///< SIP configuration

    pj::Endpoint m_endpoint;             ///< PJSIP endpoint instance
    std::unique_ptr<SipAccount> m_account; ///< SIP account for registration
    std::unique_ptr<SipCall> m_currentCall; ///< Current active SIP call (if any)

    std::atomic<bool> m_running{false};   ///< Atomic flag indicating if client is running
};
