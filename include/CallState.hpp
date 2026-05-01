#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <mutex>

/**
 * @brief Central context class that holds the current call state and information
 *
 * @details Acts as the Subject in the Observer pattern. When the call state changes,
 * all registered observers are notified via onCallStateChanged(). This class maintains
 * the shared state between GSM call detection and SIP call management.
 *
 * Thread safety: Thread-safe state updates via mutex. Observer notifications
 * occur while holding the lock to ensure consistency. All state modifications
 * should go through setState() to ensure proper observer notification.
 *
 * State management:
 * - Stores current call state (incoming, outgoing, active, etc.)
 * - Tracks caller and callee information
 * - Notifies all observers on state changes
 * - Provides string representation for debugging
 */
class CallState {
public:
    /**
     * @brief Enumeration of all possible states of a GSM/SIP call
     *
     * @details Defines the complete state machine for call lifecycle management.
     * State transitions follow the oFono and SIP call lifecycles:
     *
     * Incoming call flow: Incoming → (Reject/Answer) → Active → Disconnected
     * Outgoing call flow: Outgoing → Dialing → Alerting → Active → Disconnected
     *
     * Special states:
     * - Reject: Signal to reject the current call
     * - Answer: Signal to answer the current incoming call
     */
    enum class State {
        Unknown,       // Initial state before any call detected
        Incoming,      // Incoming call from remote party
        Outgoing,      // Outgoing call initiated by us
        Active,        // Call is connected and ongoing
        Alerting,      // Remote party is being alerted (ringing)
        Dialing,       // Number is being dialed
        Disconnecting, // Call termination in progress
        Disconnected,  // Call has ended
        Reject,        // Request to reject incoming call
        Answer         // Request to answer incoming call
    };

    CallState() = default;

    /**
     * @brief Sets the call state and automatically notifies all observers
     *
     * @details Updates the internal state and triggers notification to all registered
     * observers. Observers receive the updated CallState with the new state.
     *
     * Thread-safe: Uses mutex for synchronization. The lock is held during
     * state update and observer notification to ensure consistency.
     *
     * @param state The new call state to set
     */
    void setState(State state) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_state = state;
        notifyObservers();
    }

    /**
     * @brief Gets the current call state
     *
     * @details Returns the current state. Thread-safe (const method).
     *
     * @return Current call state
     */
    State getState() const { return m_state; }

    /**
     * @brief Sets the caller/callee identification information
     *
     * @details Sets the calling party information.
     * For incoming calls: from=caller ID, to="me"
     * For outgoing calls: from="me", to=callee number
     *
     * @param from The caller ID or "me" for outgoing calls
     */
    void setFrom(const std::string& from) { m_from = from; }

    /**
     * @brief Gets the caller information
     *
     * @return Reference to the caller ID string
     */
    const std::string& getFrom() const { return m_from; }

    /**
     * @brief Sets the callee/target identification information
     *
     * @details Sets the called party information.
     * For incoming calls: to="me"
     * For outgoing calls: to=callee number
     *
     * @param to The callee number or "me" for incoming calls
     */
    void setTo(const std::string& to) { m_to = to; }

    /**
     * @brief Gets the callee information
     *
     * @return Reference to the callee string
     */
    const std::string& getTo() const { return m_to; }

    /**
     * @brief Converts State enum to human-readable string representation
     *
     * @details Provides string equivalents for all state enum values.
     * Useful for logging, debugging, and display purposes.
     *
     * @param state The state enum to convert
     * @return String representation of the state (e.g., "incoming", "active")
     */
    static std::string stateToString(State state) {
        switch (state) {
            case State::Unknown: return "unknown";
            case State::Incoming: return "incoming";
            case State::Outgoing: return "outgoing";
            case State::Active: return "active";
            case State::Alerting: return "alerting";
            case State::Dialing: return "dialing";
            case State::Disconnecting: return "disconnecting";
            case State::Disconnected: return "disconnected";
            case State::Reject: return "reject";
            case State::Answer: return "answer";
            default: return "invalid";
        }
    }

private:
    /**
     * @brief Notifies all registered observers of state changes
     *
     * @details Calls onCallStateChanged() on each registered observer.
     * Must be called while holding the state mutex to ensure consistency.
     * This method is private and only called by setState().
     */
    void notifyObservers();

    mutable std::mutex m_stateMutex; ///< Mutex for thread-safe state access
    State m_state = State::Unknown;  ///< Current call state
    std::string m_from;              ///< Caller ID or "me"
    std::string m_to;                ///< Callee ID or "me"

    /**
     * @brief Vector of registered observer pointers
     *
     * @details Uses raw pointers because observers manage their own lifecycle
     * and call observe()/stopObserving() in their constructors/destructors.
     * This design prevents dangling pointers and memory leaks.
     */
    std::vector<class ICallObserver*> m_observers;

    /**
     * @brief Grants ICallObserver access to m_observers for registration
     *
     * @details ICallObserver needs access to m_observers to register/unregister itself.
     * This encapsulates the observer management logic within the observer base class.
     */
    friend class ICallObserver;
};

/**
 * @brief Abstract base class for call state observers
 *
 * @details Implements the Observer pattern for call state change notifications.
 * Derived classes must implement onCallStateChanged() to react to call state changes.
 *
 * Usage pattern:
 * 1. Inherit from ICallObserver in your class
 * 2. Call observe(context) in your constructor to start receiving notifications
 * 3. Call stopObserving(context) in your destructor to stop notifications
 * 4. Implement onCallStateChanged() to handle state changes
 *
 * Thread safety: Observer registration/unregistration is thread-safe via
 * the mutex in CallState. Notifications are delivered while holding the lock.
 *
 * Example:
 * @code
 * class MyObserver : public ICallObserver {
 * public:
 *     MyObserver(std::shared_ptr<CallState> state) {
 *         observe(state.get());
 *     }
 *     ~MyObserver() {
 *         stopObserving(state.get());
 *     }
 *     void onCallStateChanged(const CallState& ctx) override {
 *         // Handle state change
 *     }
 * };
 * @endcode
 */
class ICallObserver {
public:
    virtual ~ICallObserver() = default;

    /**
     * @brief Pure virtual callback for call state changes
     *
     * @details Called by CallState when the call state changes.
     * Derived classes must implement this method to react to state transitions.
     * Receives a const reference to the CallState with the new state.
     *
     * @param ctx The CallState with updated state and caller/callee info
     */
    virtual void onCallStateChanged(const CallState& ctx) = 0;

protected:
    /**
     * @brief Registers this observer to receive notifications from the given context
     *
     * @details Adds this observer to the context's observer list.
     * Must be called in the derived class constructor to start receiving notifications.
     *
     * @param context Pointer to the CallState to observe (can be null, safe check included)
     */
    void observe(CallState* context) {
        if (context) {
            context->m_observers.push_back(this);
        }
    }

    /**
     * @brief Unregisters this observer from the given context
     *
     * @details Removes this observer from the context's observer list.
     * Must be called in the derived class destructor to prevent dangling pointers.
     * Uses the remove-erase idiom for safe removal from vector.
     *
     * @param context Pointer to the CallState to stop observing (can be null, safe check included)
     */
    void stopObserving(CallState* context) {
        if (context) {
            auto& observers = context->m_observers;
            // Remove-erase idiom for safe removal from vector
            auto it = std::remove(observers.begin(), observers.end(), this);
            observers.erase(it, observers.end());
        }
    }
};
