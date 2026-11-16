#pragma once

struct UpdateRestartState {
    bool inBackground = false;
    long long backgroundMs = 0;
    bool editing = false;
    bool busy = false;
    bool cloudSavePending = false;
};

inline bool canRestartForUpdate(const UpdateRestartState& state) {
    constexpr auto quietMs = 2 * 60 * 1000;
    return state.inBackground && state.backgroundMs >= quietMs
        && !state.editing && !state.busy && !state.cloudSavePending;
}
