# KytyPS5 for DroidDeck

A DroidDeck-focused distribution of [KytyPS5](https://github.com/KytyPS5/KytyPS5) with a custom controller/touch frontend for Android.

This repository packages the official KytyPS5 Linux x86-64 release as a DroidDeck-compatible AppImage and replaces the normal desktop launcher workflow with a frontend designed for handheld use.

> KytyPS5 is the emulator core. The upstream `kyty_launcher` GUI is not required for normal use in this project.

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
kyty_emulator
        ↓
PS5 game / homebrew
```

The custom frontend owns the library, game picker, settings, controller navigation and launch diagnostics. It builds the same command-line options that the upstream KytyPS5 launcher normally passes to `kyty_emulator`.

## Installation

In DroidDeck:

```text
Desktop
→ Add
→ GitHub
→ evansvl/KytyPS5-DroidDeck
```

DroidDeck will import the latest compatible x86-64 AppImage release.

## Controller navigation

The frontend is intended to be fully usable without a mouse.

```text
D-pad / left stick  Navigate
A                   Confirm / activate
B                   Back / return to Library
X                   Next value / toggle focused setting
Y                   Add Game
LB / RB             Previous / next section
View / Select       Refresh library
```

The settings UI is split into:

- **Library**
- **Graphics**
- **System**
- **Controller**
- **Debug**
- **Expert**

## KytyPS5 settings

The frontend exposes KytyPS5 emulator settings directly instead of opening the upstream desktop GUI.

Current settings include:

### Graphics

- resolution: 720p / 1080p / 1440p / 4K
- Vulkan present mode
- GPU index / automatic GPU selection
- fullscreen
- HDR
- cursor hiding
- virtual vblank frequency
- performance overlay
- linear image readback
- tiled image readback
- raw image synchronization
- tessellation
- occlusion queries

### System

- local user name
- local user ID
- microphone device
- console language
- network-connected mode
- trophy notifications
- trophy notification duration
- skip notice screen
- add-ons / DLC directory

### Controller

- DualSense lightbar color
- controller speaker volume
- vibration intensity
- keyboard player assignment
- custom `--keymap` mappings

### Debug

- Vulkan validation
- shader validation
- shader optimization mode
- shader logging destination
- shader logging folder
- command-buffer dumps
- guest printf output
- Tracy profiler
- RenderDoc
- AMD CPU compatibility patch

### Expert

Additional `kyty_emulator` CLI switches are available for workflows that need them, including GPU-assisted validation, SPIR-V debug printf, graphics debug dumps, PlayGo fallback, VR mode and splash control.

Settings are saved persistently by the frontend and converted directly to `kyty_emulator` arguments when a game is launched.

## Adding games

Press **Y** or select **Add Game** in the Library.

The frontend uses a Qt non-native picker because desktop portal/FUSE pickers are not reliable inside DroidDeck.

You can select:

- a folder containing a PS5 game / `eboot.bin`
- `eboot.bin`
- an ELF
- a Kyty `.zar` archive
- a `.zip` archive

ZIP archives are extracted into:

```text
/storage/emulated/0/KytyPS5
```

which is shown by Android file managers as:

```text
Internal storage/KytyPS5
```

You can also place games there manually. The frontend recursively scans the directory for `eboot.bin`.

Example:

```text
Internal storage/
└── KytyPS5/
    └── PPSA99666/
        ├── eboot.bin
        ├── sce_sys/
        │   └── icon0.png
        └── ...
```

If `sce_sys/icon0.png` exists, it is used as the library artwork.

External game folders/files selected through **Add Game** are remembered and do not need to be copied into the default library folder.

## Launching

Selecting **Play** starts the official upstream `kyty_emulator` directly.

Conceptually:

```bash
kyty_emulator \
  --game "/path/to/game" \
  --screen-width 1280 \
  --screen-height 720 \
  --present-mode Mailbox \
  ...
```

The frontend captures stdout/stderr. If the emulator exits or crashes immediately, the error and log are shown in the frontend.

Logs are stored under:

```text
~/.local/share/KytyPS5-DroidDeck/logs
```

## Android storage shortcuts

The AppImage startup script also exposes:

```text
/root/PhoneStorage  → /storage/emulated/0
/root/Downloads     → /storage/emulated/0/Download
/root/Games         → /storage/emulated/0/KytyPS5
```

The custom frontend normally accesses Android storage directly, so users do not need to browse Linux root paths.

## Updates

The workflow tracks official KytyPS5 Linux x86-64 releases and creates DroidDeck packaging releases automatically.

Packaging revisions use tags such as:

```text
KytyPS5-2026-10-23-9dcff95-droiddeck.8
```

This allows frontend and DroidDeck-specific fixes to ship without waiting for a new upstream KytyPS5 build.

## Compatibility

This project is experimental. KytyPS5 was not originally designed to run through FEX on Android.

Known classes of problems include:

- x86-64 → ARM64 translation issues
- guest virtual-memory/address-space reservation failures
- Vulkan/Turnip compatibility
- unsupported Vulkan extensions/features
- individual game compatibility
- performance and thermal limits

A successful AppImage launch does not guarantee that a PS5 title will boot.

## Legal

This repository does not provide PlayStation 5 games, firmware, keys, copyrighted Sony system software, or other copyrighted game content.

Use only content that you are legally entitled to use.

KytyPS5 and DroidDeck are separate upstream projects developed by their respective contributors. This repository is an independent integration and is not affiliated with Sony Interactive Entertainment.
