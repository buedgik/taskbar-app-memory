// ==WindhawkMod==
// @id              taskbar-remember-positions
// @name            Taskbar Remember Positions
// @description     An app you close and open again goes back to its place on the taskbar, instead of to the end
// @version         0.1.0
// @author          buedgik
// @github          https://github.com/buedgik
// @homepage        https://github.com/buedgik/taskbar-remember-positions
// @license         MIT
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lcomctl32 -ladvapi32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar Remember Positions

On the Windows 11 taskbar, an app that isn't pinned loses its place when you
close it: open it again and its button goes to the end. With this mod it goes
back where it was.

- **Arrange the buttons by dragging them**, as usual. The mod remembers the
  order you leave them in.
- **Close an app and open it later**, even after restarting the computer: its
  button goes back between the same neighbours it had.
- **Pinned apps stay where Windows puts them.** The mod never moves them, it
  only uses them as landmarks: an app you left between two pinned apps goes
  back between them.
- **An app the mod has never seen** goes to the end, as usual, and is
  remembered from then on.

## How the place is chosen

The mod keeps one list of every app it has seen on the taskbar, in the order
they were last seen, closed apps included. When an app opens, its button goes
right after the nearest app that comes before it in that list and is on the
taskbar now. When none of those are there, it goes in front of the apps on
the taskbar that the list has.

So the order you arrange is kept whatever order the apps open in, which is
what makes it survive a restart: the apps that start with Windows each go to
their place as they appear.

Only what happens while the mod is running is learned. Buttons rearranged
while it's disabled go back to their old places when their apps reopen; drag
them with the mod on to change that. Pinned apps are the exception: when their
order changes without a drag (another program moving a pin, or pins moved
while the mod was off), the mod follows it from the next time a button opens
or is dragged. Until then, an app next to the pin that moved can come back on
its other side if it closes first, or if Explorer restarts (adding a separator
with Taskbar Icon Separators is one such change).

## Worth knowing

- **Apps are told apart by their App ID**, the identity Windows uses to put
  windows under one button. An app that changes it is a new app to the mod:
  a browser's new profile, or a portable app moved to another folder.
- **An app you unpin keeps the place it had among the pinned apps**: when it
  opens again, it goes there. Drag it once to put it somewhere else.
- **Only the order of the buttons is kept**, not the order of the windows
  inside one button.
- **With several monitors** there is one list for all the taskbars, and each
  taskbar places an app relative to the apps it shows. With "Show my taskbar
  apps on: All taskbars" (the default), Windows keeps the taskbars in the same
  order after each drag. Not after pinning an app from Start, though: the new
  pin goes to the end of the main taskbar but among the pinned apps on the
  others, and until the next drag an app next to it there can come back on
  its other side. With the two "taskbar where window is open" settings, an app
  can have a button on two taskbars but has one place in the list, so arrange
  the apps on the main taskbar; and on the other taskbars, where the pinned
  apps aren't shown, a pinned app's button is left where Windows puts it.
- **Switching virtual desktops** takes buttons off the taskbar and puts them
  back; they come back in the remembered order.
- **With the Taskbar Grouping mod**, which can give each window a button of its
  own, an app's first button goes back to its place, and the buttons of its
  other windows go where Taskbar Grouping's settings say. Their places aren't
  remembered: an app that sat right next to another app's extra-window buttons
  can come back on their far side, and dropping an app between another app and
  that app's extra windows isn't kept. When the first window closes, Taskbar
  Grouping passes its identity on to another of the app's windows, and that
  window's button becomes the app's place.
- **The list is the file `order.txt`**, in
  `%ProgramData%\Windhawk\Engine\ModsWritable\mod-storage\`, then the mod's
  folder (`taskbar-remember-positions`, or `local@taskbar-remember-positions`
  for a mod compiled in Windhawk's editor), then a folder named after your
  account's SID (`whoami /user` shows it). It's read when the mod starts and
  rewritten while it runs: to reset or edit it, disable the mod, delete or
  edit the file, then enable the mod again. Its first line is
  `taskbar-remember-positions v1`, and each line after it is one app, in
  order: the day it was last seen, a tab, and its App ID. A file the mod can't
  read is moved aside as `order.txt.<date>-<time>.bad`; one it can only read
  in part is copied there, then rewritten with the lines it could read.
  Uninstalling the mod deletes the folder.
  Past 256 apps, the ones not seen for the longest time are forgotten.
- **Windows 11 with its own taskbar**, made on 25H2. With the old taskbar that
  ExplorerPatcher or StartAllBack bring back, the mod does nothing. On 21H2,
  and on 22H2 without recent updates, buttons may still open at the end.
*/
// ==/WindhawkModReadme==

#include <windhawk_utils.h>

#include <commctrl.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// taskbar.dll
// ---------------------------------------------------------------------------
//
// Each taskbar (the main one and one per extra monitor) keeps its buttons in a
// comctl32 DPA of button groups, one group per app, in the order they're
// shown. Two of taskbar.dll's functions put a group into that list:
// CTaskListWnd::_CreateTBGroup when an app gets a button (at the end, unless
// it's pinned) and CTaskListWnd::TryMoveGroup when one is dragged. Both do it
// with DPA_InsertPtr, which is where this mod steps in: that's where the list
// is at hand, with no need to know where it lives inside CTaskListWnd, and
// where changing the index is all it takes for the new button to appear in the
// right place (the index DPA_InsertPtr returns is the one the XAML view is
// told about).

using CTaskListWnd__CreateTBGroup_t = void*(WINAPI*)(void* pThis,
                                                     void* taskGroup,
                                                     int index);
CTaskListWnd__CreateTBGroup_t CTaskListWnd__CreateTBGroup_Original;

using CTaskListWnd_TryMoveGroup_t = bool(WINAPI*)(void* pThis,
                                                  void* taskGroup,
                                                  UINT index);
CTaskListWnd_TryMoveGroup_t CTaskListWnd_TryMoveGroup_Original;

using CTaskListWnd_IsOnPrimaryTaskband_t = int(WINAPI*)(void* pThis);
CTaskListWnd_IsOnPrimaryTaskband_t CTaskListWnd_IsOnPrimaryTaskband;

using CTaskBtnGroup_GetGroup_t = void*(WINAPI*)(void* pThis);
CTaskBtnGroup_GetGroup_t CTaskBtnGroup_GetGroup;

using CTaskGroup_GetAppID_t = PCWSTR(WINAPI*)(void* pThis);
CTaskGroup_GetAppID_t CTaskGroup_GetAppID;

using CTaskGroup_GetFlags_t = DWORD(WINAPI*)(void* pThis);
CTaskGroup_GetFlags_t CTaskGroup_GetFlags;

// What a button group looks like, to recognize one among the DPA_InsertPtr
// calls that happen while a group is being created or moved.
void* CTaskBtnGroup_ITaskBtnGroup_vftable;

// The part of CTaskListWnd that IsOnPrimaryTaskband belongs to.
void* CTaskListWnd_ITaskListUI_vftable;

using DPA_InsertPtr_t = decltype(&DPA_InsertPtr);
DPA_InsertPtr_t DPA_InsertPtr_Original;

constexpr DWORD kTaskGroupPinned = 1;

// _CreateTBGroup gets the CTaskListWnd itself, and IsOnPrimaryTaskband wants
// its ITaskListUI part, which is found by its vtable (0x28 bytes in, on 26100).
void* TaskListUIOf(void* taskList) {
    static std::atomic<int> knownSlot{-1};
    if (!taskList) {
        return nullptr;
    }
    void** slots = (void**)taskList;
    int slot = knownSlot;
    if (slot >= 0 && slots[slot] == CTaskListWnd_ITaskListUI_vftable) {
        return &slots[slot];
    }
    for (int i = 0; i < 64; i++) {
        if (slots[i] == CTaskListWnd_ITaskListUI_vftable) {
            knownSlot = i;
            return &slots[i];
        }
    }
    return nullptr;
}

// "Show my taskbar apps on": All taskbars, the default, unless the value says
// one of the two "taskbar where window is open" settings. Read the way
// taskbar.dll reads it.
bool AllTaskbarsShowPinnedApps() {
    DWORD mode = 0;
    DWORD size = sizeof(mode);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                     L"Explorer\\Advanced",
                     L"MMTaskbarMode", RRF_RT_REG_DWORD, nullptr, &mode,
                     &size) != ERROR_SUCCESS) {
        return true;
    }
    return mode != 1 && mode != 2;
}

// Whether a taskbar is the main one, and whether it shows the pinned apps: the
// main one always does, the others with "All taskbars". When that can't be
// told, the taskbar is taken to be the main one.
struct TaskbarKind {
    bool main;
    bool showsPinned;
};

TaskbarKind KindOfTaskbar(void* taskListUI) {
    if (!taskListUI ||
        *(void**)taskListUI != CTaskListWnd_ITaskListUI_vftable ||
        CTaskListWnd_IsOnPrimaryTaskband(taskListUI)) {
        return {true, true};
    }
    return {false, AllTaskbarsShowPinnedApps()};
}

// ---------------------------------------------------------------------------
// App keys
// ---------------------------------------------------------------------------

constexpr size_t kMaxKeyLength = 1024;

// App IDs are case-insensitive: the key is the App ID in lowercase, without
// spaces at either end (the list file doesn't keep them). An App ID that can't
// be written to the file as it is (a control character would break the lines,
// a lone surrogate wouldn't survive UTF-8) gets no key, and its app is left
// where Windows puts it.
//
// The Taskbar Grouping mod can give each window a group of its own: an app's
// first window keeps its App ID, and the others get "~Wh~" plus a letter and a
// number that change every time. The key keeps "~wh~" and drops the rest, so
// those extra windows are one app to this mod, apart from the first. While the
// app's own button is on a taskbar they're left out of the order there (see
// ReadButtons): they go where Taskbar Grouping puts them.
std::wstring KeyFromAppId(PCWSTR appId) {
    if (!appId) {
        return L"";
    }
    size_t length = wcsnlen(appId, kMaxKeyLength + 1);
    if (length == 0 || length > kMaxKeyLength) {
        return L"";
    }
    for (size_t i = 0; i < length; i++) {
        if (appId[i] < 0x20 || appId[i] == 0x7F) {
            return L"";
        }
    }
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, appId, (int)length,
                            nullptr, 0, nullptr, nullptr) <= 0) {
        return L"";
    }
    int lowerLength = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
                                    appId, (int)length, nullptr, 0, nullptr,
                                    nullptr, 0);
    if (lowerLength <= 0) {
        return L"";
    }
    std::wstring key(lowerLength, L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, appId, (int)length,
                  key.data(), lowerLength, nullptr, nullptr, 0);
    // The spaces go from the App ID itself, before any "~wh~", so an extra
    // window's key still starts with its app's.
    size_t suffix = key.find(L"~wh~");
    bool extraWindow = suffix != std::wstring::npos && suffix > 0;
    if (extraWindow) {
        key.resize(suffix);
    }
    size_t first = key.find_first_not_of(L' ');
    if (first == std::wstring::npos) {
        return L"";
    }
    key = key.substr(first, key.find_last_not_of(L' ') - first + 1);
    if (extraWindow) {
        key += L"~wh~";
    }
    return key;
}

// The app a key belongs to: the key without Taskbar Grouping's "~wh~".
std::wstring AppOfKey(const std::wstring& key) {
    size_t suffix = key.find(L"~wh~");
    return suffix != std::wstring::npos && suffix > 0 ? key.substr(0, suffix)
                                                      : key;
}

// A group's key on a taskbar. A pinned app's place is Windows' to keep, and the
// mod uses it as a landmark wherever the pinned apps are shown. On a taskbar
// that doesn't show them, a pinned app only appears while it has a window on
// that monitor, as a button Windows puts at the end: sharing the key would tie
// that monitor's order to the pinned apps elsewhere, so there it gets none and
// is left alone.
std::wstring TaskGroupKey(void* taskGroup, bool showsPinned, bool* pinned) {
    *pinned = false;
    if (!taskGroup) {
        return L"";
    }
    *pinned = CTaskGroup_GetFlags(taskGroup) & kTaskGroupPinned;
    if (*pinned && !showsPinned) {
        return L"";
    }
    return KeyFromAppId(CTaskGroup_GetAppID(taskGroup));
}

bool IsButtonGroup(void* p) {
    return (ULONG_PTR)p >= 0x10000 &&
           *(void**)p == CTaskBtnGroup_ITaskBtnGroup_vftable;
}

// A taskbar's buttons, in the order they're shown.
struct Buttons {
    // Each button's app key, empty for one that isn't part of the order: no
    // key, a pinned app's button where pinned apps aren't shown, another
    // button with a key already seen further left (an app's place is where
    // its first button is), or one of the extra windows Taskbar Grouping gives
    // an app whose own button is on this taskbar.
    std::vector<std::wstring> keys;
    // Each button's app, kept even where the key isn't: the key without
    // Taskbar Grouping's "~wh~".
    std::vector<std::wstring> apps;
    // Whether each button with a key is a pinned app's.
    std::vector<bool> pinned;
    // Each button's task group, to find a given one.
    std::vector<void*> groups;
};

// False when the list doesn't look like one.
bool ReadButtons(HDPA buttonGroups, bool showsPinned, Buttons& buttons) {
    buttons = Buttons();
    int count = DPA_GetPtrCount(buttonGroups);
    if (count < 0 || count > 4096) {
        return false;
    }
    std::unordered_set<std::wstring> seen;
    for (int i = 0; i < count; i++) {
        void* buttonGroup = DPA_FastGetPtr(buttonGroups, i);
        void* taskGroup =
            IsButtonGroup(buttonGroup) ? CTaskBtnGroup_GetGroup(buttonGroup)
                                       : nullptr;
        bool pinned;
        std::wstring key = TaskGroupKey(taskGroup, showsPinned, &pinned);
        buttons.apps.push_back(AppOfKey(key));
        if (!key.empty() && !seen.insert(key).second) {
            key.clear();
        }
        buttons.pinned.push_back(pinned && !key.empty());
        buttons.keys.push_back(std::move(key));
        buttons.groups.push_back(taskGroup);
    }
    for (size_t i = 0; i < buttons.keys.size(); i++) {
        std::wstring& key = buttons.keys[i];
        if (!key.empty() && key != buttons.apps[i] &&
            seen.count(buttons.apps[i])) {
            key.clear();
            buttons.pinned[i] = false;
        }
    }
    return true;
}

int FindGroup(const Buttons& buttons, void* taskGroup) {
    for (size_t i = 0; i < buttons.groups.size(); i++) {
        if (buttons.groups[i] == taskGroup) {
            return (int)i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// The order
// ---------------------------------------------------------------------------
//
// One list of apps, in the order they were last seen on a taskbar, apps that
// are closed now included: a closed app stays right after the app it followed.
// Pinned apps are in it too, as the landmarks everything else is placed by.

struct Entry {
    std::wstring key;
    // When the app was last on a taskbar, in days, to know which apps to forget
    // when the list gets too long.
    DWORD lastSeen;
};

constexpr size_t kMaxApps = 256;

// Guards everything below. The hooks run on the taskbar's thread; saving runs
// on a thread pool thread.
std::mutex g_orderMutex;
std::vector<Entry> g_order;
bool g_orderChanged;
// The file couldn't be read, or is from a newer version of the mod: saving
// would overwrite what's in it.
bool g_saveBlocked;
// It couldn't be read at all: see RetryLoad.
bool g_loadPending;
PTP_TIMER g_saveTimer;
int g_saveRetries;

std::atomic<bool> g_unloading;

DWORD Today() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER time{{ft.dwLowDateTime, ft.dwHighDateTime}};
    return (DWORD)(time.QuadPart / 864000000000ULL);
}

int IndexOf(const std::vector<Entry>& order, const std::wstring& key) {
    for (size_t i = 0; i < order.size(); i++) {
        if (order[i].key == key) {
            return (int)i;
        }
    }
    return -1;
}

// Forgets the apps not seen for the longest time until there are few enough.
// The apps on the taskbar at hand (`shown`) are never the ones forgotten, and
// between apps last seen the same day, the one further down the list goes:
// that's where the apps seen only once end up, not the pinned apps at the top.
void PruneOrder(std::vector<Entry>& order,
                const std::vector<std::wstring>& shown) {
    if (order.size() <= kMaxApps) {
        return;
    }
    std::unordered_set<std::wstring> onTaskbar(shown.begin(), shown.end());
    while (order.size() > kMaxApps) {
        int victim = -1;
        for (size_t i = order.size(); i-- > 0;) {
            if (!onTaskbar.count(order[i].key) &&
                (victim < 0 || order[i].lastSeen < order[victim].lastSeen)) {
                victim = (int)i;
            }
        }
        if (victim < 0) {
            victim = (int)order.size() - 1;
        }
        order.erase(order.begin() + victim);
    }
}

void CommitOrder(std::vector<Entry>&& order,
                 const std::vector<std::wstring>& shown) {
    PruneOrder(order, shown);
    bool same = order.size() == g_order.size();
    for (size_t i = 0; same && i < order.size(); i++) {
        same = order[i].key == g_order[i].key &&
               order[i].lastSeen == g_order[i].lastSeen;
    }
    if (!same) {
        g_order = std::move(order);
        g_orderChanged = true;
    }
}

// Adds the apps on a taskbar that the order doesn't have yet, and marks every
// app on it as seen today. A new app goes after the nearest app on its left
// there and after the apps that follow that one in the order without being on
// this taskbar (closed, or shown on another monitor): those keep their place
// next to it, before the newcomer. With nothing on its left, it goes right
// before the nearest app on its right, or at the end.
void TakeInNewApps(const std::vector<std::wstring>& keys) {
    std::vector<Entry> order = g_order;
    DWORD today = Today();
    std::unordered_set<std::wstring> shown;
    for (const auto& key : keys) {
        if (!key.empty()) {
            shown.insert(key);
        }
    }
    for (size_t i = 0; i < keys.size(); i++) {
        const std::wstring& key = keys[i];
        if (key.empty()) {
            continue;
        }
        int at = IndexOf(order, key);
        if (at >= 0) {
            order[at].lastSeen = today;
            continue;
        }
        // Going left to right, every app before this one is in the order by
        // now.
        for (size_t j = i; j-- > 0 && at < 0;) {
            if (!keys[j].empty() && keys[j] != key) {
                int neighbour = IndexOf(order, keys[j]);
                if (neighbour >= 0) {
                    at = neighbour + 1;
                }
            }
        }
        while (at >= 0 && at < (int)order.size() &&
               !shown.count(order[at].key)) {
            at++;
        }
        for (size_t j = i + 1; j < keys.size() && at < 0; j++) {
            if (!keys[j].empty() && keys[j] != key) {
                at = IndexOf(order, keys[j]);
            }
        }
        if (at < 0) {
            at = (int)order.size();
        }
        order.insert(order.begin() + at, Entry{key, today});
    }
    CommitOrder(std::move(order), keys);
}

// The button at `moved` is where the user (or Windows) put it: in the order,
// its app goes right after the nearest app on its left there, or, when there's
// none, right before the app on its right that comes first in the order. The
// other apps don't move, closed ones included.
void LearnMove(const std::vector<std::wstring>& keys, int moved) {
    const std::wstring& key = keys[moved];
    if (key.empty()) {
        return;
    }

    std::vector<Entry> order = g_order;
    int old = IndexOf(order, key);
    if (old >= 0) {
        order.erase(order.begin() + old);
    }

    int at = -1;
    for (int j = moved; j-- > 0 && at < 0;) {
        if (!keys[j].empty() && keys[j] != key) {
            int neighbour = IndexOf(order, keys[j]);
            if (neighbour >= 0) {
                at = neighbour + 1;
            }
        }
    }
    // Nothing on its left: it goes before every app on its right, which on a
    // taskbar arranged while the mod was off needn't be the nearest one.
    if (at < 0) {
        for (size_t j = moved + 1; j < keys.size(); j++) {
            if (!keys[j].empty() && keys[j] != key) {
                int neighbour = IndexOf(order, keys[j]);
                if (neighbour >= 0 && (at < 0 || neighbour < at)) {
                    at = neighbour;
                }
            }
        }
    }
    // No neighbour to go by: the app stays where it was.
    if (at < 0) {
        at = old >= 0 ? old : (int)order.size();
    }
    order.insert(order.begin() + at, Entry{key, Today()});
    CommitOrder(std::move(order), keys);
}

// The pinned apps' order is Windows' to keep, and it can change without a drag
// the mod sees: another program moving a pin (Windows then moves just that
// pin), or pins moved while the mod wasn't running. So on each change, a
// taskbar's buttons are checked against the order, and the fewest pins that
// explain the difference are moved in it; the apps around them stay.
//
// What stays is the largest run of buttons already in the order's order,
// counting each pin as more than all the other buttons together: the most pins
// possible, and between runs with as many, the one that agrees with more of
// the other buttons. Each pin outside it goes right after the nearest button
// on its left that's in the run or already put back (right before the nearest
// on its right when there's none), so one pass is enough. `ignore` is a button
// left out, the one being dragged. Pins the order doesn't know yet are left to
// TakeInNewApps.
void ReconcilePins(const Buttons& buttons, int ignore) {
    std::vector<int> index;
    std::vector<int> place;
    std::vector<bool> isPin;
    bool anyPin = false;
    for (size_t i = 0; i < buttons.keys.size(); i++) {
        if ((int)i == ignore || buttons.keys[i].empty()) {
            continue;
        }
        int at = IndexOf(g_order, buttons.keys[i]);
        if (at >= 0) {
            index.push_back((int)i);
            place.push_back(at);
            isPin.push_back(buttons.pinned[i]);
            anyPin = anyPin || buttons.pinned[i];
        }
    }
    if (!anyPin) {
        return;
    }

    size_t count = index.size();
    long long pinWeight = (long long)count + 1;
    std::vector<long long> score(count);
    std::vector<int> previous(count, -1);
    int best = -1;
    for (size_t i = 0; i < count; i++) {
        long long weight = isPin[i] ? pinWeight : 1;
        score[i] = weight;
        for (size_t j = 0; j < i; j++) {
            if (place[j] < place[i] && score[j] + weight >= score[i]) {
                score[i] = score[j] + weight;
                previous[i] = (int)j;
            }
        }
        if (best < 0 || score[i] >= score[best]) {
            best = (int)i;
        }
    }
    std::vector<bool> anchor(count, false);
    for (int i = best; i >= 0; i = previous[i]) {
        anchor[i] = true;
    }
    bool allPinsStay = true;
    for (size_t i = 0; i < count; i++) {
        allPinsStay = allPinsStay && (!isPin[i] || anchor[i]);
    }
    if (allPinsStay) {
        return;
    }

    std::vector<Entry> order = g_order;
    for (size_t i = 0; i < count; i++) {
        if (!isPin[i] || anchor[i]) {
            continue;
        }
        const std::wstring& key = buttons.keys[index[i]];
        int old = IndexOf(order, key);
        Entry entry = order[old];
        order.erase(order.begin() + old);
        int at = -1;
        for (size_t j = i; j-- > 0 && at < 0;) {
            if (anchor[j]) {
                at = IndexOf(order, buttons.keys[index[j]]) + 1;
            }
        }
        for (size_t j = i + 1; j < count && at < 0; j++) {
            if (anchor[j]) {
                at = IndexOf(order, buttons.keys[index[j]]);
            }
        }
        if (at < 0) {
            at = old;
        }
        order.insert(order.begin() + at, entry);
        anchor[i] = true;
    }
    CommitOrder(std::move(order), buttons.keys);
}

// A pinned app got a button at the end of the main taskbar: pinned just now,
// or one of the pinned apps coming back when Explorer starts, before any other
// button. A new pin's place is learned. So is a known one's when it landed
// after other apps' buttons and after an app that comes later than it in the
// order: that's pinning an app the mod already knew. Pins coming back at
// start are left alone; ReconcilePins sorts out a changed order once they're
// all there.
void LearnPinnedPlace(const Buttons& buttons, int pinned) {
    if (pinned < 0 || buttons.keys[pinned].empty()) {
        return;
    }
    int self = IndexOf(g_order, buttons.keys[pinned]);
    if (self < 0) {
        LearnMove(buttons.keys, pinned);
        return;
    }
    bool afterOtherApps = false;
    bool afterLaterApps = false;
    for (int j = 0; j < pinned; j++) {
        if (!buttons.keys[j].empty()) {
            afterOtherApps = afterOtherApps || !buttons.pinned[j];
            afterLaterApps =
                afterLaterApps || IndexOf(g_order, buttons.keys[j]) > self;
        }
    }
    if (afterOtherApps && afterLaterApps) {
        LearnMove(buttons.keys, pinned);
    }
}

// Where a new button for `key` goes on a taskbar: right after the button of
// the nearest app before it in the order, or, when none of those is there, in
// front of the first button of an app the order has. -1 when the order has
// nothing to say. Buttons of the anchor app's other windows (Taskbar Grouping)
// right after it stay with it; only on that side, since an app that was
// dragged past its own extra windows must not be split from them.
int PlaceFor(const Buttons& buttons, const std::wstring& key) {
    int self = IndexOf(g_order, key);
    if (self < 0) {
        return -1;
    }
    std::unordered_map<std::wstring, int> position;
    for (size_t i = 0; i < g_order.size(); i++) {
        position.emplace(g_order[i].key, (int)i);
    }

    const std::vector<std::wstring>& keys = buttons.keys;
    int before = -1;
    int beforePosition = -1;
    int after = -1;
    for (size_t i = 0; i < keys.size(); i++) {
        auto it = keys[i].empty() ? position.end() : position.find(keys[i]);
        if (it == position.end()) {
            continue;
        }
        if (it->second <= self) {
            if (it->second > beforePosition) {
                before = (int)i;
                beforePosition = it->second;
            }
        } else if (after < 0) {
            after = (int)i;
        }
    }
    if (before < 0) {
        return after;
    }
    int place = before + 1;
    while (place < (int)keys.size() && keys[place].empty() &&
           !buttons.apps[before].empty() &&
           buttons.apps[place] == buttons.apps[before]) {
        place++;
    }
    return place;
}

// ---------------------------------------------------------------------------
// The file
// ---------------------------------------------------------------------------
//
// A text file per user in the mod's storage folder (the storage is shared by
// all the accounts on the computer), one app per line, the order being the
// order of the lines.

constexpr char kFileHeader[] = "taskbar-remember-positions v";
constexpr unsigned kFileVersion = 1;

std::wstring g_userSid;
std::wstring g_orderFilePath;

std::wstring CurrentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return L"";
    }
    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    std::wstring sid;
    if (needed) {
        std::vector<BYTE> buffer(needed);
        if (GetTokenInformation(token, TokenUser, buffer.data(), needed,
                                &needed)) {
            PWSTR sidString = nullptr;
            if (ConvertSidToStringSidW(((TOKEN_USER*)buffer.data())->User.Sid,
                                       &sidString)) {
                sid = sidString;
                LocalFree(sidString);
            }
        }
    }
    CloseHandle(token);
    return sid;
}

enum class LoadResult {
    Loaded,
    // Loaded, but with lines that couldn't be read, which the next save would
    // lose.
    LoadedWithLoss,
    // Written by a newer version of the mod: left as it is.
    Newer,
    // Not a file this mod can read.
    BadFormat,
    // Couldn't be read now.
    ReadError,
};

// Reads the order into `order`; a missing file is an empty order.
LoadResult LoadOrderFile(std::vector<Entry>& order) {
    order.clear();
    // Not shared for writing: a handle open for writing may be a save in
    // progress, truncated and not yet written, which nothing in the bytes can
    // tell from a finished file. Such a file is read again later.
    HANDLE file = CreateFileW(g_orderFilePath.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        // A missing folder gives ERROR_PATH_NOT_FOUND.
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                   ? LoadResult::Loaded
                   : LoadResult::ReadError;
    }
    BY_HANDLE_FILE_INFORMATION before;
    if (!GetFileInformationByHandle(file, &before)) {
        CloseHandle(file);
        return LoadResult::ReadError;
    }
    // The mod writes at most 256 keys of at most 1024 characters.
    if (before.nFileSizeHigh || before.nFileSizeLow > 1024 * 1024) {
        CloseHandle(file);
        return LoadResult::BadFormat;
    }
    std::string data(before.nFileSizeLow, '\0');
    DWORD read = 0;
    BY_HANDLE_FILE_INFORMATION after;
    bool ok = (data.empty() || ReadFile(file, data.data(), (DWORD)data.size(),
                                        &read, nullptr)) &&
              GetFileInformationByHandle(file, &after) &&
              after.nFileSizeLow == before.nFileSizeLow &&
              after.nFileSizeHigh == before.nFileSizeHigh &&
              CompareFileTime(&after.ftLastWriteTime,
                              &before.ftLastWriteTime) == 0;
    CloseHandle(file);
    if (!ok || read != data.size()) {
        return LoadResult::ReadError;
    }

    // Edited by hand, it may have gained a byte order mark, blank lines, and
    // spaces around the fields or in place of the tab.
    size_t position = data.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    bool header = true;
    bool lost = false;
    DWORD today = Today();
    std::unordered_set<std::wstring> seen;
    while (position < data.size()) {
        size_t end = data.find('\n', position);
        if (end == std::string::npos) {
            end = data.size();
        }
        std::string line = data.substr(position, end - position);
        position = end + 1;
        size_t first = line.find_first_not_of(" \t");
        size_t last = line.find_last_not_of(" \t\r");
        if (first == std::string::npos || last == std::string::npos ||
            last < first) {
            continue;
        }
        line = line.substr(first, last - first + 1);
        if (header) {
            size_t prefix = sizeof(kFileHeader) - 1;
            if (line.compare(0, prefix, kFileHeader) != 0 ||
                line.size() == prefix ||
                line.find_first_not_of("0123456789", prefix) !=
                    std::string::npos) {
                return LoadResult::BadFormat;
            }
            unsigned long version = strtoul(line.c_str() + prefix, nullptr, 10);
            if (version != kFileVersion) {
                return version > kFileVersion ? LoadResult::Newer
                                              : LoadResult::BadFormat;
            }
            header = false;
            continue;
        }
        // <day the app was last seen> TAB <app key>
        size_t digits = line.find_first_not_of("0123456789");
        size_t keyStart = digits == std::string::npos
                              ? std::string::npos
                              : line.find_first_not_of(" \t", digits);
        if (digits == 0 || digits == std::string::npos ||
            (line[digits] != '\t' && line[digits] != ' ') ||
            keyStart == std::string::npos) {
            lost = true;
            continue;
        }
        unsigned long lastSeen = strtoul(line.c_str(), nullptr, 10);
        int wideLength = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, line.data() + keyStart,
            (int)(line.size() - keyStart), nullptr, 0);
        std::wstring appId(wideLength > 0 ? wideLength : 0, L'\0');
        if (wideLength > 0) {
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                line.data() + keyStart,
                                (int)(line.size() - keyStart), appId.data(),
                                wideLength);
        }
        // Put through the same rules as a key read from the taskbar, so a
        // line edited by hand can't hold a key the taskbar would never match.
        std::wstring key = KeyFromAppId(appId.c_str());
        if (key.empty()) {
            lost = true;
            continue;
        }
        if (seen.insert(key).second) {
            // A day still to come (a clock that was ahead, or a typo) would
            // keep the app from ever being forgotten.
            order.push_back(
                Entry{key, (DWORD)std::min<unsigned long>(lastSeen, today)});
            // Enough to fill the list; the rest of a file this long can only
            // be noise.
            if (order.size() >= kMaxApps * 4) {
                lost = true;
                break;
            }
        }
    }
    PruneOrder(order, {});
    return lost ? LoadResult::LoadedWithLoss : LoadResult::Loaded;
}

// The folder with the file, created readable only by its user (and SYSTEM
// and the administrators): Windhawk's storage folder is open to every account
// on the computer. Only that folder is created. The storage folder itself
// missing means the mod was removed, and then nothing is saved.
enum class UserFolder { Ready, Gone, Failed };

UserFolder EnsureUserFolder() {
    std::wstring folder =
        g_orderFilePath.substr(0, g_orderFilePath.rfind(L'\\'));
    std::wstring sddl = L"D:P(A;OICI;FA;;;" + g_userSid +
                        L")(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, FALSE};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        attributes.lpSecurityDescriptor = descriptor;
    }
    BOOL created = CreateDirectoryW(folder.c_str(), &attributes);
    DWORD error = created ? ERROR_SUCCESS : GetLastError();
    if (descriptor) {
        LocalFree(descriptor);
    }
    if (created || error == ERROR_ALREADY_EXISTS) {
        return UserFolder::Ready;
    }
    SetLastError(error);
    return error == ERROR_PATH_NOT_FOUND ? UserFolder::Gone
                                         : UserFolder::Failed;
}

bool SaveOrderFile(const std::vector<Entry>& order) {
    std::string data =
        std::string(kFileHeader) + std::to_string(kFileVersion) + "\r\n";
    for (const auto& entry : order) {
        int length = WideCharToMultiByte(CP_UTF8, 0, entry.key.data(),
                                         (int)entry.key.size(), nullptr, 0,
                                         nullptr, nullptr);
        std::string key(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, entry.key.data(), (int)entry.key.size(),
                            key.data(), length, nullptr, nullptr);
        data += std::to_string(entry.lastSeen) + "\t" + key + "\r\n";
    }

    switch (EnsureUserFolder()) {
        case UserFolder::Ready:
            break;
        case UserFolder::Gone:
            Wh_Log(L"The mod's storage folder is gone: not saving");
            return true;
        case UserFolder::Failed:
            Wh_Log(L"Couldn't create the folder for %s: %u",
                   g_orderFilePath.c_str(), GetLastError());
            return false;
    }

    std::wstring temporary = g_orderFilePath + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        Wh_Log(L"Couldn't create %s: %u", temporary.c_str(), GetLastError());
        return false;
    }
    DWORD written = 0;
    bool ok = WriteFile(file, data.data(), (DWORD)data.size(), &written,
                        nullptr) &&
              written == data.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), g_orderFilePath.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Wh_Log(L"Couldn't save the order: %u", GetLastError());
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

struct LoadOutcome {
    bool saveBlocked;
    // Couldn't be read at all: tried again before each save.
    bool loadPending;
};

// What a load that got an answer means for saving, and what's done with a file
// that couldn't be read whole.
LoadOutcome SettleLoad(LoadResult result, std::vector<Entry>& order) {
    // A copy set aside is never overwritten: each gets the time in its name.
    // The other Explorer process may set the same file aside the same second,
    // and then the copy is already there.
    SYSTEMTIME now;
    GetLocalTime(&now);
    WCHAR stamp[32];
    swprintf(stamp, ARRAYSIZE(stamp), L".%04u%02u%02u-%02u%02u%02u.bad",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
             now.wSecond);
    std::wstring aside = g_orderFilePath + stamp;
    switch (result) {
        case LoadResult::Loaded:
            break;
        case LoadResult::LoadedWithLoss:
            // Copied aside, then rewritten at once with the lines that were
            // read, so the next load doesn't copy it again. Not left for a
            // later save: an Explorer process with no taskbar would write this
            // snapshot over the taskbar's newer order when the mod unloads.
            if (CopyFileW(g_orderFilePath.c_str(), aside.c_str(), TRUE)) {
                Wh_Log(L"Some lines of %s couldn't be read: copied it to %s",
                       g_orderFilePath.c_str(), aside.c_str());
                if (!SaveOrderFile(order)) {
                    Wh_Log(L"Couldn't rewrite %s", g_orderFilePath.c_str());
                }
            } else if (GetLastError() != ERROR_FILE_EXISTS) {
                Wh_Log(L"Some lines of %s couldn't be read, and it couldn't "
                       L"be copied: not overwriting it",
                       g_orderFilePath.c_str());
                return {true, false};
            }
            break;
        case LoadResult::Newer:
            Wh_Log(L"%s is from a newer version of the mod: not overwriting it",
                   g_orderFilePath.c_str());
            return {true, false};
        case LoadResult::BadFormat:
            // Put aside rather than lost, and the mod starts afresh.
            order.clear();
            if (MoveFileExW(g_orderFilePath.c_str(), aside.c_str(), 0) ||
                GetLastError() == ERROR_FILE_NOT_FOUND) {
                Wh_Log(L"Couldn't read %s: moved it to %s",
                       g_orderFilePath.c_str(), aside.c_str());
            } else {
                Wh_Log(L"Couldn't read %s or move it aside: not overwriting it",
                       g_orderFilePath.c_str());
                return {true, false};
            }
            break;
        case LoadResult::ReadError:
            Wh_Log(L"Couldn't read %s: %u; not overwriting it, trying again "
                   L"later",
                   g_orderFilePath.c_str(), GetLastError());
            return {true, true};
    }
    return {false, false};
}

// Only one save at a time, so an older order can never be written over a
// newer one.
std::mutex g_saveMutex;

// The file couldn't be read when the mod started: until it can, the order is
// only this session's, and nothing is saved. Tried again before each save,
// with the save lock held; once read, the file's order replaces this
// session's.
void RetryLoad() {
    {
        std::lock_guard<std::mutex> lock(g_orderMutex);
        if (!g_loadPending) {
            return;
        }
    }
    std::vector<Entry> order;
    LoadResult result = LoadOrderFile(order);
    if (result == LoadResult::ReadError) {
        return;
    }
    LoadOutcome outcome = SettleLoad(result, order);
    std::lock_guard<std::mutex> lock(g_orderMutex);
    Wh_Log(L"Read %s at last", g_orderFilePath.c_str());
    g_saveBlocked = outcome.saveBlocked;
    g_loadPending = outcome.loadPending;
    g_order = std::move(order);
    g_orderChanged = false;
}

void SaveNow() {
    std::lock_guard<std::mutex> saving(g_saveMutex);
    RetryLoad();
    std::vector<Entry> order;
    {
        std::lock_guard<std::mutex> lock(g_orderMutex);
        if (!g_orderChanged || g_saveBlocked || g_orderFilePath.empty()) {
            return;
        }
        order = g_order;
        g_orderChanged = false;
    }
    bool saved = SaveOrderFile(order);

    std::lock_guard<std::mutex> lock(g_orderMutex);
    if (saved) {
        g_saveRetries = 0;
        return;
    }
    // Tried again a few times, further apart each time, and then with the
    // next change.
    g_orderChanged = true;
    if (g_saveTimer && g_saveRetries < 5) {
        g_saveRetries++;
        ULARGE_INTEGER due;
        due.QuadPart = (ULONGLONG)(-10LL * g_saveRetries * 10000000LL);
        FILETIME dueTime{due.LowPart, due.HighPart};
        SetThreadpoolTimer(g_saveTimer, &dueTime, 0, 1000);
    }
}

void CALLBACK SaveTimerCallback(PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER) {
    SaveNow();
}

// Saves two seconds after the last change, off the taskbar's thread: a drag
// can move a button several times in a row. Called with the order lock held.
void ScheduleSave() {
    if (!g_orderChanged || !g_saveTimer) {
        return;
    }
    ULARGE_INTEGER due;
    due.QuadPart = (ULONGLONG)(-2LL * 10000000LL);
    FILETIME dueTime{due.LowPart, due.HighPart};
    SetThreadpoolTimer(g_saveTimer, &dueTime, 0, 1000);
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

// Set while _CreateTBGroup or TryMoveGroup runs, for the DPA_InsertPtr hook to
// know which insert is the group's.
struct GroupInsert {
    void* taskGroup;
    bool moving;
    TaskbarKind taskbar;
    // The list the group went into, found by the DPA_InsertPtr hook.
    HDPA buttonGroups;
};

thread_local GroupInsert* t_groupInsert;

// A group the main taskbar has just taken in as an app the mod had never seen.
// Windows gives each taskbar its button in turn, the main one first; the
// others, which may still hold a new pin where Windows put it regardless of
// the rest, leave it at the end too, as the main taskbar did.
thread_local void* t_newOnMainTaskbar;

// A new app's button: before it goes in, the list is read and the order says
// where; after, the order takes in whatever is new on this taskbar.
int InsertNewGroup(HDPA buttonGroups,
                   void* buttonGroup,
                   const GroupInsert& groupInsert) {
    const TaskbarKind& taskbar = groupInsert.taskbar;
    Buttons buttons;
    if (!ReadButtons(buttonGroups, taskbar.showsPinned, buttons)) {
        return DPA_InsertPtr_Original(buttonGroups, DA_LAST, buttonGroup);
    }

    bool pinned;
    std::wstring key =
        TaskGroupKey(groupInsert.taskGroup, taskbar.showsPinned, &pinned);
    int index = DA_LAST;
    {
        std::lock_guard<std::mutex> lock(g_orderMutex);
        // Left where it lands: a pinned app (its place is Windows'), and
        // another button of an app that already has one here, which only
        // Taskbar Grouping makes, and places as its settings say.
        bool alreadyHere = false;
        std::wstring app = AppOfKey(key);
        for (const auto& shown : buttons.apps) {
            alreadyHere = alreadyHere || (!shown.empty() && shown == app);
        }
        bool newOnMainTaskbar = !taskbar.main && taskbar.showsPinned &&
                                t_newOnMainTaskbar == groupInsert.taskGroup;
        // Good for that one round of buttons: another button made on another
        // taskbar (one being rebuilt, say) ends it.
        if (taskbar.main) {
            t_newOnMainTaskbar = !key.empty() && IndexOf(g_order, key) < 0
                                     ? groupInsert.taskGroup
                                     : nullptr;
        } else if (!newOnMainTaskbar) {
            t_newOnMainTaskbar = nullptr;
        }
        if (!key.empty() && !pinned && !alreadyHere && !newOnMainTaskbar) {
            // At Explorer start the pinned apps come first, so by the first
            // other button they're all there to check. Only on the main
            // taskbar: the others can hold a new pin where Windows put it
            // without regard to the rest, until the next drag.
            if (taskbar.main) {
                ReconcilePins(buttons, -1);
            }
            int place = PlaceFor(buttons, key);
            if (place >= 0 && place < (int)buttons.keys.size()) {
                index = place;
            }
        }
    }

    int inserted = DPA_InsertPtr_Original(buttonGroups, index, buttonGroup);
    Wh_Log(L"%s: %d of %d%s", key.c_str(), inserted,
           (int)buttons.keys.size() + 1,
           index == DA_LAST ? L" (as Windows placed it)" : L"");

    if (inserted != -1 &&
        ReadButtons(buttonGroups, taskbar.showsPinned, buttons)) {
        std::lock_guard<std::mutex> lock(g_orderMutex);
        // New pins get their button on the main taskbar this way.
        if (pinned && taskbar.main) {
            LearnPinnedPlace(buttons, FindGroup(buttons, groupInsert.taskGroup));
        }
        TakeInNewApps(buttons.keys);
        ScheduleSave();
    }
    return inserted;
}

int WINAPI DPA_InsertPtr_Hook(HDPA hdpa, int i, void* p) {
    GroupInsert* groupInsert = t_groupInsert;
    if (!groupInsert || g_unloading || !hdpa || groupInsert->buttonGroups) {
        return DPA_InsertPtr_Original(hdpa, i, p);
    }

    // `p` is only looked into for the insert that can be the group's, so no
    // other DPA's items are touched: a new group goes in at the end
    // (DA_LAST), a moved one at its new index. A pinned app's button can also
    // be created with an index of its own, which is Windows' to choose.
    if (groupInsert->moving == (i == DA_LAST) || !IsButtonGroup(p) ||
        CTaskBtnGroup_GetGroup(p) != groupInsert->taskGroup) {
        return DPA_InsertPtr_Original(hdpa, i, p);
    }

    groupInsert->buttonGroups = hdpa;
    if (groupInsert->moving) {
        return DPA_InsertPtr_Original(hdpa, i, p);
    }
    return InsertNewGroup(hdpa, p, *groupInsert);
}

void* WINAPI CTaskListWnd__CreateTBGroup_Hook(void* pThis,
                                              void* taskGroup,
                                              int index) {
    GroupInsert groupInsert{taskGroup, false,
                            KindOfTaskbar(TaskListUIOf(pThis)), nullptr};
    // Any other button made on another taskbar ends the round of the app the
    // main taskbar just took in, pinned ones too (they come with an index of
    // their own, and never reach InsertNewGroup).
    if (!groupInsert.taskbar.main && t_newOnMainTaskbar != taskGroup) {
        t_newOnMainTaskbar = nullptr;
    }
    GroupInsert* outer = t_groupInsert;
    t_groupInsert = &groupInsert;
    void* buttonGroup =
        CTaskListWnd__CreateTBGroup_Original(pThis, taskGroup, index);
    t_groupInsert = outer;
    return buttonGroup;
}

bool WINAPI CTaskListWnd_TryMoveGroup_Hook(void* pThis,
                                           void* taskGroup,
                                           UINT index) {
    t_newOnMainTaskbar = nullptr;
    GroupInsert groupInsert{taskGroup, true, KindOfTaskbar(pThis), nullptr};
    GroupInsert* outer = t_groupInsert;
    t_groupInsert = &groupInsert;
    bool moved = CTaskListWnd_TryMoveGroup_Original(pThis, taskGroup, index);
    t_groupInsert = outer;

    Buttons buttons;
    if (!moved || g_unloading || !groupInsert.buttonGroups ||
        !ReadButtons(groupInsert.buttonGroups,
                     groupInsert.taskbar.showsPinned, buttons)) {
        return moved;
    }

    // Found by its group, not its key: with Taskbar Grouping, several buttons
    // can share one. A button left out of the order (not its app's first, for
    // one) teaches nothing by moving; one that the drag made its app's first
    // has the key now, and is learned.
    int at = FindGroup(buttons, taskGroup);
    Wh_Log(L"%s moved to %d", at >= 0 ? buttons.keys[at].c_str() : L"?", at);

    std::lock_guard<std::mutex> lock(g_orderMutex);
    // On every taskbar: after a drag, Windows puts the others in the same
    // order.
    ReconcilePins(buttons, at);
    if (at >= 0) {
        LearnMove(buttons.keys, at);
    }
    TakeInNewApps(buttons.keys);
    ScheduleSave();
    return moved;
}

// ---------------------------------------------------------------------------
// Mod lifetime
// ---------------------------------------------------------------------------

void LoadOrder() {
    g_userSid = CurrentUserSid();
    WCHAR storage[MAX_PATH * 2];
    size_t length = Wh_GetModStoragePath(storage, ARRAYSIZE(storage));
    if (length == 0 || g_userSid.empty()) {
        Wh_Log(L"No storage path: the order won't be saved");
        return;
    }
    g_orderFilePath =
        std::wstring(storage) + L"\\" + g_userSid + L"\\order.txt";
    Wh_Log(L"Order file: %s", g_orderFilePath.c_str());

    std::vector<Entry> order;
    LoadResult result = LoadResult::ReadError;
    // A file that can't be read at this moment (an antivirus scanning it, the
    // other Explorer process reading it) usually can a moment later.
    for (int attempt = 0; attempt < 3; attempt++) {
        result = LoadOrderFile(order);
        if (result != LoadResult::ReadError || attempt == 2) {
            break;
        }
        Sleep(100);
    }
    LoadOutcome outcome = SettleLoad(result, order);
    g_saveBlocked = outcome.saveBlocked;
    g_loadPending = outcome.loadPending;
    g_order = std::move(order);
}

BOOL Wh_ModInit() {
    Wh_Log(L">");

    // The Windows 11 taskbar. Windows 10 doesn't have this DLL.
    HMODULE taskbarModule = LoadLibraryExW(L"taskbar.dll", nullptr,
                                           LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!taskbarModule) {
        Wh_Log(L"Couldn't load taskbar.dll");
        return FALSE;
    }

    WindhawkUtils::SYMBOL_HOOK taskbarDllHooks[] = {
        {
            {LR"(protected: struct ITaskBtnGroup * __cdecl CTaskListWnd::_CreateTBGroup(struct ITaskGroup *,int))"},
            &CTaskListWnd__CreateTBGroup_Original,
            CTaskListWnd__CreateTBGroup_Hook,
        },
        {
            {LR"(public: virtual bool __cdecl CTaskListWnd::TryMoveGroup(struct ITaskGroup *,unsigned int))"},
            &CTaskListWnd_TryMoveGroup_Original,
            CTaskListWnd_TryMoveGroup_Hook,
        },
        {
            {LR"(public: virtual int __cdecl CTaskListWnd::IsOnPrimaryTaskband(void))"},
            &CTaskListWnd_IsOnPrimaryTaskband,
        },
        {
            {LR"(public: virtual struct ITaskGroup * __cdecl CTaskBtnGroup::GetGroup(void))"},
            &CTaskBtnGroup_GetGroup,
        },
        {
            {LR"(public: virtual unsigned short const * __cdecl CTaskGroup::GetAppID(void))"},
            &CTaskGroup_GetAppID,
        },
        {
            {LR"(public: virtual unsigned long __cdecl CTaskGroup::GetFlags(void)const )"},
            &CTaskGroup_GetFlags,
        },
        {
            {LR"(const CTaskBtnGroup::`vftable'{for `ITaskBtnGroup'})"},
            &CTaskBtnGroup_ITaskBtnGroup_vftable,
        },
        {
            {LR"(const CTaskListWnd::`vftable'{for `ITaskListUI'})"},
            &CTaskListWnd_ITaskListUI_vftable,
        },
    };

    if (!WindhawkUtils::HookSymbols(taskbarModule, taskbarDllHooks,
                                    ARRAYSIZE(taskbarDllHooks))) {
        Wh_Log(L"HookSymbols failed");
        return FALSE;
    }

    if (!WindhawkUtils::SetFunctionHook(DPA_InsertPtr, DPA_InsertPtr_Hook,
                                        &DPA_InsertPtr_Original)) {
        Wh_Log(L"Couldn't hook DPA_InsertPtr");
        return FALSE;
    }

    // taskbar.dll reaches DPA_InsertPtr in the comctl32 of Explorer's manifest
    // (version 6); a different copy here would mean nothing is ever placed.
    HMODULE comctl32 = nullptr;
    WCHAR comctl32Path[MAX_PATH];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (PCWSTR)(void*)DPA_InsertPtr, &comctl32) &&
        GetModuleFileNameW(comctl32, comctl32Path, ARRAYSIZE(comctl32Path))) {
        Wh_Log(L"DPA_InsertPtr from %s", comctl32Path);
    }

    LoadOrder();
    g_saveTimer = CreateThreadpoolTimer(SaveTimerCallback, nullptr, nullptr);
    return TRUE;
}

void Wh_ModBeforeUninit() {
    Wh_Log(L">");
    g_unloading = true;
}

void Wh_ModUninit() {
    Wh_Log(L">");

    PTP_TIMER timer;
    {
        std::lock_guard<std::mutex> lock(g_orderMutex);
        timer = g_saveTimer;
        g_saveTimer = nullptr;
    }
    if (timer) {
        SetThreadpoolTimer(timer, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(timer, TRUE);
        CloseThreadpoolTimer(timer);
    }
    SaveNow();
}
