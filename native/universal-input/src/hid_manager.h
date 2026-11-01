// Owns the mapped (non-XInput) HID guitars and hands them to free XInput slots.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "hid_device.h"
#include "mapping.h"
#include "xinput_types.h"

namespace uinput {

class HidManager {
public:
    // Starts a background thread that opens/closes devices matching the profiles.
    void Start(std::vector<Profile> profiles);

    // Called for a slot with no real XInput controller. If a mapped HID guitar is (or can
    // be) assigned to this slot, fills *state and returns true.
    bool Query(DWORD slot, XState* state);
    // A real XInput controller occupies this slot: move any virtual guitar out of it.
    void ReleaseSlot(DWORD slot);
    bool HasProfileFor(USHORT vid, USHORT pid) const;
    std::wstring NameOf(DWORD slot) const;

private:
    struct Entry {
        std::unique_ptr<HidDevice> device;
        const Profile* profile = nullptr;
        int slot = -1;
    };
    static DWORD WINAPI ThreadMain(LPVOID self);
    void Rescan();

    std::vector<Profile> profiles_;
    std::vector<Entry> entries_;
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
};

}  // namespace uinput
