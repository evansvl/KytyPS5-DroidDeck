# KytyPS5 for DroidDeck

A DroidDeck-ready AppImage packaging repository for [KytyPS5](https://github.com/KytyPS5/KytyPS5).

This repository exists **only to make KytyPS5 easy to add and launch through DroidDeck using DroidDeck's built-in GitHub AppImage import feature**.

It is **not a fork of KytyPS5**, does not contain a separate emulator implementation, and is not intended to replace the upstream project.

## Projects

### KytyPS5

KytyPS5 is an open-source PlayStation 5 translation layer for Windows, Linux, and macOS.

Upstream repository:

https://github.com/KytyPS5/KytyPS5

All emulator development, compatibility work, bug reports, feature requests, and emulator-related contributions should go to the upstream KytyPS5 repository.

### DroidDeck

DroidDeck provides a Linux gaming environment for Android devices and supports running x86-64 Linux applications through FEX.

DroidDeck repository:

https://github.com/Droid-Deck/DroidDeck

## What This Repository Does

KytyPS5 currently provides official Linux x86-64 builds.

This repository automatically takes the official KytyPS5 Linux x86-64 release and packages it as an **x86-64 Type 2 AppImage** that DroidDeck can import directly from GitHub.

The resulting flow is:

```text
KytyPS5 official Linux x86-64 build
                ↓
        AppImage packaging
                ↓
KytyPS5-DroidDeck-x86_64.AppImage
                ↓
       DroidDeck GitHub import
                ↓
               FEX
                ↓
         ARM64 Android device
```

The KytyPS5 source code itself is not modified by this repository unless explicitly stated otherwise.

## Installation in DroidDeck

You do not need to manually download the AppImage.

Open **DroidDeck** and go to:

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

After the import finishes, KytyPS5 should appear in your DroidDeck applications.

## Architecture

The AppImage distributed by this repository is:

```text
x86_64
```

On ARM64 Android devices, DroidDeck runs the x86-64 application through its integrated **FEX** environment.

This means KytyPS5 is **not running natively as an ARM64 application**. The purpose of this repository is to provide native integration with DroidDeck's application import system, while the x86-64 KytyPS5 binaries themselves are executed through FEX.

## Updates

Releases in this repository are generated from official KytyPS5 Linux x86-64 releases.

When a new supported KytyPS5 release becomes available, the packaging workflow can produce a corresponding DroidDeck-compatible AppImage.

If KytyPS5 was installed through DroidDeck using the GitHub import method, DroidDeck can associate the application with this repository for easier update handling.

## Compatibility

This project is experimental.

KytyPS5 was not designed specifically for Android or DroidDeck, and running it through FEX introduces an additional translation layer.

Potential issues may include:

- x86-64 to ARM64 translation problems
- virtual memory/address-space conflicts
- Vulkan driver compatibility
- missing Vulkan extensions or features
- Turnip/Adreno-specific issues
- Qt or window-system issues
- crashes or performance problems
- individual game compatibility problems

Successful AppImage installation does **not** guarantee that KytyPS5 itself, or any particular game, will run correctly on a given Android device.

For normal KytyPS5 compatibility information, use the upstream project:

https://github.com/KytyPS5/KytyPS5

## Releases

The generated release asset is named similarly to:

```text
KytyPS5-DroidDeck-x86_64.AppImage
```

A SHA-256 checksum may also be provided alongside the AppImage.

Manual installation is possible, but the recommended method for DroidDeck users is:

```text
Desktop
→ Add
→ GitHub
→ evansvl/KytyPS5-DroidDeck
```

## Legal

This repository does not provide PlayStation 5 games, firmware, copyrighted Sony system software, keys, or other copyrighted game content.

Use only software and game files that you are legally entitled to use.

KytyPS5 is developed by its respective upstream contributors and is licensed according to the terms provided by the upstream project.

DroidDeck is developed separately by its respective contributors.

This repository is an independent packaging/integration project and is not affiliated with Sony Interactive Entertainment, PlayStation, the KytyPS5 maintainers, or the DroidDeck maintainers.

## Credits

- **KytyPS5:** https://github.com/KytyPS5/KytyPS5
- **DroidDeck:** https://github.com/Droid-Deck/DroidDeck
- **DroidDeck AppImage packaging:** https://github.com/evansvl/KytyPS5-DroidDeck
