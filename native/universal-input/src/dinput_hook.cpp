#include "dinput_hook.h"

#include <cstring>

#include "log.h"

namespace uinput {
namespace {

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
using EnumCallback = BOOL(CALLBACK*)(const void* deviceInstance, LPVOID ref);
using EnumDevicesFn = HRESULT(STDMETHODCALLTYPE*)(void* self, DWORD devType, EnumCallback cb, LPVOID ref, DWORD flags);

// Leading members shared by DIDEVICEINSTANCEA and DIDEVICEINSTANCEW.
struct DeviceInstanceHead {
    DWORD dwSize;
    GUID guidInstance;
    GUID guidProduct;  // Data1 == MAKELONG(vendor id, product id) for HID devices
    DWORD dwDevType;
};
constexpr size_t kInstanceNameOffset = sizeof(DeviceInstanceHead);
constexpr size_t kProductNameOffsetA = kInstanceNameOffset + MAX_PATH;
constexpr size_t kProductNameOffsetW = kInstanceNameOffset + MAX_PATH * sizeof(wchar_t);
constexpr int kEnumDevicesVtableIndex = 4;  // QueryInterface, AddRef, Release, CreateDevice, EnumDevices

constexpr BOOL kDiEnumContinue = TRUE;  // DIENUM_CONTINUE

HideDevicePredicate g_shouldHide = nullptr;
void (*g_onFirstUse)() = nullptr;
DirectInput8CreateFn g_realCreate = nullptr;

// IDirectInput8A and IDirectInput8W have separate vtables; remember the original per vtable.
struct PatchedVtable {
    void** vtable;
    EnumDevicesFn original;
};
PatchedVtable g_patched[2] = {};
SRWLOCK g_patchLock = SRWLOCK_INIT;

EnumDevicesFn OriginalFor(void* self) {
    void** vtable = *static_cast<void***>(self);
    for (const auto& p : g_patched)
        if (p.vtable == vtable) return p.original;
    return nullptr;
}

struct EnumContext {
    EnumCallback callback;
    LPVOID ref;
};

BOOL CALLBACK FilteringCallback(const void* instance, LPVOID ref) {
    auto* ctx = static_cast<EnumContext*>(ref);
    auto* head = static_cast<const DeviceInstanceHead*>(instance);
    const USHORT vid = LOWORD(head->guidProduct.Data1);
    const USHORT pid = HIWORD(head->guidProduct.Data1);

    char name[MAX_PATH] = {};
    // DIDEVICEINSTANCEA is 580 bytes, DIDEVICEINSTANCEW 1100: anything past both name arrays of
    // the wide layout must be the wide struct.
    const bool wide = head->dwSize >= kProductNameOffsetW + MAX_PATH * sizeof(wchar_t);
    if (wide) {
        WideCharToMultiByte(CP_UTF8, 0,
                            reinterpret_cast<const wchar_t*>(static_cast<const BYTE*>(instance) + kProductNameOffsetW),
                            -1, name, MAX_PATH - 1, nullptr, nullptr);
    } else {
        strncpy_s(name, reinterpret_cast<const char*>(static_cast<const BYTE*>(instance) + kProductNameOffsetA), _TRUNCATE);
    }
    const bool hide = g_shouldHide && g_shouldHide(vid, pid, name);
    Log("DirectInput device %04X:%04X \"%s\"%s", vid, pid, name, hide ? " -> hidden (mapped as an XInput guitar)" : "");
    return hide ? kDiEnumContinue : ctx->callback(instance, ctx->ref);
}

HRESULT STDMETHODCALLTYPE HookedEnumDevices(void* self, DWORD devType, EnumCallback cb, LPVOID ref, DWORD flags) {
    EnumDevicesFn original = OriginalFor(self);
    if (!original) return E_FAIL;
    if (!cb) return original(self, devType, cb, ref, flags);
    EnumContext ctx = {cb, ref};
    return original(self, devType, &FilteringCallback, &ctx, flags);
}

void PatchInterface(void* directInput) {
    void** vtable = *static_cast<void***>(directInput);
    AcquireSRWLockExclusive(&g_patchLock);
    bool done = false;
    for (auto& p : g_patched) done |= p.vtable == vtable;
    for (auto& p : g_patched) {
        if (done || p.vtable) continue;
        DWORD old = 0;
        if (VirtualProtect(&vtable[kEnumDevicesVtableIndex], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
            p.vtable = vtable;
            p.original = reinterpret_cast<EnumDevicesFn>(vtable[kEnumDevicesVtableIndex]);
            vtable[kEnumDevicesVtableIndex] = reinterpret_cast<void*>(&HookedEnumDevices);
            VirtualProtect(&vtable[kEnumDevicesVtableIndex], sizeof(void*), old, &old);
            Log("DirectInput EnumDevices hooked");
        }
        done = true;
    }
    ReleaseSRWLockExclusive(&g_patchLock);
}

HRESULT WINAPI HookedDirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, void* outer) {
    if (g_onFirstUse) g_onFirstUse();  // outside DllMain now, so full initialisation is safe
    HRESULT hr = g_realCreate(inst, version, riid, out, outer);
    if (SUCCEEDED(hr) && out && *out) PatchInterface(*out);
    return hr;
}

}  // namespace

bool InstallDirectInputHook(HideDevicePredicate shouldHide, void (*onFirstUse)()) {
    g_shouldHide = shouldHide;
    g_onFirstUse = onFirstUse;
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;

    HMODULE dinput = GetModuleHandleW(L"dinput8.dll");
    FARPROC realByName = dinput ? GetProcAddress(dinput, "DirectInput8Create") : nullptr;

    for (auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), "dinput8.dll") != 0) continue;
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        auto* names = desc->OriginalFirstThunk ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk) : nullptr;
        for (size_t i = 0; iat[i].u1.Function; ++i) {
            bool match = false;
            if (names && !IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) {
                auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
                match = strcmp(reinterpret_cast<const char*>(byName->Name), "DirectInput8Create") == 0;
            } else if (realByName) {
                match = iat[i].u1.Function == reinterpret_cast<ULONG_PTR>(realByName);
            }
            if (!match) continue;
            DWORD old = 0;
            if (!VirtualProtect(&iat[i].u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE, &old)) return false;
            g_realCreate = reinterpret_cast<DirectInput8CreateFn>(iat[i].u1.Function);
            iat[i].u1.Function = reinterpret_cast<ULONG_PTR>(&HookedDirectInput8Create);
            VirtualProtect(&iat[i].u1.Function, sizeof(ULONG_PTR), old, &old);
            return true;
        }
    }
    return false;
}

}  // namespace uinput
