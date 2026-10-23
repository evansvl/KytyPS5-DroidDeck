# KytyPS5 for DroidDeck

A DroidDeck-focused distribution of [KytyPS5](https://github.com/KytyPS5/KytyPS5) with a custom launcher designed for Android, touch screens, controllers, and DroidDeck's GitHub AppImage import flow.

This project packages the official KytyPS5 Linux x86-64 release as a DroidDeck-compatible AppImage and adds a simpler frontend for managing and launching games on Android.

> This repository does **not** implement its own PlayStation 5 emulator. Emulation is provided by the upstream KytyPS5 project. The custom code in this repository is focused on DroidDeck integration, packaging, storage access, and a mobile-friendly launcher.

## Projects

### KytyPS5

KytyPS5 is an open-source PlayStation 5 translation layer for Windows, Linux, and macOS.

Upstream repository:

https://github.com/KytyPS5/KytyPS5

### DroidDeck

DroidDeck provides a Linux gaming environment for Android devices and can run x86-64 Linux applications on ARM64 Android hardware through FEX.

DroidDeck repository:

https://github.com/Droid-Deck/DroidDeck

## What This Repository Adds

This repository automatically packages official KytyPS5 Linux x86-64 releases into an x86-64 Type 2 AppImage that DroidDeck can import directly from GitHub.

It also includes a custom **KytyPS5 DroidDeck Launcher** intended to replace most interaction with the desktop-oriented upstream launcher.

The custom launcher currently provides:

- automatic game-library scanning
- a fixed Android-friendly game folder
- game cards with `sce_sys/icon0.png` artwork when available
- one-tap game launching
- resolution selection
- Vulkan present-mode selection
- fullscreen toggle
- performance-overlay toggle
- skip-notices toggle
- access to the original KytyPS5 launcher through **Advanced Kyty**

The resulting stack is:

```text
Android device
    ↓
DroidDeck
    ↓
FEX x86-64 translation
    ↓
KytyPS5 DroidDeck Launcher
    ↓
kyty_emulator
    ↓
PS5 game / homebrew
```

## Installation in DroidDeck

You do not need to download the AppImage manually.

Open DroidDeck and go to:

```text
Desktop
→ Add
→ GitHub
```

Enter:

```text
evansvl/KytyPS5-DroidDeck
```

DroidDeck will detect the latest compatible AppImage release and import it automatically.

After the import finishes, launch **KytyPS5** from DroidDeck.

## Adding Games

The custom launcher uses this directory on Android internal storage:

```text
/storage/emulated/0/KytyPS5
```

In a normal Android file manager this is simply:

```text
Internal storage/KytyPS5
```

Create the folder if it does not already exist, then place each game in its own directory.

Example:

```text
Internal storage/
└── KytyPS5/
    ├── PPSA00001/
    │   ├── eboot.bin
    │   ├── sce_sys/
    │   │   └── icon0.png
    │   └── ...
    │
    └── MyHomebrew/
        ├── eboot.bin
        ├── sce_sys/
        │   └── icon0.png
        └── ...
```

The important part is that the game directory contains:

```text
eboot.bin
```

The launcher recursively scans `Internal storage/KytyPS5` for `eboot.bin` files. Every detected game is added to the library automatically.

If this file exists:

```text
<Game>/sce_sys/icon0.png
```

it is used as the game's artwork.

After copying a new game to the folder, press **Refresh** in the launcher.

## Launching a Game

Once a game is detected, press **Play** on its card.

The custom launcher runs the upstream emulator directly using KytyPS5's command-line interface, approximately equivalent to:

```bash
kyty_emulator --game "/storage/emulated/0/KytyPS5/MyGame"
```

Selected launcher settings are added as KytyPS5 command-line arguments.

This avoids the desktop file-picker workflow that is inconvenient inside DroidDeck on Android.

## Advanced Kyty

The **Advanced Kyty** button opens the original upstream `kyty_launcher`.

Use it when you need functionality that has not yet been exposed in the DroidDeck launcher.

For normal game launching, the custom DroidDeck launcher is the recommended interface.

## Storage Shortcuts

The AppImage startup script may also expose convenient paths inside the DroidDeck Linux environment:

```text
/root/PhoneStorage  → /storage/emulated/0
/root/Downloads     → /storage/emulated/0/Download
/root/Games         → /storage/emulated/0/KytyPS5
```

These exist mainly for compatibility with desktop Linux applications. The custom launcher accesses the Android game directory directly, so users normally do not need to interact with these paths.

## Architecture

The AppImage distributed by this repository is:

```text
x86_64
```

KytyPS5 itself is also x86-64.

On ARM64 Android devices, DroidDeck runs the AppImage through FEX.

Therefore the execution path is approximately:

```text
ARM64 Android
    ↓
DroidDeck Linux environment
    ↓
FEX
    ↓
x86-64 KytyPS5
```

The DroidDeck launcher is bundled into the same x86-64 AppImage so it runs inside the same environment as KytyPS5.

## Updates

Releases are generated from official KytyPS5 Linux x86-64 releases.

When a new supported upstream KytyPS5 release is published, the GitHub Actions workflow can automatically create a corresponding DroidDeck AppImage release.

DroidDeck packaging revisions use tags similar to:

```text
KytyPS5-2026-10-23-9dcff95-droiddeck.5
```

This allows packaging and launcher fixes to be released without requiring a new upstream KytyPS5 version.

## Compatibility

This project is experimental.

KytyPS5 was not originally designed for Android or DroidDeck, and running it through FEX introduces another translation layer.

Potential issues include:

- x86-64 to ARM64 translation problems
- virtual-memory/address-space conflicts
- Vulkan driver compatibility
- missing Vulkan extensions or features
- Turnip/Adreno-specific issues
- window-system or Qt issues
- controller/input problems
- crashes or performance problems
- individual game compatibility problems

Successful AppImage installation does **not** guarantee that a particular PS5 game will boot or run correctly.

For upstream emulator compatibility, development, and emulator-specific bug reports, use:

https://github.com/KytyPS5/KytyPS5

## Releases

The generated AppImage is named:

```text
KytyPS5-DroidDeck-x86_64.AppImage
```

A SHA-256 checksum is published alongside it.

For DroidDeck users, the recommended installation method remains:

```text
Desktop
→ Add
→ GitHub
→ evansvl/KytyPS5-DroidDeck
```

## Legal

This repository does not provide PlayStation 5 games, firmware, copyrighted Sony system software, keys, or other copyrighted game content.

Use only games, homebrew, dumps, firmware, keys, and other files that you are legally entitled to use.

KytyPS5 is developed by its respective upstream contributors and remains subject to its upstream license and project terms.

DroidDeck is developed separately by its respective contributors.

This repository is an independent DroidDeck integration and packaging project and is not affiliated with Sony Interactive Entertainment, PlayStation, the KytyPS5 maintainers, or the DroidDeck maintainers.

## Credits

- **KytyPS5:** https://github.com/KytyPS5/KytyPS5
- **DroidDeck:** https://github.com/Droid-Deck/DroidDeck
- **DroidDeck integration and launcher:** https://github.com/evansvl/KytyPS5-DroidDeck
