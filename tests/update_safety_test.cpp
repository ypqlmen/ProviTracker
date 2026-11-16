#ifdef NDEBUG
#undef NDEBUG
#endif

#include "../updater_safety.h"
#include <cassert>

int main() {
    UpdateRestartState state{true, 119999, false, false, false};
    assert(!canRestartForUpdate(state)); // A brief trip to another window is not enough.
    state.backgroundMs = 120000;
    assert(canRestartForUpdate(state));
    state.inBackground = false;
    assert(!canRestartForUpdate(state)); // The user returned while the helper was preparing.
    state.inBackground = true;
    state.editing = true;
    assert(!canRestartForUpdate(state));
    state.editing = false;
    state.busy = true;
    assert(!canRestartForUpdate(state));
    state.busy = false;
    state.cloudSavePending = true;
    assert(!canRestartForUpdate(state)); // Includes failed saves awaiting retry.
    state.cloudSavePending = false;
    assert(canRestartForUpdate(state));
}
