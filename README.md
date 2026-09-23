# Taskbar App Memory

A [Windhawk](https://windhawk.net) mod for the Windows 11 taskbar: the apps you
choose go back to their place when they reopen, instead of to the end.

On the Windows 11 taskbar, an app that isn't pinned loses its place when you
close it. Arrange your buttons just so, close an app, open it again, and its
button is at the end. The same happens after a restart: the apps come back in
whatever order they happen to start. This mod remembers where the apps you tick
were, and puts their buttons back there.

## What it does

- **Choose the apps**: right-click an empty part of the taskbar, open
  **Remember positions** (Lembrar posições on a Portuguese Windows), and tick
  the apps whose place should be kept. It lists the open apps that aren't
  pinned (pinned apps stay where Windows puts them anyway), then the ticked apps
  that are closed or pinned since, so you can untick those too. In the mod's
  settings you can have every app remembered instead; the submenu then only
  says so.
- **Arrange the buttons by dragging them**, as usual. The mod remembers where
  you leave the ticked apps.
- **Close a ticked app and open it later**, even after restarting the computer:
  its button goes back between the same neighbours it had.
- **The other apps go where Windows puts them**, as usual. The mod keeps track
  of where they are, since the ticked apps are placed next to them.
- **Pinned apps stay where Windows puts them.** The mod never moves them, it
  only uses them as landmarks: a ticked app you left between two pinned apps
  goes back between them.

## How the place is chosen

The mod keeps one list of every app it has seen on the taskbar, in the order
they were last seen, closed apps included. When a ticked app opens, its button
goes right after the nearest app that comes before it in that list and is on
the taskbar now. When none of those are there, it goes in front of the apps on
the taskbar that the list has.

So the order you arrange is kept whatever order the apps open in, which is what
makes it survive a restart: the apps that start with Windows each go to their
place as they appear. A ticked app's place is certain next to pinned apps and
other ticked apps; next to apps that aren't ticked it can shift, since those go
wherever Windows puts them.

Only what happens while the mod is running is learned. Ticked apps rearranged
while it's disabled go back to their old places when they reopen; drag them
with the mod on to change that. Pinned apps are the exception: when their order
changes without a drag (another program moving a pin, or pins moved while the
mod was off), the mod follows it from the next time a button opens or is
dragged. Until then, an app next to the pin that moved can come back on its
other side if it closes first, or if Explorer restarts (adding a separator with
Taskbar Icon Separators is one such change).

## How it works

Each taskbar keeps its buttons in a list of button groups, one per app, in the
order they're shown. When an app gets a button, `taskbar.dll`
(`CTaskListWnd::_CreateTBGroup`) adds its group at the end of that list with
`DPA_InsertPtr`; when you drag one, `CTaskListWnd::TryMoveGroup` puts it back at
its new index the same way. The mod hooks `DPA_InsertPtr` while either of those
runs:

- a ticked app's group goes in at the index the remembered order gives, instead
  of the end. That index is the one the taskbar's XAML view is then told about,
  so the button simply appears in its place, with no jump;
- after a drag, the order learns where the app now is.

The submenu is added the way the Taskbar Restart Explorer and Taskbar Icon
Separators mods add their items to that menu: while `Taskbar.View.dll` builds it
(`ContextMenus::ShowTaskbarSettingsContextMenu`), in front of the first item it
appends. To list the apps, the mod finds each taskbar's list in its
`CTaskListWnd` as the field that points at a list of button groups, checking
every pointer before following it. Everything else comes from Microsoft's
public symbols, which Windhawk downloads.

## Worth knowing

- **Apps are told apart by their App ID**, the identity Windows uses to put
  windows under one button. An app that changes it is a new app to the mod: a
  browser's new profile, or a portable app moved to another folder.
- **A ticked app you unpin keeps the place it had among the pinned apps**: when
  it opens again, it goes there (with All apps, every app does). Drag it once to
  put it somewhere else. An app that isn't ticked opens at the end, like any
  other.
- **Only the order of the buttons is kept**, not the order of the windows
  inside one button.
- **With several monitors** there is one list for all the taskbars, and each
  taskbar places an app relative to the apps it shows. With "Show my taskbar
  apps on: All taskbars" (the default), Windows keeps the taskbars in the same
  order after each drag. Not after pinning an app from Start, though: the new
  pin goes to the end of the main taskbar but among the pinned apps on the
  others, and until the next drag an app next to it there can come back on its
  other side. With the two "taskbar where window is open" settings, an app can
  have a button on two taskbars but has one place in the list, so arrange the
  apps on the main taskbar (ticking an app shown only on another monitor takes
  its place from that monitor, which can put it on the other side of a pinned
  app on the main taskbar); and on the other taskbars, where the pinned apps
  aren't shown, a pinned app's button is left where Windows puts it.
- **Switching virtual desktops** takes buttons off the taskbar and puts them
  back; the ticked ones come back in the remembered order.
- **With the [Taskbar Grouping](https://windhawk.net/mods/taskbar-grouping)
  mod**, which can give each window a button of its own, a ticked app's first
  button goes back to its place, and the buttons of its other windows go where
  Taskbar Grouping's settings say. Their places aren't remembered: an app that
  sat right next to another app's extra-window buttons can come back on their
  far side, and dropping an app between another app and that app's extra windows
  isn't kept. When the first window closes, Taskbar Grouping passes its identity on
  to another of the app's windows, and that window's button becomes the app's
  place.
- **If the submenu doesn't appear** (the mod's log says "No menu"), choose All
  apps in the settings, or, with the mod disabled, change the `-` to `r` on the
  app's line in `order.txt`.

## Where the list is kept

| Path | What |
| --- | --- |
| `%ProgramData%\Windhawk\Engine\ModsWritable\mod-storage\<mod folder>\<your SID>\order.txt` | the remembered order and the ticks, one app per line |

The mod folder is `taskbar-app-memory` for the mod installed from Windhawk, and
`local@taskbar-app-memory` for one compiled in Windhawk's editor. With Windhawk's logging on, the mod writes the exact path to the log
when it starts.

Windhawk's storage is shared by every account on the computer, so each account
gets a folder of its own, named after its SID (`whoami /user` shows it), which
the mod creates readable only by that account and the administrators.

The file is read when the mod starts and rewritten while it runs. To reset or
edit it, disable the mod, delete or edit the file, then enable the mod again.
Its first line is `taskbar-app-memory v2`, and each line after it is one app, in order, with tabs between: the day it was last seen, `r` if it's
ticked or `-` if not, its App ID, and its name. A file the mod can't read is
moved aside as `order.txt.<date>-<time>.bad`; one it can only read in part is
copied there, then rewritten with the lines it could read. Uninstalling the mod
deletes the folder. Past 256 apps, the ones not seen for the longest time are
forgotten, ticked ones last.

## Install

1. Install [Windhawk](https://windhawk.net).
2. Create a new mod, paste
   [`taskbar-app-memory.wh.cpp`](taskbar-app-memory.wh.cpp),
   compile it.
3. Right-click an empty part of the taskbar, open **Remember positions**, and
   tick the apps.

Windows 11 with its own taskbar, tested on 25H2 (build 26200) with four
monitors. With the old taskbar that ExplorerPatcher or StartAllBack bring back,
the mod does nothing. On 21H2, and on 22H2 without recent updates, buttons may
still open at the end.

### Coming from Taskbar Remember Positions

Versions 0.1.0 and 0.2.0 were called Taskbar Remember Positions
(`taskbar-remember-positions`). Windhawk sees the new name as a different mod,
with a storage folder of its own, so install this one and remove the old one.
To keep the order and the ticks, disable both, copy `order.txt` from the old
mod's folder (`...\mod-storage\taskbar-remember-positions\<your SID>\`, or
`local@taskbar-remember-positions`) into the new one's, then enable the new mod;
it reads the old file.

Version 0.1.0 remembered every app. From 0.2.0 no app is ticked at first, and
each app that opens takes the place Windows gives it. To keep 0.1.0's behaviour,
choose **All apps** in the mod's settings; otherwise, tick the apps you want
kept.

## Build check

`compilar.sh` compiles the mod with Windhawk's own compiler and flags, for every
target Windhawk builds it for (x64 and ARM64), without installing anything, to
catch errors and warnings:

```sh
sh compilar.sh taskbar-app-memory.wh.cpp
```

## License

MIT, see [LICENSE](LICENSE).
