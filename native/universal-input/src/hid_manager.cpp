#include "hid_manager.h"

#include "log.h"

namespace uinput {

void HidManager::Start(std::vector<Profile> profiles) {
    profiles_ = std::move(profiles);
    for (const Profile& p : profiles_)
        Log("profile %04X:%04X \"%ls\" loaded", p.vid, p.pid, p.name.c_str());
    if (profiles_.empty()) return;  // nothing to look for
    Rescan();
    HANDLE t = CreateThread(nullptr, 0, &HidManager::ThreadMain, this, 0, nullptr);
    if (t) CloseHandle(t);  // runs for the life of the process
}

DWORD WINAPI HidManager::ThreadMain(LPVOID self) {
    for (;;) {
        Sleep(2000);
        static_cast<HidManager*>(self)->Rescan();
    }
}

void HidManager::Rescan() {
    // Drop devices that went away.
    std::vector<std::unique_ptr<HidDevice>> dead;
    AcquireSRWLockExclusive(&lock_);
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!it->device->Alive()) {
            Log("HID guitar %04X:%04X removed (slot %d)", it->device->Info().vid, it->device->Info().pid, it->slot + 1);
            dead.push_back(std::move(it->device));
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
    std::vector<std::wstring> openPaths;
    for (const Entry& e : entries_) openPaths.push_back(e.device->Info().path);
    ReleaseSRWLockExclusive(&lock_);
    dead.clear();  // joins reader threads outside the lock

    for (const HidDeviceInfo& info : EnumerateHidGameControllers()) {
        if (info.xinputShadow) continue;
        const Profile* profile = nullptr;
        for (const Profile& p : profiles_)
            if (p.vid == info.vid && p.pid == info.pid) profile = &p;
        if (!profile) continue;
        bool open = false;
        for (const auto& path : openPaths) open |= _wcsicmp(path.c_str(), info.path.c_str()) == 0;
        if (open) continue;

        auto device = HidDevice::Open(info);
        if (!device) continue;
        Log("HID guitar %04X:%04X \"%ls\" opened with profile \"%ls\"", info.vid, info.pid, info.product.c_str(),
            profile->name.c_str());
        AcquireSRWLockExclusive(&lock_);
        entries_.push_back({std::move(device), profile, -1});
        ReleaseSRWLockExclusive(&lock_);
    }
}

bool HidManager::Query(DWORD slot, XState* state) {
    AcquireSRWLockExclusive(&lock_);
    Entry* found = nullptr;
    for (Entry& e : entries_)
        if (e.slot == static_cast<int>(slot) && e.device->Alive()) found = &e;
    if (!found) {
        for (Entry& e : entries_) {
            if (e.slot == -1 && e.device->Alive()) {
                e.slot = static_cast<int>(slot);
                found = &e;
                Log("HID guitar %04X:%04X assigned to slot %lu", e.device->Info().vid, e.device->Info().pid, slot + 1);
                break;
            }
        }
    }
    if (found && state) {
        const HidSnapshot snap = found->device->Snapshot();
        state->dwPacketNumber = snap.sequence;
        state->Gamepad = MapToGuitar(*found->profile, snap, found->device->Values());
    }
    ReleaseSRWLockExclusive(&lock_);
    return found != nullptr;
}

void HidManager::ReleaseSlot(DWORD slot) {
    AcquireSRWLockExclusive(&lock_);
    for (Entry& e : entries_) {
        if (e.slot == static_cast<int>(slot)) {
            Log("slot %lu taken by a real XInput controller; HID guitar %04X:%04X moves to the next free slot", slot + 1,
                e.device->Info().vid, e.device->Info().pid);
            e.slot = -1;
        }
    }
    ReleaseSRWLockExclusive(&lock_);
}

bool HidManager::HasProfileFor(USHORT vid, USHORT pid) const {
    for (const Profile& p : profiles_)
        if (p.vid == vid && p.pid == pid) return true;
    return false;
}

std::wstring HidManager::NameOf(DWORD slot) const {
    AcquireSRWLockShared(&lock_);
    std::wstring name;
    for (const Entry& e : entries_)
        if (e.slot == static_cast<int>(slot)) name = e.profile->name;
    ReleaseSRWLockShared(&lock_);
    return name;
}

}  // namespace uinput
