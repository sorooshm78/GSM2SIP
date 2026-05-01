#include "SipClient.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace SipConstants;

// ============================================================================
// SipClient
// ============================================================================

/**
 * @brief Constructs a SipClient with the given configuration
 *
 * @details Validates the SIP configuration and registers as an observer
 * of the shared CallState. Throws an exception if configuration is invalid.
 *
 * @param state Shared call state for monitoring GSM call events
 * @param config SIP configuration with server, credentials, and extension
 * @throws std::invalid_argument if configuration is invalid
 */
SipClient::SipClient(std::shared_ptr<CallState> state, const SipConfig& config)
    : m_state(state), m_config(config) {

    if (!m_config.isValid()) {
        throw std::invalid_argument("Invalid SIP configuration");
    }

    observe(m_state.get());
}

/**
 * @brief Destructor - cleans up PJSIP resources and unregisters observer
 *
 * @details Calls cleanup() to release PJSIP resources and unregisters
 * from CallState observation to prevent dangling pointers.
 */
SipClient::~SipClient() {
    cleanup();
    stopObserving(m_state.get());
}

/**
 * @brief Initializes the SIP client and establishes connection to SIP server
 *
 * @details Performs the following initialization steps:
 * 1. Creates and configures PJSIP library endpoint
 * 2. Creates SIP account with credentials from config
 * 3. Initiates registration with SIP server
 * 4. Sets running flag to true
 *
 * If any step fails, performs cleanup and returns false.
 *
 * @return true if initialization succeeded, false otherwise
 */
bool SipClient::initialize() {
    try {
        if (!initPjsip()) {
            return false;
        }

        if (!createAccount()) {
            cleanup();
            return false;
        }

        m_running = true;
        std::cout << "[SipClient] Initialized successfully" << std::endl;
        return true;

    } catch (const pj::Error& err) {
        std::cerr << "[SipClient] Initialization failed: " << err.info()
                  << " (status=" << err.status << ")" << std::endl;
        cleanup();
        return false;
    }
}

/**
 * @brief Cleans up PJSIP resources and shuts down the SIP client
 *
 * @details Performs the following cleanup steps:
 * 1. Checks running flag (no-op if already cleaned up)
 * 2. Hangs up any active SIP call
 * 3. Unregisters SIP account from server
 * 4. Destroys PJSIP endpoint and library
 *
 * Safe to call multiple times (idempotent).
 * Errors are logged but don't throw exceptions.
 */
void SipClient::cleanup() {
    if (!m_running.load()) {
        return;
    }

    m_running = false;

    try {
        hangupCall();

        if (m_account) {
            m_account->setRegistration(false);
            pj_thread_sleep(SipConstants::CALL_CHECK_INTERVAL_MS);
        }

        m_endpoint.libDestroy();
        std::cout << "[SipClient] Cleaned up" << std::endl;

    } catch (const pj::Error& err) {
        std::cerr << "[SipClient] Error during cleanup: " << err.info() << std::endl;
    }
}

/**
 * @brief Initializes the PJSIP library and endpoint
 *
 * @details Creates and configures the PJSIP endpoint with the following settings:
 * - Sets maximum concurrent calls to 1
 * - Configures logging level
 * - Sets NULL audio device (for BlueALSA integration)
 * - Creates UDP transport on random port
 * - Starts the PJSIP library
 *
 * @return true if PJSIP initialization succeeded, false otherwise
 */
bool SipClient::initPjsip() {
    try {
        m_endpoint.libCreate();

        pj::EpConfig cfg;
        cfg.uaConfig.maxCalls = MAX_CALLS;
        cfg.logConfig.level = LOG_LEVEL;
        cfg.logConfig.consoleLevel = LOG_LEVEL;

        m_endpoint.libInit(cfg);

        // Set NULL audio device since audio is handled by BlueALSA
        pj::AudDevManager& audioMgr = m_endpoint.audDevManager();
        audioMgr.setNullDev();
        std::cout << "[SipClient] NULL audio device set" << std::endl;

        m_endpoint.libStart();

        pj::TransportConfig tcfg;
        tcfg.port = 0;
        m_endpoint.transportCreate(PJSIP_TRANSPORT_UDP, tcfg);

        std::cout << "[SipClient] UDP transport created" << std::endl;
        return true;

    } catch (const pj::Error& err) {
        std::cerr << "[SipClient] Failed to create transport: " << err.info() << std::endl;
        return false;
    }
}

/**
 * @brief Creates and configures the SIP account
 *
 * @details Creates a SipAccount object with authentication credentials
 * from the configuration and initiates registration with the SIP server.
 *
 * Account configuration includes:
 * - SIP URI (user@server)
 * - Registrar URI
 * - Authentication credentials (username, password)
 *
 * @return true if account creation succeeded, false otherwise
 */
bool SipClient::createAccount() {
    try {
        m_account = std::make_unique<SipAccount>(m_state);

        pj::AccountConfig acc_cfg;
        acc_cfg.idUri = buildUri(m_config.user);
        acc_cfg.regConfig.registrarUri = buildUri("");
        acc_cfg.sipConfig.authCreds.push_back(
            pj::AuthCredInfo("digest", "*", m_config.user, 0, m_config.password)
        );

        m_account->create(acc_cfg);

        std::cout << "[SipClient] Account created: " << acc_cfg.idUri << std::endl;
        return true;

    } catch (const pj::Error& err) {
        std::cerr << "[SipClient] Account creation failed: " << err.info() << std::endl;
        return false;
    }
}

/**
 * @brief Observer callback for GSM call state changes
 *
 * @details Reacts to GSM call state transitions and manages SIP calls accordingly:
 *
 * - Incoming: Makes SIP call to configured extension.
 *   If SIP call fails, sets CallState to Reject to terminate GSM call.
 *
 * - Answer: Logs that call is being answered (handled by other observers)
 *
 * - Reject: Logs that call is being rejected (handled by other observers)
 *
 * - Disconnected: Cleans up SIP call resources
 *
 * - Other states: No action
 *
 * This creates a bridge where GSM calls trigger SIP calls to an extension,
 * allowing the call to be handled by a SIP phone system.
 *
 * @param state The CallState with updated information
 */
void SipClient::onCallStateChanged(const CallState& state) {
    const CallState::State callState = state.getState();
    std::cout << "[SipClient] State changed: "
              << CallState::stateToString(callState) << std::endl;

    switch (callState) {
        case CallState::State::Incoming:
            std::cout << "  " << state.getFrom() << " -> " << state.getTo() << std::endl;

            if (!makeCall(m_config.extension)) {
                std::cerr << "[SipClient] Failed to initiate SIP call" << std::endl;
                m_state->setState(CallState::State::Reject);
                return;
            }

            break;

        case CallState::State::Answer:
            std::cout << "  Answering call..." << std::endl;
            break;

        case CallState::State::Reject:
            std::cout << "  Rejecting call..." << std::endl;
            break;

        case CallState::State::Disconnected:
            std::cout << "  Call ended" << std::endl;
            m_currentCall.reset();
            break;

        default:
            break;
    }

    std::cout.flush();
}

/**
 * @brief Validates if a new call can be initiated
 *
 * @details Checks registration status and ensures no active call exists.
 * Logs specific error messages for each validation failure.
 *
 * @return true if call can be initiated, false otherwise
 */
bool SipClient::canInitiateCall() const {
    if (!isRegistered()) {
        std::cerr << "[SipClient] Cannot initiate call: Not registered with SIP server" << std::endl;
        return false;
    }

    if (m_currentCall) {
        std::cerr << "[SipClient] Cannot initiate call: Call already in progress" << std::endl;
        return false;
    }

    return true;
}

/**
 * @brief Creates call parameters with caller ID header
 *
 * @details Builds PJSIP call operation parameters and optionally adds
 * P-Asserted-Identity header with the original GSM caller's number.
 *
 * The P-Asserted-Identity header allows the SIP extension to see the
 * original GSM caller's number, improving call traceability.
 *
 * @return CallOpParam configured for the outgoing call
 */
pj::CallOpParam SipClient::createCallParams() const {
    pj::CallOpParam prm;

    const std::string originalCaller = m_state->getFrom();
    if (!originalCaller.empty()) {
        pj::SipHeader paiHeader;
        paiHeader.hName = "P-Asserted-Identity";
        paiHeader.hValue = "<sip:" + originalCaller + "@" + m_config.server + ">";
        prm.txOption.headers.push_back(paiHeader);
        std::cout << "[SipClient] Setting caller ID to: " << originalCaller << std::endl;
    }

    return prm;
}

/**
 * @brief Initiates a SIP call to the specified extension
 *
 * @details Performs the following steps:
 * 1. Validates prerequisites (registration, no active call)
 * 2. Creates a new SipCall object and sets its state
 * 3. Builds the target SIP URI
 * 4. Creates call parameters with caller ID forwarding
 * 5. Initiates the call via PJSIP
 *
 * The caller ID is forwarded via P-Asserted-Identity header to allow
 * the SIP extension to see the original GSM caller's number.
 *
 * @param extension The extension number to call
 * @return true if call initiation succeeded, false otherwise
 */
bool SipClient::makeCall(const std::string& extension) {
    if (!canInitiateCall()) {
        return false;
    }

    try {
        m_currentCall = std::make_unique<SipCall>(*m_account);
        m_currentCall->setState(m_state);

        const std::string uri = buildUri(extension);
        pj::CallOpParam prm = createCallParams();

        m_currentCall->makeCall(uri, prm);

        std::cout << "[SipClient] Calling: " << uri << std::endl;
        return true;

    } catch (const pj::Error& err) {
        std::cerr << "[SipClient] Call initiation failed: " << err.info() << std::endl;
        m_currentCall.reset();
        return false;
    }
}

/**
 * @brief Hangs up the current active SIP call
 *
 * @details Safely terminates the active call by:
 * 1. Checking if there's an active call
 * 2. Calling PJSIP's hangup() method
 * 3. Resetting the call pointer
 *
 * No-op if no call is active. Errors are logged but don't throw.
 */
void SipClient::hangupCall() {
    if (!m_currentCall) {
        return;
    }

    try {
        pj::CallOpParam prm;
        m_currentCall->hangup(prm);
        m_currentCall.reset();
    } catch (const pj::Error& err) {
        std::cerr << "[SipClient] Hangup error: " << err.info() << std::endl;
        m_currentCall.reset();
    }
}

/**
 * @brief Checks if the SIP account is registered with the server
 *
 * @details Queries the account's registration status from PJSIP.
 * Returns false if account doesn't exist or query fails.
 *
 * @return true if registered and active, false otherwise
 */
bool SipClient::isRegistered() const {
    if (!m_account) {
        return false;
    }

    try {
        return m_account->getInfo().regIsActive;
    } catch (const pj::Error&) {
        return false;
    }
}

/**
 * @brief Builds a SIP URI from a target extension
 *
 * @details Constructs a SIP URI in the format: sip:[target@]server:port
 * - If target is non-empty: sip:target@server:port
 * - If target is empty: sip:server:port (for registrar URI)
 *
 * @param target The extension or user (empty string for registrar URI)
 * @return Complete SIP URI string
 */
std::string SipClient::buildUri(const std::string& target) const {
    std::ostringstream oss;
    oss << "sip:";

    if (!target.empty()) {
        oss << target << "@";
    }

    oss << m_config.server << ":" << m_config.port;
    return oss.str();
}

// ============================================================================
// SipCall
// ============================================================================

/**
 * @brief Checks if the SIP call is in a specific state
 *
 * @details Queries the call info from PJSIP and compares the state.
 * Returns false if the query fails.
 *
 * @param state The PJSIP invite session state to check against
 * @return true if the call is in the specified state, false otherwise
 */
bool SipCall::isState(pjsip_inv_state state) const {
    try {
        return getInfo().state == state;
    } catch (const pj::Error&) {
        return false;
    }
}

/**
 * @brief PJSIP callback for SIP call state changes
 *
 * @details Handles PJSIP call state transitions and updates the shared CallState:
 *
 * - CONFIRMED (call answered): Sets CallState to Answer
 *   This signals other observers to answer the corresponding GSM call
 *
 * - DISCONNECTED (call ended): Sets CallState to Reject
 *   This signals other observers to reject/terminate the corresponding GSM call
 *
 * - Other states: No action
 *
 * This creates bidirectional synchronization between SIP and GSM call states.
 *
 * @param prm PJSIP parameter containing call state information
 */
void SipCall::onCallState(pj::OnCallStateParam& prm) {
    const pj::CallInfo info = getInfo();
    std::cout << "[SipCall] State: " << info.stateText
              << " (status: " << info.lastStatusCode << ")" << std::endl;

    if (!m_state) {
        return;
    }

    switch (info.state) {
        case PJSIP_INV_STATE_CONFIRMED:
            std::cout << "[SipCall] Call answered - accepting GSM call" << std::endl;
            m_state->setState(CallState::State::Answer);
            break;

        case PJSIP_INV_STATE_DISCONNECTED:
            std::cout << "[SipCall] Call disconnected - rejecting GSM call" << std::endl;
            m_state->setState(CallState::State::Reject);
            break;

        default:
            break;
    }
}

/**
 * @brief PJSIP callback for SIP call media state changes
 *
 * @details Called when the media state of the call changes (e.g., media is created).
 * Attempts to connect audio media when active media is detected.
 *
 * @param prm PJSIP parameter containing media state information
 */
void SipCall::onCallMediaState(pj::OnCallMediaStateParam& prm) {
    const pj::CallInfo info = getInfo();

    if (info.media.empty()) {
        return;
    }

    try {
        connectAudio();
    } catch (const pj::Error& err) {
        std::cerr << "[SipCall] Audio error: " << err.info() << std::endl;
    }
}

/**
 * @brief Connects audio media for the active SIP call
 *
 * @details Establishes bidirectional audio streams:
 * 1. From capture device to SIP call media (upload)
 * 2. From SIP call media to playback device (download)
 *
 * Uses NULL audio device which is appropriate for BlueALSA integration,
 * as audio is handled by the BlueALSA system rather than PJSIP.
 *
 * Only connects audio for active media of type AUDIO.
 *
 * @throws pj::Error if audio media connection fails
 */
void SipCall::connectAudio() {
    pj::AudDevManager& audioMgr = pj::Endpoint::instance().audDevManager();
    const pj::CallInfo info = getInfo();

    for (unsigned i = 0; i < info.media.size(); ++i) {
        if (info.media[i].type == PJMEDIA_TYPE_AUDIO &&
            info.media[i].status == PJSUA_CALL_MEDIA_ACTIVE) {

            pj::AudioMedia* media = static_cast<pj::AudioMedia*>(getMedia(i));
            if (media) {
                media->startTransmit(audioMgr.getPlaybackDevMedia());
                audioMgr.getCaptureDevMedia().startTransmit(*media);
            }
        }
    }
}

// ============================================================================
// SipAccount
// ============================================================================

/**
 * @brief PJSIP callback for SIP account registration state changes
 *
 * @details Called when the account registration status changes.
 * Logs whether the account is successfully registered with the SIP server
 * or if registration has failed/expired.
 *
 * Registration states:
 * - Registered: Successfully authenticated with SIP server
 * - Unregistered: Not registered or registration failed
 *
 * @param prm PJSIP parameter containing registration state information
 */
void SipAccount::onRegState(pj::OnRegStateParam& prm) {
    const pj::AccountInfo info = getInfo();

    if (info.regIsActive) {
        std::cout << "[SipAccount] Registered" << std::endl;
    } else {
        std::cout << "[SipAccount] Unregistered" << std::endl;
    }
}
