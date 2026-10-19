# ARMXBOX distribution identity

The public project and release app are named **ARMXBOX**. Android Debug and Profile builds are **ARMXBOX Dev**, with a red Xbox-style DEV icon; production builds use the selected green Xbox-style design. Settings and About read the installed version from BuildConfig, including the variant and versionCode. CI package filenames include version, variant, optional candidate label and build host.

Compatibility identities deliberately remain unchanged: Android application IDs (`com.izzy2lost.x1box` and `.dev`), signing keys, JNI namespaces, `libxemu.so`, preferences, managed storage paths, desktop preference directories, `xemu.toml`, save formats, the Debian package name and the macOS bundle identifier. These preserve updates and existing data. They are implementation identifiers rather than public app names. Upstream copyright and license attribution remains intact.

Desktop distributions contain `armxbox`, `armxbox.exe` or `ARMXBOX.app`. Linux desktop entries and AppStream metadata use the new identity. The Windows update UI opens this fork's releases rather than downloading upstream xemu. Desktop About displays version and Release/Dev, and the main window includes the version.

Original icon masters are in `assets/branding/`. Android adaptive icons inset the full square master to keep the mark within the mask-safe area. Themed icons use an X silhouette, with a separate Dev badge. Native desktop UI icons are embedded from `data/armxbox*.png`; Android carries equivalent packed headers because its build does not run the desktop resource generator.

Before publishing: build all desktop configurations and Android Debug/Profile/Release, check installed labels/version/icon resources, verify production signing identity and update versionCode, and check every release upload glob against the produced filenames. Device launcher appearance and update installation still need a physical-device check.
