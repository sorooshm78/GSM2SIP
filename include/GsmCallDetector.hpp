#pragma once
#include "CallState.hpp"
#include <sdbus-c++/sdbus-c++.h>
#include <memory>
#include <map>

/**
 * @brief Detects GSM call events by monitoring oFono D-Bus signals
 *
 * @details Listens to CallAdded and CallRemoved signals from oFono's VoiceCallManager
 * interface and updates the shared CallState accordingly. Implements both detection
 * and control functionality (answer, reject) for GSM calls.
 *
 * Integration flow:
 * 1. Monitors oFono D-Bus for call events
 * 2. Updates CallState when calls are added/removed
 * 3. Responds to Reject/Answer states from other observers by controlling the GSM call
 *
 * D-Bus signals monitored:
 * - org.ofono.VoiceCallManager.CallAdded(callPath, properties)
 *   Provides: call state (incoming, active, etc.) and caller ID
 * - org.ofono.VoiceCallManager.CallRemoved(callPath)
 *   Indicates call termination
 *
 * D-Bus methods called:
 * - org.ofono.VoiceCall.Answer() - Answers incoming call
 * - org.ofono.VoiceCall.Hangup() - Rejects/terminates call
 *
 * Important implementation notes:
 * - The oFono proxy MUST be kept alive as a member variable (not local) because
 *   signal handlers are destroyed when the proxy is destroyed
 * - Signal handlers are lambda functions that capture 'this'
 * - Thread-safe: D-Bus calls are synchronous and block until complete
 */
class GsmCallDetector : public ICallObserver {
public:
    /**
     * @brief Constructs a GSM call detector for a specific modem
     *
     * @details Creates a detector that monitors call events on the specified oFono modem.
     * The modem path is typically derived from the BlueALSA PCM path.
     *
     * @param state Shared call state to update with call state changes
     * @param connection D-Bus system bus connection for signal monitoring
     * @param modemPath oFono modem object path (e.g., /hfp/org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX)
     */
    GsmCallDetector(std::shared_ptr<CallState> state,
                    sdbus::IConnection& connection,
                    const std::string& modemPath)
        : m_state(state),
          m_connection(connection),
          m_modemPath(modemPath) {}

    /**
     * @brief Sets up D-Bus signal handlers for call detection
     *
     * @details Performs the following initialization:
     * 1. Registers this observer with the CallState
     * 2. Creates D-Bus proxy to oFono modem object
     * 3. Registers signal handler for CallAdded events
     * 4. Registers signal handler for CallRemoved events
     *
     * Must be called after construction to start detecting calls.
     */
    void initialize();

    /**
     * @brief Rejects the current active call via oFono
     *
     * @details Calls the Hangup() method on the active call object via D-Bus.
     * This method is triggered when the CallState is set to Reject.
     *
     * No-op if there's no active call path (i.e., no call in progress).
     */
    void rejectCall();

    /**
     * @brief Answers the current incoming call via oFono
     *
     * @details Calls the Answer() method on the active call object via D-Bus.
     * This method is triggered when the CallState is set to Answer.
     *
     * No-op if there's no active call path (i.e., no incoming call).
     */
    void answerCall();

    /**
     * @brief Observer callback for call state changes
     *
     * @details Checks for Reject/Answer states and triggers corresponding actions.
     * Other observers can request rejection or answering by setting the CallState
     * to Reject or Answer respectively.
     *
     * @param state The CallState with updated information
     */
    void onCallStateChanged(const CallState& state) override;

    /**
     * @brief Gets the D-Bus object path of the current active call
     *
     * @details Returns the path of the call object being monitored.
     * Updated when CallAdded signal is received, cleared on CallRemoved.
     *
     * @return Reference to the current call path string (empty if no active call)
     */
    const std::string& getCurrentCallPath() const { return m_currentCallPath; }

private:
    std::shared_ptr<CallState> m_state;   ///< Shared call state for integration with other components
    sdbus::IConnection& m_connection;     ///< D-Bus system bus connection
    std::string m_modemPath;              ///< oFono modem object path

    /**
     * @brief D-Bus object path of the current active call
     *
     * @details Updated when CallAdded signal is received, cleared on CallRemoved.
     * Used to identify which call to operate on (reject, answer, etc.).
     */
    std::string m_currentCallPath;

    /**
     * @brief D-Bus proxy to the oFono modem object
     *
     * @details MUST be a member variable (not local in initialize()) because the
     * signal handlers are tied to the proxy's lifetime. If the proxy is destroyed,
     * signals stop being delivered.
     */
    std::unique_ptr<sdbus::IProxy> m_ofonoProxy;

    /**
     * @brief Converts oFono state strings to CallState::State enum
     *
     * @details oFono uses string states like "incoming", "active", "disconnected".
     * This method maps those strings to our internal State enum values.
     *
     * @param ofonoState oFono state string from CallAdded signal
     * @return Corresponding CallState::State, or Unknown if unrecognized
     */
    static CallState::State ofonoStateToContextState(const std::string& ofonoState);
};
