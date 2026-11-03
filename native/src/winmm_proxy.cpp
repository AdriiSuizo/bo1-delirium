#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <intrin.h>


static HMODULE systemWinmm() {
    static const HMODULE handle = [] {
        wchar_t filename[MAX_PATH]{};
        const UINT count = GetSystemDirectoryW(filename, MAX_PATH);
        if (!count || count + 11 >= MAX_PATH) return static_cast<HMODULE>(nullptr);
        wcscat_s(filename, L"\\winmm.dll");
        return LoadLibraryExW(filename, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }();
    return handle;
}

template<class Function>
static Function systemFunction(const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(systemWinmm(), name));
}

static void startMod(void* caller) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(caller, &memory, sizeof(memory)) || memory.AllocationBase != GetModuleHandleW(nullptr))
        return;

    static volatile LONG started = 0;
    if (InterlockedCompareExchange(&started, 1, 0) != 0) return;
    wchar_t gameMod[32768]{}, adapter[32768]{};
    const DWORD gameModLength = GetEnvironmentVariableW(L"BO1_DELIRIUM_LOCAL_GAMEMOD", gameMod, 32768);
    const DWORD adapterLength = GetEnvironmentVariableW(L"BO1_DELIRIUM_LOCAL_ADAPTER", adapter, 32768);
    if (!gameModLength || !adapterLength || gameModLength >= 32768 || adapterLength >= 32768)
        return;

    constexpr DWORD search = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    const HMODULE loader = LoadLibraryExW(gameMod, nullptr, search);
    const HMODULE native = loader ? LoadLibraryExW(adapter, nullptr, search) : nullptr;
    using Attach = DWORD(WINAPI*)(void*);
    const auto attach = native ? reinterpret_cast<Attach>(GetProcAddress(native, "DeliriumAttach")) : nullptr;
    if (!attach || attach(nullptr) != 1) ExitProcess(2);

    wchar_t eventName[128]{};
    const DWORD length = GetEnvironmentVariableW(L"BO1_DELIRIUM_BOOT_EVENT", eventName, 128);
    if (length && length < 128) {
        if (HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName)) {
            SetEvent(event);
            CloseHandle(event);
        }
    }
}

extern "C" DWORD WINAPI proxy_timeGetTime() {
    startMod(_ReturnAddress());
    static const auto function = systemFunction<decltype(&::timeGetTime)>("timeGetTime");
    if (!function) ExitProcess(3);
    return function();
}
extern "C" MMRESULT WINAPI proxy_timeBeginPeriod(UINT period) {
    startMod(_ReturnAddress());
    static const auto function = systemFunction<decltype(&::timeBeginPeriod)>("timeBeginPeriod");
    if (!function) ExitProcess(3);
    return function(period);
}

static FARPROC __cdecl resolveExport(const char* name) {
    const auto function = GetProcAddress(systemWinmm(), name);
    if (!function) ExitProcess(3);
    return function;
}

// Forward the original x86 stack and register ABI to the system DLL.
#define FORWARD_EXPORT(name) \
    static const char export_##name[] = #name; \
    extern "C" __declspec(naked) void proxy_##name() { \
        __asm pushad \
        __asm push offset export_##name \
        __asm call resolveExport \
        __asm add esp, 4 \
        __asm mov [esp + 28], eax \
        __asm popad \
        __asm jmp eax \
    }
FORWARD_EXPORT(CloseDriver)
FORWARD_EXPORT(DefDriverProc)
FORWARD_EXPORT(DriverCallback)
FORWARD_EXPORT(DrvGetModuleHandle)
FORWARD_EXPORT(GetDriverModuleHandle)
FORWARD_EXPORT(NotifyCallbackData)
FORWARD_EXPORT(OpenDriver)
FORWARD_EXPORT(PlaySound)
FORWARD_EXPORT(PlaySoundA)
FORWARD_EXPORT(PlaySoundW)
FORWARD_EXPORT(SendDriverMessage)
FORWARD_EXPORT(WOW32DriverCallback)
FORWARD_EXPORT(WOW32ResolveMultiMediaHandle)
FORWARD_EXPORT(WOWAppExit)
FORWARD_EXPORT(aux32Message)
FORWARD_EXPORT(auxGetDevCapsA)
FORWARD_EXPORT(auxGetDevCapsW)
FORWARD_EXPORT(auxGetNumDevs)
FORWARD_EXPORT(auxGetVolume)
FORWARD_EXPORT(auxOutMessage)
FORWARD_EXPORT(auxSetVolume)
FORWARD_EXPORT(joy32Message)
FORWARD_EXPORT(joyConfigChanged)
FORWARD_EXPORT(joyGetDevCapsA)
FORWARD_EXPORT(joyGetDevCapsW)
FORWARD_EXPORT(joyGetNumDevs)
FORWARD_EXPORT(joyGetPos)
FORWARD_EXPORT(joyGetPosEx)
FORWARD_EXPORT(joyGetThreshold)
FORWARD_EXPORT(joyReleaseCapture)
FORWARD_EXPORT(joySetCapture)
FORWARD_EXPORT(joySetThreshold)
FORWARD_EXPORT(mci32Message)
FORWARD_EXPORT(mciDriverNotify)
FORWARD_EXPORT(mciDriverYield)
FORWARD_EXPORT(mciExecute)
FORWARD_EXPORT(mciFreeCommandResource)
FORWARD_EXPORT(mciGetCreatorTask)
FORWARD_EXPORT(mciGetDeviceIDA)
FORWARD_EXPORT(mciGetDeviceIDFromElementIDA)
FORWARD_EXPORT(mciGetDeviceIDFromElementIDW)
FORWARD_EXPORT(mciGetDeviceIDW)
FORWARD_EXPORT(mciGetDriverData)
FORWARD_EXPORT(mciGetErrorStringA)
FORWARD_EXPORT(mciGetErrorStringW)
FORWARD_EXPORT(mciGetYieldProc)
FORWARD_EXPORT(mciLoadCommandResource)
FORWARD_EXPORT(mciSendCommandA)
FORWARD_EXPORT(mciSendCommandW)
FORWARD_EXPORT(mciSendStringA)
FORWARD_EXPORT(mciSendStringW)
FORWARD_EXPORT(mciSetDriverData)
FORWARD_EXPORT(mciSetYieldProc)
FORWARD_EXPORT(mid32Message)
FORWARD_EXPORT(midiConnect)
FORWARD_EXPORT(midiDisconnect)
FORWARD_EXPORT(midiInAddBuffer)
FORWARD_EXPORT(midiInClose)
FORWARD_EXPORT(midiInGetDevCapsA)
FORWARD_EXPORT(midiInGetDevCapsW)
FORWARD_EXPORT(midiInGetErrorTextA)
FORWARD_EXPORT(midiInGetErrorTextW)
FORWARD_EXPORT(midiInGetID)
FORWARD_EXPORT(midiInGetNumDevs)
FORWARD_EXPORT(midiInMessage)
FORWARD_EXPORT(midiInOpen)
FORWARD_EXPORT(midiInPrepareHeader)
FORWARD_EXPORT(midiInReset)
FORWARD_EXPORT(midiInStart)
FORWARD_EXPORT(midiInStop)
FORWARD_EXPORT(midiInUnprepareHeader)
FORWARD_EXPORT(midiOutCacheDrumPatches)
FORWARD_EXPORT(midiOutCachePatches)
FORWARD_EXPORT(midiOutClose)
FORWARD_EXPORT(midiOutGetDevCapsA)
FORWARD_EXPORT(midiOutGetDevCapsW)
FORWARD_EXPORT(midiOutGetErrorTextA)
FORWARD_EXPORT(midiOutGetErrorTextW)
FORWARD_EXPORT(midiOutGetID)
FORWARD_EXPORT(midiOutGetNumDevs)
FORWARD_EXPORT(midiOutGetVolume)
FORWARD_EXPORT(midiOutLongMsg)
FORWARD_EXPORT(midiOutMessage)
FORWARD_EXPORT(midiOutOpen)
FORWARD_EXPORT(midiOutPrepareHeader)
FORWARD_EXPORT(midiOutReset)
FORWARD_EXPORT(midiOutSetVolume)
FORWARD_EXPORT(midiOutShortMsg)
FORWARD_EXPORT(midiOutUnprepareHeader)
FORWARD_EXPORT(midiStreamClose)
FORWARD_EXPORT(midiStreamOpen)
FORWARD_EXPORT(midiStreamOut)
FORWARD_EXPORT(midiStreamPause)
FORWARD_EXPORT(midiStreamPosition)
FORWARD_EXPORT(midiStreamProperty)
FORWARD_EXPORT(midiStreamRestart)
FORWARD_EXPORT(midiStreamStop)
FORWARD_EXPORT(mixerClose)
FORWARD_EXPORT(mixerGetControlDetailsA)
FORWARD_EXPORT(mixerGetControlDetailsW)
FORWARD_EXPORT(mixerGetDevCapsA)
FORWARD_EXPORT(mixerGetDevCapsW)
FORWARD_EXPORT(mixerGetID)
FORWARD_EXPORT(mixerGetLineControlsA)
FORWARD_EXPORT(mixerGetLineControlsW)
FORWARD_EXPORT(mixerGetLineInfoA)
FORWARD_EXPORT(mixerGetLineInfoW)
FORWARD_EXPORT(mixerGetNumDevs)
FORWARD_EXPORT(mixerMessage)
FORWARD_EXPORT(mixerOpen)
FORWARD_EXPORT(mixerSetControlDetails)
FORWARD_EXPORT(mmDrvInstall)
FORWARD_EXPORT(mmGetCurrentTask)
FORWARD_EXPORT(mmTaskBlock)
FORWARD_EXPORT(mmTaskCreate)
FORWARD_EXPORT(mmTaskSignal)
FORWARD_EXPORT(mmTaskYield)
FORWARD_EXPORT(mmioAdvance)
FORWARD_EXPORT(mmioAscend)
FORWARD_EXPORT(mmioClose)
FORWARD_EXPORT(mmioCreateChunk)
FORWARD_EXPORT(mmioDescend)
FORWARD_EXPORT(mmioFlush)
FORWARD_EXPORT(mmioGetInfo)
FORWARD_EXPORT(mmioInstallIOProcA)
FORWARD_EXPORT(mmioInstallIOProcW)
FORWARD_EXPORT(mmioOpenA)
FORWARD_EXPORT(mmioOpenW)
FORWARD_EXPORT(mmioRead)
FORWARD_EXPORT(mmioRenameA)
FORWARD_EXPORT(mmioRenameW)
FORWARD_EXPORT(mmioSeek)
FORWARD_EXPORT(mmioSendMessage)
FORWARD_EXPORT(mmioSetBuffer)
FORWARD_EXPORT(mmioSetInfo)
FORWARD_EXPORT(mmioStringToFOURCCA)
FORWARD_EXPORT(mmioStringToFOURCCW)
FORWARD_EXPORT(mmioWrite)
FORWARD_EXPORT(mmsystemGetVersion)
FORWARD_EXPORT(mod32Message)
FORWARD_EXPORT(mxd32Message)
FORWARD_EXPORT(sndPlaySoundA)
FORWARD_EXPORT(sndPlaySoundW)
FORWARD_EXPORT(tid32Message)
FORWARD_EXPORT(timeEndPeriod)
FORWARD_EXPORT(timeGetDevCaps)
FORWARD_EXPORT(timeGetSystemTime)
FORWARD_EXPORT(timeKillEvent)
FORWARD_EXPORT(timeSetEvent)
FORWARD_EXPORT(waveInAddBuffer)
FORWARD_EXPORT(waveInClose)
FORWARD_EXPORT(waveInGetDevCapsA)
FORWARD_EXPORT(waveInGetDevCapsW)
FORWARD_EXPORT(waveInGetErrorTextA)
FORWARD_EXPORT(waveInGetErrorTextW)
FORWARD_EXPORT(waveInGetID)
FORWARD_EXPORT(waveInGetNumDevs)
FORWARD_EXPORT(waveInGetPosition)
FORWARD_EXPORT(waveInMessage)
FORWARD_EXPORT(waveInOpen)
FORWARD_EXPORT(waveInPrepareHeader)
FORWARD_EXPORT(waveInReset)
FORWARD_EXPORT(waveInStart)
FORWARD_EXPORT(waveInStop)
FORWARD_EXPORT(waveInUnprepareHeader)
FORWARD_EXPORT(waveOutBreakLoop)
FORWARD_EXPORT(waveOutClose)
FORWARD_EXPORT(waveOutGetDevCapsA)
FORWARD_EXPORT(waveOutGetDevCapsW)
FORWARD_EXPORT(waveOutGetErrorTextA)
FORWARD_EXPORT(waveOutGetErrorTextW)
FORWARD_EXPORT(waveOutGetID)
FORWARD_EXPORT(waveOutGetNumDevs)
FORWARD_EXPORT(waveOutGetPitch)
FORWARD_EXPORT(waveOutGetPlaybackRate)
FORWARD_EXPORT(waveOutGetPosition)
FORWARD_EXPORT(waveOutGetVolume)
FORWARD_EXPORT(waveOutMessage)
FORWARD_EXPORT(waveOutOpen)
FORWARD_EXPORT(waveOutPause)
FORWARD_EXPORT(waveOutPrepareHeader)
FORWARD_EXPORT(waveOutReset)
FORWARD_EXPORT(waveOutRestart)
FORWARD_EXPORT(waveOutSetPitch)
FORWARD_EXPORT(waveOutSetPlaybackRate)
FORWARD_EXPORT(waveOutSetVolume)
FORWARD_EXPORT(waveOutUnprepareHeader)
FORWARD_EXPORT(waveOutWrite)
FORWARD_EXPORT(wid32Message)
FORWARD_EXPORT(wod32Message)
