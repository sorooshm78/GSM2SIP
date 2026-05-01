#include "GsmCallDetector.hpp"
#include <iostream>

/**
 * @brief Initializes the GSM call detector and sets up D-Bus signal handlers
 *
 * @details Performs the following initialization steps:
 * 1. Registers this observer with the shared CallState
 * 2. Creates D-Bus proxy to the oFono modem object
 * 3. Registers CallAdded signal handler to detect new calls
 * 4. Registers CallRemoved signal handler to detect call termination
 *
 * The CallAdded handler extracts:
 * - Call state (incoming, outgoing, active, etc.)
 * - Caller ID (LineIdentification)
 * - Updates CallState with the new information
 *
 * The CallRemoved handler:
 * - Clears the current call path
 * - Sets CallState to Disconnected
 *
 * No-op if modem path is empty (no GSM modem detected).
 */
void GsmCallDetector::initialize() {
    if (m_modemPath.empty()) return;

    observe(m_state.get());

    sdbus::ServiceName ofonoService{"org.ofono"};
    sdbus::ObjectPath ofonoObjectPath{m_modemPath};
    m_ofonoProxy = sdbus::createProxy(m_connection, std::move(ofonoService), std::move(ofonoObjectPath));

    m_ofonoProxy->uponSignal("CallAdded")
        .onInterface("org.ofono.VoiceCallManager")
        .call(
            [this](const sdbus::ObjectPath& callPath,
                   const std::map<std::string, sdbus::Variant>& props) {

                auto stateIt = props.find("State");
                std::string ofonoState = (stateIt != props.end()) ? stateIt->second.get<std::string>() : "";

                auto numIt = props.find("LineIdentification");
                std::string number = (numIt != props.end()) ? numIt->second.get<std::string>() : "";

                m_currentCallPath = callPath;

                CallState::State contextState = ofonoStateToContextState(ofonoState);

                if (contextState == CallState::State::Incoming) {
                    m_state->setFrom(number);
                    m_state->setTo("me");
                } else {
                    m_state->setFrom("me");
                    m_state->setTo(number);
                }

                m_state->setState(contextState);

                std::cout << "[GsmCallDetector] Call added → State: " << ofonoState
                          << "  Number: " << number << std::endl;
            });

    m_ofonoProxy->uponSignal("CallRemoved")
        .onInterface("org.ofono.VoiceCallManager")
        .call([this](const sdbus::ObjectPath& callPath) {
            if (callPath == m_currentCallPath) {
                m_currentCallPath.clear();
                m_state->setState(CallState::State::Disconnected);
                std::cout << "[GsmCallDetector] Call removed" << std::endl;
            }
        });
}

/**
 * @brief Observer callback for GSM call state changes
 *
 * @details Monitors the shared CallState for Reject and Answer states,
 * which are signals from other components (typically SipClient) to
 * control the GSM call.
 *
 * When state is Reject: Calls rejectCall() to terminate the GSM call
 * When state is Answer: Calls answerCall() to accept the GSM call
 * All other states: No action
 *
 * This allows bidirectional control: GSM→SIP (via CallAdded/Removed)
 * and SIP→GSM (via Reject/Answer states).
 *
 * @param state The CallState with updated information
 */
void GsmCallDetector::onCallStateChanged(const CallState& state) {
    switch (state.getState()) {
        case CallState::State::Reject:
            rejectCall();
            break;
        case CallState::State::Answer:
            answerCall();
            break;
        default:
            break;
    }
}

/**
 * @brief Rejects the current active GSM call
 *
 * @details Creates a D-Bus proxy to the active call object and calls
 * the Hangup() method via org.ofono.VoiceCall interface.
 *
 * This method is typically called when the SIP call fails or is declined,
 * and we want to reject the corresponding GSM call.
 *
 * Errors are logged to stderr but don't throw exceptions.
 *
 * @note No-op if there's no active call path (i.e., no call in progress)
 */
void GsmCallDetector::rejectCall() {
    if (m_currentCallPath.empty()) {
        std::cerr << "[GsmCallDetector] Cannot reject: no active call path" << std::endl;
        return;
    }

    try {
        sdbus::ServiceName ofonoService{"org.ofono"};
        sdbus::ObjectPath callObjectPath{m_currentCallPath};
        auto callProxy = sdbus::createProxy(m_connection, std::move(ofonoService), std::move(callObjectPath));

        callProxy->callMethod("Hangup")
            .onInterface("org.ofono.VoiceCall")
            .dontExpectReply();

        std::cout << "[GsmCallDetector] Call rejected" << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "[GsmCallDetector] Failed to reject call: " << ex.what() << std::endl;
    }
}

/**
 * @brief Answers the current incoming GSM call
 *
 * @details Creates a D-Bus proxy to the active call object and calls
 * the Answer() method via org.ofono.VoiceCall interface.
 *
 * This method is typically called when the SIP call is answered,
 * and we want to answer the corresponding GSM call.
 *
 * Errors are logged to stderr but don't throw exceptions.
 *
 * @note No-op if there's no active call path (i.e., no incoming call)
 */
void GsmCallDetector::answerCall() {
    if (m_currentCallPath.empty()) {
        std::cerr << "[GsmCallDetector] Cannot answer: no active call path" << std::endl;
        return;
    }

    try {
        sdbus::ServiceName ofonoService{"org.ofono"};
        sdbus::ObjectPath callObjectPath{m_currentCallPath};
        auto callProxy = sdbus::createProxy(m_connection, std::move(ofonoService), std::move(callObjectPath));

        callProxy->callMethod("Answer")
            .onInterface("org.ofono.VoiceCall")
            .dontExpectReply();

        std::cout << "[GsmCallDetector] Call answered" << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "[GsmCallDetector] Failed to answer call: " << ex.what() << std::endl;
    }
}

/**
 * @brief Converts oFono state strings to CallState::State enum values
 *
 * @details Maps oFono's string-based call states to our internal State enum.
 * oFono uses string states like "incoming", "active", "disconnected".
 *
 * Supported mappings:
 * - "incoming" → State::Incoming
 * - "outgoing" → State::Outgoing
 * - "active" → State::Active
 * - "alerting" → State::Alerting
 * - "dialing" → State::Dialing
 * - "disconnecting" → State::Disconnecting
 * - "disconnected" → State::Disconnected
 * - (unrecognized) → State::Unknown
 *
 * @param ofonoState The oFono state string from CallAdded signal
 * @return Corresponding CallState::State enum value
 */
CallState::State GsmCallDetector::ofonoStateToContextState(const std::string& ofonoState) {
    if (ofonoState == "incoming") return CallState::State::Incoming;
    if (ofonoState == "outgoing") return CallState::State::Outgoing;
    if (ofonoState == "active") return CallState::State::Active;
    if (ofonoState == "alerting") return CallState::State::Alerting;
    if (ofonoState == "dialing") return CallState::State::Dialing;
    if (ofonoState == "disconnecting") return CallState::State::Disconnecting;
    if (ofonoState == "disconnected") return CallState::State::Disconnected;
    return CallState::State::Unknown;
}
