#include "CallState.hpp"
#include <algorithm>

/**
 * @brief Notifies all registered observers of the current state
 *
 * @details Iterates through all registered observers and calls their
 * onCallStateChanged() method with a reference to this CallState.
 * This method is private and only called by setState() while holding
 * the state mutex to ensure thread-safe notification delivery.
 *
 * Thread safety: Must be called while holding m_stateMutex
 */
void CallState::notifyObservers() {
    for (auto* observer : m_observers) {
        observer->onCallStateChanged(*this);
    }
}
