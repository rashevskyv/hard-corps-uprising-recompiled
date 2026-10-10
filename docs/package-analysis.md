# Hard Corps Uprising XBLA Package Analysis

Technical metadata only. No game data is stored in this repository; all original and extracted files live under the Git-ignored `game/` directory.

## Original package

| Field | Value |
|---|---|
| Path | `game/58410A18/000D0000/76AB6844A70515BCCE4BA43C2854DB3A5E602BF658` |
| Size | 1,036,054,528 bytes |
| SHA-256 | `53DF4058709E7DD2156AEADABB71B9A620D47D1EA57D827567ED3F75D3A9D1DE` |
| SHA-1 | `F57B6C6207F660FC4F7D1F860F8DAAEEA00F8CE2` |
| File timestamp | 2011-02-16 13:33:02 UTC (last write) |
| Format | Xbox 360 STFS, magic `LIVE` (Live-signed content package) |
| Metadata version | 2 (header size `0xAD0E`, first hash table at `0xB000`) |
| Content type | `0x000D0000` (Arcade Title) |
| Content ID | `76AB6844A70515BCCE4BA43C2854DB3A5E602BF6` (first 40 hex digits of the file name) |
| Title ID | `0x58410A18` |
| Media ID | `0x3C5663CB` |
| Version / base version | `0x00000003` / `0x00000003` (0.0.0.3) |
| Display name | `Hard Corps: Uprising` (EN, JA, DE, FR, ES, IT slots) |
| Title name | `Hard Corps: Uprising` |
| Publisher field | empty |
| Description | placeholder text `good game.(<language>)` in the same six slots |
| Platform / executable type / disc | 0 / 0 / 0 of 0 |
| License entries | 1 entry: unrestricted ID (all `0xFF`), license bits `0x00000001` |
| Console / profile / device ID | all zero (not bound to a console or profile) |
| Transfer flags | `0xC0` |
| Thumbnails | 10,167-byte package and title thumbnails (not extracted) |
| Region | not stored in STFS; the XEX allows all regions (`0xFFFFFFFF`) |

Title ID `0x58410A18` is confirmed by three independent sources: the STFS header, the XEX execution info and `ArcadeInfo.xml` (`TitleInfo ID="1480657432"`).

STFS volume: read-only layout (block separation 1), three-level hash tree, 251,442 allocated / 0 free blocks, file table of 131 blocks starting at block 0.

Integrity checks (read-only):

- Header SHA-1 (`0x344`–`0xB000`) equals the Content ID.
- Top-level hash, all level-2/level-1 hash tables, and all 251,442 data blocks match their SHA-1 entries (0 mismatches).
- The Microsoft RSA signature was not verified.
- Package SHA-256, size and timestamp were identical before and after extraction.

## Extracted package

- Tool: `tools/extract_stfs.ps1` + `tools/StfsExtract.cs` from `earthworm-jim-hd-recompiled` (identical to the `outpost-kaloki-x-recompiled` copy), run from the reference checkout. No tool code was copied into this repository.
- Output: `game/extracted/`. 8,345 files and 27 directories, 1,005,963,779 bytes total. All file-table entries are contiguous (none fragmented).
- Verification: a separate STFS reader, using the verified hash tree, produced the same SHA-256 for all 8,345 extracted files.

Root:

| File | Size | Notes |
|---|---|---|
| `default.xex` | 12,955,648 | Only executable module in the package |
| `ArcadeInfo.xml` | 3,238 | UTF-16 XBLA metadata: 6 locales, 12 achievements, 1 leaderboard |
| `nxeart` | 954,368 | Nested STFS `LIVE` package (dashboard art) |

`DataXENON/`:

| Directory | Files | Size | Contents |
|---|---:|---:|---|
| `Object` | 1,117 | 341.8 MiB | `.igb` (389), `.bin` (728) |
| `Gui` | 6,212 | 182.2 MiB | `.bin` images/layouts, `.pla`, `Img/charsel.pak` |
| `Movie` | 4 | 161.1 MiB | `.wmv` (ASF): `Opening`, `EndingCommon`, `EndingBH`, `EndingCL` |
| `Sound` | 17 | 156.9 MiB | `.bnk` with `FSB4` magic: BGM, per-stage SE, voice banks |
| `Level` | 489 | 90.3 MiB | `.igb` (408), `.csv` (81) |
| `Particle` | 413 | 4.9 MiB | `.csv` |
| `Cockpit` | 48 | 4.7 MiB | `.bin` |
| `Event` | 8 | 2.7 MiB | `.igb` |
| `Network` | 18 | 1.2 MiB | `.png`, `.jpg` |
| `Shader` | 11 | 0.1 MiB | 6 vertex + 4 pixel shader `.bin`, `aspShaders.pkg` |
| `Font` | 2 | <0.1 MiB | `.bin`, `.pla` |
| `Save` | 1 | <0.1 MiB | `.png` |
| (root) | 2 | 0.1 MiB | `LoadList.csv`, `PTK_id_list.csv` |

Container and format markers:

- `.igb`: Alchemy IGB marker `0xFADA` at offset `0x28` (little-endian header).
- `.bnk`: FMOD Ex sound banks (`FSB4`), despite the `.bnk` extension.
- `.wmv`: ASF/WMV video.
- `Shader/*.bin`: Xbox 360 compiled shader containers (`0x102A1101` vertex, `0x102A1100` pixel). `aspShaders.pkg` uses the `spkg` magic.
- `.pak`: `FPAC` magic. `.pla`: `PLAF` magic.
- Localization: `Gui/Img/{DE,EN,ES,FR,IT,JP}` (737–763 files each) and `Gui/Xml/{DE,EN,ES,FR,IT,JP}` (183–191 files each).
- GUI content includes trial-related assets (for example `1280x720_OnlineNoticeTrial.bin`).

## default.xex

| Field | Value |
|---|---|
| Path | `game/extracted/default.xex` |
| Size | 12,955,648 bytes |
| SHA-256 | `530EDE8ED5271051A2AB19C077A97FB167E4F30B094C292E8F59D37D0D2069DC` |
| Format | XEX2, module flags `0x00000001` (TITLE): main executable, not a DLL or patch |
| Original PE name | `Contra.exe` |
| Encryption | Normal (AES-128, retail key) |
| Compression | Basic: 5 blocks, 12,943,360 data bytes + 688,128 zero-fill bytes |
| Image base / load address | `0x82000000` |
| Image size | `0xD00000` (13,631,488 bytes), 64 KiB pages, image flags `0x00000000` |
| Entry point | `0x82571AD0` (matches PE entry RVA `0x571AD0`) |
| Architecture | PE machine `0x01F2` (PowerPC big-endian, Xenon), subsystem 14 |
| PE timestamp | `0x4CF6314D` (2010-12-01 11:28:13 UTC) |
| Execution info | Title `0x58410A18`, Media `0x3C5663CB`, version and base version 0.0.0.3, disc 0/0 |
| Region | `0xFFFFFFFF` (all regions) |
| Allowed media | `LIVE_SIGNED_PACKAGE`, `XBOX_PACKAGE` |
| System flags | `0x604`: `NO_ODD_MAPPING`, `USES_GAME_VOICE_CHANNEL`, `PAL50_INCOMPATIBLE` |
| Default stack size | `0x40000` |
| TLS | 64 slots, no TLS data |
| Resource | `58410A18` (XDBF title resource) at `0x82CD0000`, `0x2EA50` bytes |
| Other optional headers | LAN key, Xbox 360 logo (1,736 bytes), game ratings (ESRB T, PEGI 12+, CERO B, USK 12+) |
| Exports | none |

Memory layout (64 KiB page descriptors, matching the PE sections):

| Range | Protection | PE sections |
|---|---|---|
| `0x82000000`–`0x82400000` | read-only | headers, `.rdata` (`0x3CE21C`), `.pdata` (`0x25CC8`) |
| `0x82400000`–`0x82BE0000` | code | `.text` (`0x7D49CC`) |
| `0x82BE0000`–`0x82CD0000` | data | `.data` (`0xE7C80`), `.XBMOVIE` (`0xC`) |
| `0x82CD0000`–`0x82D00000` | read-only | XDBF resource (replaces `.idata`/`.XBLD`/`.reloc` in the loaded image) |

Static libraries: XDK 2.0.11775.3 for D3D9, D3DX9, XGRAPHC, XAPILIB, XAUD, XAUDIO2, XAPOBA, XMP, XMCORE, XMIC, XHV2, XNET, XONLINE, XPARTY, LIBCPMT and XBOXKRNL. Compiler C1/C2 16.0.10224, LINK 10.0.10224.

Imports. Both libraries have version 2.0.11775.0 and minimum version 2.0.9199.0:

| Library | Functions | Variables |
|---|---:|---:|
| `xboxkrnl.exe` | 136 | 13 |
| `xam.xex` | 70 | 0 |

- Areas: threading/sync (`Ke*`, `Nt*`, `Rtl*`), file I/O (`NtCreateFile`, `NtReadFile`, `NtQueryDirectoryFile`, …), GPU (`Vd*`, including `VdSwap` and `VdInitializeRingBuffer`), audio (`XAudio*`, `XMACreateContext`/`XMAReleaseContext`), input (`XamInputGetState`/`SetState`/`GetCapabilities`), content and saves (`XamContentCreateEx`, `XamContentGetLicenseMask`, …), networking (`NetDll_*` sockets, `XNet*`), voice (`XamVoice*`), Live UI (`XamShow*UI`), and module APIs (`XexLoadImage`, `XexGetProcedureAddress`, `XexUnloadImage`).
- Coverage in the local `rexglue-sdk` export tables: all 206 imported functions are known. 194 are implemented (`REX_EXPORT`/`REX_HOOK_RAW`) and 12 are stubs: `ObReferenceObject`, `MicDeviceRequest`, `NtCancelIoFile`, `RmcDeviceRequest`, `NetDll_XNetQosLookup`, `XNetLogonGetTitleID`, `XamShowFriendsUI`, `XamShowAchievementsUI`, `XamShowMarketplaceUI`, `XamShowGamerCardUIForXUID`, `XamUserCreateStatsEnumerator`, `XamVoiceSubmitPacket`. All 13 variable imports are referenced by the SDK kernel source.
- No other modules are shipped. The only runtime module dependencies are `xboxkrnl.exe` and `xam.xex`, both provided by the ReXGlue runtime.

Parsing: the image was decrypted (retail key) and expanded (basic compression) with the same algorithm as ReXGlue's `XexModule::ReadImageBasicCompressed`. The resulting PE headers, section table and entry point are consistent. A derived image is kept locally at `game/analysis/default.image.bin` (SHA-256 `4BDA1B29F8882C78754055B5A5FF023603987B17167373E65933A8F776059AC3`). The ReXGlue CLI itself was not run: no prebuilt binary is present locally, and codegen is out of scope for this phase.

## Reference comparison

| Project | Usefulness |
|---|---|
| `outpost-kaloki-x-recompiled` | Closest bootstrap workflow. Its `setup.ps1` runs extract → `default.xex` size/CRC32 pin → `rexglue codegen` on an SDK 0.10.0 manifest (`game_root = "assets"`, `includes`), and it needed no veneer or `setjmp` workarounds. It documents the `license_mask = 1` default (needed because Hard Corps Uprising also imports `XamContentGetLicenseMask`) and an SDK 0.10.0 FP-exception issue in host-thread callbacks. |
| `earthworm-jim-hd-recompiled` | Source of the extractor used here, including its `TitleId` header check. Same `000D0000` package workflow. Its XEX is devkit-key encrypted and uses veneer/`.embsec_` code layout, so its game-specific overrides and `setjmp` patching do not transfer. |
| `simpsonsarcade` | Closest binary profile: Konami XBLA title, base `0x82000000`, identical XDK section set including `.XBMOVIE`. Its workflow differs (XenonRecomp-derived config, LZX-compressed XEX, Python extractor). |
| `rexglue-sdk` | Its `XexModule` loader supports retail AES + basic compression. Its export tables cover every imported function, and it includes an STFS container device. |
| `Aegis-Wing-XBLA-PC-Recomp`, `GoldenEye-007-XBLA-PC-Recomp` | Pure-PowerShell port of Velocity's STFS algorithm (same math, slower). Their manifests show `setjmp_address`/`longjmp_address` usage for a later phase. |
| `NocturneRecomp` | Extractor is hardcoded to its own package layout. Its XEX decrypt/basic-decompress approach matches the one used here. |
| `afterburner` | Minimal SDK 0.8.0 manifest (`game_root = "extracted"`). Nothing package-specific. |
