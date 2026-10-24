# KytyPS5 for DroidDeck

A DroidDeck-focused distribution of [KytyPS5](https://github.com/KytyPS5/KytyPS5) with a custom controller-first frontend for Android handhelds.

This repository packages KytyPS5 as a DroidDeck-compatible x86-64 AppImage and replaces the desktop-oriented launcher workflow with a console-style interface designed to be operated from a gamepad.

> KytyPS5 is used as the emulator core. The upstream `kyty_launcher` GUI is not part of the normal user flow.

## Projects

- **KytyPS5:** https://github.com/KytyPS5/KytyPS5
- **DroidDeck:** https://github.com/Droid-Deck/DroidDeck

## Architecture

```text
Android / DroidDeck
        ↓
FEX x86-64 translation
        ↓
KytyPS5 DroidDeck Frontend
        ↓
patched kyty_emulator core
        ↓
PS5 game / homebrew
```

The custom frontend owns the game library, file browser, settings, controller navigation and launch diagnostics. KytyPS5 provides the actual PS5 translation/emulation core.

## Installation

In DroidDeck:

```text
Desktop
→ Add
→ GitHub
→ evansvl/KytyPS5-DroidDeck
```

DroidDeck imports the latest compatible x86-64 AppImage release.

## Console-style UI

The frontend uses a controller-first interaction model inspired by open-source console frontends such as Pegasus.

The Library uses a horizontal game carousel. The entire game card is the focus target, not a small button inside it. The focused card receives a bright cyan outline and the large hero panel above it immediately updates with the selected game's artwork, title and path.

Settings use full-row focus highlighting so it is always clear which option will be changed.

The visual language is deliberately PS5/emulator-oriented: dark navy surfaces, electric blue/cyan focus states, a `K5` launcher mark, Kyty/Vulkan core status and persistent gamepad button hints.

## Controller navigation

```text
D-pad / left stick  Navigate
A                   Launch / confirm / activate
B                   Back / return to Library
X                   Game settings / next focused value
Y                   Add Game
LB / RB             Previous / next section
View / Select       Refresh library
```

Sections:

- **Library**
- **Graphics**
- **System**
- **Controller**
- **Debug**
- **Expert**

## Single-window design

DroidDeck can behave poorly when desktop applications create extra top-level windows. For that reason the frontend intentionally keeps normal interaction inside one `QMainWindow`.

The following are internal pages of the same launcher window:

- game library
- settings
- Add Game browser
- ZIP import status
- launch errors
- Kyty core logs

The frontend does **not** use `QFileDialog` or `QMessageBox` for its normal workflows.

The only separate window expected during normal use is the actual game window created by the emulator core after a successful launch.

## Adding games

Press **Y** or focus the **+ Add Game** card.

A built-in file browser opens inside the launcher itself. It starts in Android internal storage and can be operated entirely with the controller.

```text
A   Open folder / add selected game
B   Parent folder / back
X   Add current folder when eboot.bin is present
Y   Jump to Internal storage root
```

Supported inputs:

- game folder containing `eboot.bin`
- `eboot.bin`
- ELF
- Kyty `.zar`
- `.zip`

ZIP archives are extracted into:

```text
/storage/emulated/0/KytyPS5
```

Android file managers display this as:

```text
Internal storage/KytyPS5
```

Games can also be placed there manually:

```text
Internal storage/
└── KytyPS5/
    └── PPSA99666/
        ├── eboot.bin
        ├── sce_sys/
        │   └── icon0.png
        └── ...
```

`sce_sys/icon0.png` is used as library artwork when present.

External game folders and supported direct files added through the built-in browser are remembered persistently.

## KytyPS5 settings

The frontend exposes Kyty emulator settings directly and converts them to `kyty_emulator` command-line arguments.

### Graphics

- resolution: 720p / 1080p / 1440p / 4K
- Vulkan present mode
- GPU index / automatic GPU selection
- fullscreen
- HDR
- cursor hiding
- virtual VBlank frequency
- performance overlay
- linear/tiled image readback
- raw image synchronization
- tessellation
- occlusion queries

### System

- user name and user ID
- console language
- microphone device
- network-connected mode
- trophy notifications and duration
- skip notice screen
- add-ons / DLC path

### Controller

- DualSense lightbar color
- speaker volume
- vibration intensity
- keyboard player assignment
- custom `--keymap` mappings

### Debug / Expert

- Vulkan and shader validation
- shader optimization and logging
- command-buffer dumps
- guest printf output
- Tracy profiler
- RenderDoc
- AMD CPU compatibility patch
- GPU-assisted validation
- SPIR-V debug printf
- graphics debug dumps
- PlayGo fallback
- VR and splash options

Settings are stored persistently.

## Launch diagnostics

The frontend starts `kyty_emulator` directly and captures stdout/stderr.

If the core exits early, the launcher returns to an internal error page in the same window and shows the captured log there.

Logs are also stored under:

```text
~/.local/share/KytyPS5-DroidDeck/logs
```

## DroidDeck / FEX memory patch

The stock Linux KytyPS5 core creates a sparse 13.5 GiB `memfd` for PS5 direct memory and then maps the entire backing store again as one contiguous writable alias.

That extra 13.5 GiB alias fails when the x86-64 Kyty process is running through FEX on DroidDeck, producing an error similar to:

```text
could not reserve 13824 MB for guest direct memory
```

DroidDeck packaging revision r10 introduces an experimental core patch for this environment:

- keep the original sparse `memfd` backing store;
- do not create the extra contiguous 13.5 GiB writable alias;
- perform CPU-side backing reads/writes using `pread` / `pwrite`;
- retain Kyty's real fixed guest mappings to the same `memfd`.

This patch is specifically intended to get past the FEX address-space failure. It is experimental and may expose additional Kyty/FEX compatibility issues after the original blocker is removed.

## Android storage shortcuts

The AppImage also exposes:

```text
/root/PhoneStorage  → /storage/emulated/0
/root/Downloads     → /storage/emulated/0/Download
/root/Games         → /storage/emulated/0/KytyPS5
```

Normal users do not need to interact with these Linux paths because the custom browser accesses Android storage directly.

## Updates

The workflow tracks official KytyPS5 releases and creates DroidDeck packaging revisions automatically.

Tags use the form:

```text
KytyPS5-<upstream release>-droiddeck.<revision>
```

DroidDeck-specific frontend and core compatibility fixes can therefore ship without waiting for another upstream release.

## Compatibility

This project is experimental. Running a PS5 translation layer through an additional x86-64 → ARM64 FEX layer introduces constraints that do not exist on normal desktop Linux.

Potential blockers include:

- guest virtual-memory/address-space mappings
- FEX syscall and memory semantics
- Vulkan / Turnip compatibility
- unsupported Vulkan extensions or features
- individual game compatibility
- performance and thermal limits

A successful AppImage launch does not guarantee that every PS5 title will boot.

## Legal

This repository does not provide PlayStation 5 games, firmware, keys, copyrighted Sony system software or other copyrighted game content.

Use only content that you are legally entitled to use.

KytyPS5 and DroidDeck are separate upstream projects developed by their respective contributors. This repository is an independent integration and is not affiliated with Sony Interactive Entertainment.
