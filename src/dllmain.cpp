// FF7RemakeFpsUnlock - unlock the 120 FPS cap of FINAL FANTASY VII REMAKE INTERGRADE.
// MIT License. Not affiliated with Square Enix.

#include <windows.h>
#include <xinput.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char* kVersion = "1.0.0";
constexpr const wchar_t* kIniName = L"FF7RFpsUnlock.ini";
constexpr const wchar_t* kLogName = L"FF7RFpsUnlock.log";

// The code block that applies the in-game "120" framerate option.
//   movss xmm1, [120.0f]
//   mov   r8d, 0x01000000        ; ECVF_SetByScalability
//   mov   rcx, [t.MaxFPS]
//   call  Set
//   mov   edx, 120               ; RHI.TargetRefreshRate
//   mov   rcx, [RHI.TargetRefreshRate]
//   mov   r8d, 0x01000000
//   call  Set
constexpr const char* kFpsSignature =
    "F3 0F 10 0D ?? ?? ?? ?? 41 B8 00 00 00 01 48 8B 0D ?? ?? ?? ?? "
    "E8 ?? ?? ?? ?? BA 78 00 00 00 48 8B 0D ?? ?? ?? ?? 41 B8 00 00 00 01 E8";

// UGameEngine::GetMaxTickRate: the smoothed frame rate range clamp.
//   mov rax, [rcx]
//   call qword ptr [rax + 0x298]
//   test al, al
//   je   skip_smoothing
constexpr const char* kSmoothingSignature =
    "48 8B 01 FF 90 98 02 00 00 84 C0 74 ??";

HMODULE g_self = nullptr;

// ---------------------------------------------------------------------------
// Paths / logging
// ---------------------------------------------------------------------------

std::wstring SelfPath() {
  wchar_t buf[MAX_PATH]{};
  DWORD len = GetModuleFileNameW(g_self, buf, MAX_PATH);
  return std::wstring(buf, len);
}

std::wstring SiblingFile(const wchar_t* name) {
  std::wstring path = SelfPath();
  size_t pos = path.find_last_of(L"\\/");
  if (pos == std::wstring::npos) {
    return std::wstring(name);
  }
  return path.substr(0, pos + 1) + name;
}

void Log(const char* fmt, ...) {
  char line[1024]{};
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);

  SYSTEMTIME st{};
  GetLocalTime(&st);
  char stamp[64]{};
  sprintf_s(stamp, "[%04u-%02u-%02u %02u:%02u:%02u] ", st.wYear, st.wMonth,
            st.wDay, st.wHour, st.wMinute, st.wSecond);

  std::wstring path = SiblingFile(kLogName);
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD written = 0;
  WriteFile(file, stamp, (DWORD)strlen(stamp), &written, nullptr);
  WriteFile(file, line, (DWORD)strlen(line), &written, nullptr);
  WriteFile(file, "\r\n", 2, &written, nullptr);
  CloseHandle(file);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

int ReadIniInt(const wchar_t* key, int fallback) {
  std::wstring ini = SiblingFile(kIniName);
  return (int)GetPrivateProfileIntW(L"Settings", key, fallback, ini.c_str());
}

// ---------------------------------------------------------------------------
// PE helpers / pattern scanning
// ---------------------------------------------------------------------------

struct BytePattern {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> mask;  // 1 = must match, 0 = wildcard
};

bool ParsePattern(const char* text, BytePattern& out) {
  out.bytes.clear();
  out.mask.clear();
  const char* p = text;
  while (*p) {
    while (*p == ' ') {
      ++p;
    }
    if (!*p) {
      break;
    }
    if (p[0] == '?' && p[1] == '?') {
      out.bytes.push_back(0);
      out.mask.push_back(0);
      p += 2;
      continue;
    }
    unsigned value = 0;
    for (int i = 0; i < 2; ++i) {
      char c = p[i];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= (unsigned)(c - '0');
      } else if (c >= 'A' && c <= 'F') {
        value |= (unsigned)(c - 'A' + 10);
      } else if (c >= 'a' && c <= 'f') {
        value |= (unsigned)(c - 'a' + 10);
      } else {
        return false;
      }
    }
    out.bytes.push_back((uint8_t)value);
    out.mask.push_back(1);
    p += 2;
  }
  return !out.bytes.empty();
}

uint8_t* FindSection(HMODULE module, const char* name, size_t* size) {
  auto* dos = (const IMAGE_DOS_HEADER*)module;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    return nullptr;
  }
  auto* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)module + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    return nullptr;
  }
  auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
    if (memcmp(section->Name, name, strlen(name)) == 0) {
      *size = section->Misc.VirtualSize;
      return (uint8_t*)module + section->VirtualAddress;
    }
  }
  return nullptr;
}

uint8_t* FindPattern(const uint8_t* begin, size_t size, const BytePattern& pattern) {
  const size_t n = pattern.bytes.size();
  if (n == 0 || n > size) {
    return nullptr;
  }
  const uint8_t first = pattern.bytes[0];
  const uint8_t* last = begin + (size - n);
  const uint8_t* cursor = begin;
  while (cursor <= last) {
    const uint8_t* hit =
        (const uint8_t*)memchr(cursor, first, (size_t)(last - cursor) + 1);
    if (!hit) {
      return nullptr;
    }
    size_t i = 1;
    for (; i < n; ++i) {
      if (pattern.mask[i] && hit[i] != pattern.bytes[i]) {
        break;
      }
    }
    if (i == n) {
      return (uint8_t*)hit;
    }
    cursor = hit + 1;
  }
  return nullptr;
}

bool WriteBytes(void* address, const void* data, size_t size) {
  DWORD old_protect = 0;
  if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
    return false;
  }
  memcpy(address, data, size);
  DWORD unused = 0;
  VirtualProtect(address, size, old_protect, &unused);
  FlushInstructionCache(GetCurrentProcess(), address, size);
  return true;
}

uint8_t* AllocateNear(uint8_t* site, size_t size) {
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  const uintptr_t granularity =
      info.dwAllocationGranularity ? info.dwAllocationGranularity : 0x10000;
  const uintptr_t site_addr = (uintptr_t)site;
  const uintptr_t reach = 0x70000000ull;
  uintptr_t low = site_addr > reach ? site_addr - reach : granularity;
  uintptr_t high = site_addr + reach;
  for (uintptr_t address = low; address < high; address += granularity) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) != sizeof(mbi)) {
      continue;
    }
    if (mbi.State != MEM_FREE) {
      continue;
    }
    void* p = VirtualAlloc((LPVOID)address, size, MEM_RESERVE | MEM_COMMIT,
                           PAGE_EXECUTE_READWRITE);
    if (p) {
      return (uint8_t*)p;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Game patches
// ---------------------------------------------------------------------------

void ApplyFpsPatch(uint8_t* site, int fps) {
  // Sanity check: the movss must load 120.0f, otherwise the signature matched
  // some unrelated code and we bail out.
  int32_t displacement = 0;
  memcpy(&displacement, site + 4, sizeof(displacement));
  const float* constant = (const float*)(site + 8 + displacement);
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(constant, &mbi, sizeof(mbi)) != sizeof(mbi) ||
      mbi.State != MEM_COMMIT || mbi.RegionSize < sizeof(float)) {
    Log("error: framerate constant is not readable, patch aborted");
    return;
  }
  if (*constant != 120.0f) {
    Log("error: framerate constant is %.1f, expected 120.0, patch aborted",
        (double)*constant);
    return;
  }

  bool fps_patched = false;
  if (fps <= 0) {
    // xorps xmm1, xmm1 ; nop * 5  -> t.MaxFPS = 0 (uncapped)
    const uint8_t kUncapped[8] = {0x0F, 0x57, 0xC9, 0x90,
                                  0x90, 0x90, 0x90, 0x90};
    fps_patched = WriteBytes(site, kUncapped, sizeof(kUncapped));
    Log(fps_patched ? "t.MaxFPS patched to 0 (uncapped)"
                    : "error: failed to patch t.MaxFPS");
  } else {
    // movss xmm1, [rip+0] ; float value ; jmp back
    uint8_t* cave = AllocateNear(site, 64);
    if (!cave) {
      Log("error: failed to allocate trampoline near %p", site);
    } else {
      const uint8_t kMovss[8] = {0xF3, 0x0F, 0x10, 0x0D, 0x00, 0x00, 0x00, 0x00};
      memcpy(cave, kMovss, sizeof(kMovss));
      const float value = (float)fps;
      memcpy(cave + 8, &value, sizeof(value));
      const int64_t back = (int64_t)(site + 8) - (int64_t)(cave + 17);
      if (back < INT32_MIN || back > INT32_MAX) {
        Log("error: trampoline is too far from the patch site");
      } else {
        cave[12] = 0xE9;
        int32_t back32 = (int32_t)back;
        memcpy(cave + 13, &back32, sizeof(back32));
        FlushInstructionCache(GetCurrentProcess(), cave, 17);

        uint8_t jump[8] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
        int32_t forward = (int32_t)((int64_t)cave - (int64_t)(site + 5));
        memcpy(jump + 1, &forward, sizeof(forward));
        fps_patched = WriteBytes(site, jump, sizeof(jump));
        Log(fps_patched ? "t.MaxFPS patched to %d" : "error: failed to patch t.MaxFPS",
            fps);
      }
    }
    if (!fps_patched) {
      // Fall back to uncapped rather than leaving the original 120 cap.
      const uint8_t kUncapped[8] = {0x0F, 0x57, 0xC9, 0x90,
                                    0x90, 0x90, 0x90, 0x90};
      WriteBytes(site, kUncapped, sizeof(kUncapped));
      Log("fallback: t.MaxFPS patched to 0 (uncapped)");
    }
  }

  // Clear RHI.TargetRefreshRate (the int immediate right after the first call).
  const int32_t zero = 0;
  if (WriteBytes(site + 27, &zero, sizeof(zero))) {
    Log("RHI.TargetRefreshRate patched to 0");
  } else {
    Log("error: failed to patch RHI.TargetRefreshRate");
  }
}

void ApplySmoothingPatch(uint8_t* site) {
  const uint8_t kJmp = 0xEB;  // force "skip smoothing"
  if (WriteBytes(site + 12, &kJmp, 1)) {
    Log("smoothed frame rate clamp disabled");
  } else {
    Log("error: failed to disable the smoothed frame rate clamp");
  }
}

void ApplyPatches() {
  HMODULE exe = GetModuleHandleW(nullptr);
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(exe, path, MAX_PATH);
  const wchar_t* name = wcsrchr(path, L'\\');
  name = name ? name + 1 : path;
  if (_wcsicmp(name, L"ff7remake_.exe") != 0) {
    return;  // Loaded into some other process, do nothing.
  }

  Log("FF7RemakeFpsUnlock v%s starting (base %p)", kVersion, (void*)exe);

  size_t text_size = 0;
  uint8_t* text = FindSection(exe, ".text", &text_size);
  if (!text) {
    Log("error: .text section not found");
    return;
  }

  int fps = ReadIniInt(L"FPS", 0);
  if (fps < 0) {
    fps = 0;
  }
  if (fps > 1000) {
    fps = 1000;
  }
  const bool disable_smoothing = ReadIniInt(L"DisableSmoothing", 1) != 0;

  BytePattern fps_pattern{};
  BytePattern smoothing_pattern{};
  if (!ParsePattern(kFpsSignature, fps_pattern) ||
      !ParsePattern(kSmoothingSignature, smoothing_pattern)) {
    Log("error: failed to parse built-in patterns");
    return;
  }

  uint8_t* fps_site = FindPattern(text, text_size, fps_pattern);
  if (fps_site) {
    Log("framerate signature found at %p", fps_site);
    ApplyFpsPatch(fps_site, fps);
  } else {
    Log("warning: framerate signature not found (unsupported game build?)");
  }

  if (disable_smoothing) {
    uint8_t* smoothing_site = FindPattern(text, text_size, smoothing_pattern);
    if (smoothing_site) {
      Log("smoothing signature found at %p", smoothing_site);
      ApplySmoothingPatch(smoothing_site);
    } else {
      Log("warning: smoothing signature not found");
    }
  }

  Log("done");
}

DWORD WINAPI PatchThread(LPVOID) {
  ApplyPatches();
  return 0;
}

// ---------------------------------------------------------------------------
// xinput1_3 proxy
// ---------------------------------------------------------------------------

INIT_ONCE g_xinput_once = INIT_ONCE_STATIC_INIT;
HMODULE g_system_xinput = nullptr;

BOOL CALLBACK InitSystemXInput(PINIT_ONCE, PVOID, PVOID*) {
  wchar_t path[MAX_PATH]{};
  UINT len = GetSystemDirectoryW(path, MAX_PATH);
  if (len == 0 || len + 16 >= MAX_PATH) {
    return TRUE;
  }
  wcscat_s(path, L"\\xinput1_3.dll");
  g_system_xinput = LoadLibraryW(path);
  return TRUE;
}

HMODULE SystemXInput() {
  InitOnceExecuteOnce(&g_xinput_once, InitSystemXInput, nullptr, nullptr);
  return g_system_xinput;
}

template <typename Fn>
Fn SystemProc(const char* name) {
  HMODULE module = SystemXInput();
  return module ? reinterpret_cast<Fn>(GetProcAddress(module, name)) : nullptr;
}

constexpr DWORD kDeviceNotConnected = ERROR_DEVICE_NOT_CONNECTED;

}  // namespace

extern "C" {

// The real functions are exported by name/ordinal from xinput1_3.def, which
// aliases these internal proxy functions. They intentionally use different
// names than <xinput.h> to avoid conflicting declarations.
DWORD WINAPI Proxy_XInputGetState(DWORD index, XINPUT_STATE* state) {
  using Fn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
  Fn fn = SystemProc<Fn>("XInputGetState");
  return fn ? fn(index, state) : kDeviceNotConnected;
}

DWORD WINAPI Proxy_XInputSetState(DWORD index,
                                  XINPUT_VIBRATION* vibration) {
  using Fn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
  Fn fn = SystemProc<Fn>("XInputSetState");
  return fn ? fn(index, vibration) : kDeviceNotConnected;
}

DWORD WINAPI Proxy_XInputGetCapabilities(
    DWORD index, DWORD flags, XINPUT_CAPABILITIES* capabilities) {
  using Fn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
  Fn fn = SystemProc<Fn>("XInputGetCapabilities");
  return fn ? fn(index, flags, capabilities) : kDeviceNotConnected;
}

void WINAPI Proxy_XInputEnable(BOOL enable) {
  using Fn = void(WINAPI*)(BOOL);
  Fn fn = SystemProc<Fn>("XInputEnable");
  if (fn) {
    fn(enable);
  }
}

DWORD WINAPI Proxy_XInputGetBatteryInformation(
    DWORD index, BYTE device_type,
    XINPUT_BATTERY_INFORMATION* battery_information) {
  using Fn = DWORD(WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
  Fn fn = SystemProc<Fn>("XInputGetBatteryInformation");
  return fn ? fn(index, device_type, battery_information)
            : kDeviceNotConnected;
}

DWORD WINAPI Proxy_XInputGetKeystroke(
    DWORD index, DWORD reserved, PXINPUT_KEYSTROKE keystroke) {
  using Fn = DWORD(WINAPI*)(DWORD, DWORD, PXINPUT_KEYSTROKE);
  Fn fn = SystemProc<Fn>("XInputGetKeystroke");
  return fn ? fn(index, reserved, keystroke) : kDeviceNotConnected;
}

DWORD WINAPI Proxy_XInputGetDSoundAudioDeviceGuids(DWORD index,
                                                   GUID* render_guid,
                                                   GUID* capture_guid) {
  using Fn = DWORD(WINAPI*)(DWORD, GUID*, GUID*);
  Fn fn = SystemProc<Fn>("XInputGetDSoundAudioDeviceGuids");
  return fn ? fn(index, render_guid, capture_guid) : kDeviceNotConnected;
}

// Undocumented but widely used (guide button state), exported as ordinal 100.
DWORD WINAPI Proxy_XInputGetStateEx(DWORD index, XINPUT_STATE* state) {
  using Fn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
  HMODULE module = SystemXInput();
  Fn fn =
      module ? reinterpret_cast<Fn>(GetProcAddress(module, (LPCSTR)100))
             : nullptr;
  return fn ? fn(index, state) : kDeviceNotConnected;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_self = instance;
    DisableThreadLibraryCalls(instance);
    HANDLE thread = CreateThread(nullptr, 0, PatchThread, nullptr, 0, nullptr);
    if (thread) {
      CloseHandle(thread);
    }
  }
  return TRUE;
}

}  // extern "C"
