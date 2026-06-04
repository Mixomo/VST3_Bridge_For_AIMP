# VST3 Bridge for AIMP

VST3 Bridge for AIMP is a Windows DSP plug-in that hosts a single VST3 effect inside AIMP's DSP pipeline. It is intended for users who want to run a selected VST3 processor while playing audio through AIMP, including ASIO output.

VST3 plug-ins run in the separate `VST3BridgeHost.exe` process. A plug-in crash therefore terminates the helper instead of AIMP; the DSP bridge falls back to bypass and the helper can be restarted by reopening the bridge configuration.

Developed by Ezequiel Casas (Mixomo): https://github.com/Mixomo

![dsp_plugin](assets/dsp.png)

![VST3_Bridge_with_CalCurve_vst3_loaded](assets/dsp2.png)

Featured VST3: [CalCurve by Mixomo](https://github.com/Mixomo/CalCurve) 

## Current Status

This bridge is experimental. It works well with some VST3 effects, but universal VST3 compatibility is not guaranteed. Some commercial plug-ins assume behavior from full DAWs that AIMP's Winamp-style DSP pipeline cannot provide safely in-process.

The bridge currently prioritizes stability:

- Manual VST3 selection only, no plug-in folder scanning.
- One active VST3 at a time.
- Out-of-process VST3 audio processing and GUI hosting.
- Pipelined shared-memory audio IPC that never waits for the helper from AIMP's DSP callback.
- The helper runs at normal process priority without realtime scheduling, avoiding competition with AIMP's ASIO thread.
- Automatic bypass if the helper crashes, stops responding, or is temporarily starved by the system.
- Preserves AIMP-provided sample rate and PCM bit depth where possible.
- Supports float/double internal VST3 processing through JUCE.
- Saves the last working VST3 and its state.
- If a VST3 fails or throws during audio processing, the bridge unloads it, removes it from the internal dropdown, and enters bypass.

## Download / Install

The ready-to-install AIMP package is:

```text
dist/dsp_vst3_bridge.aimppack
```

Install it from AIMP's plug-in manager, or open the `.aimppack` with AIMP. After installation, enable `VST3 Bridge - Mixomo` from AIMP's DSP plug-ins selector.

## Build Requirements

- Windows 10/11 x64
- Visual Studio 2022 with C++ desktop workload
- CMake 3.22 or newer
- Windows PowerShell, used by the packaging target
- AIMP 5.x

The repository vendors the local dependencies needed to build:

- `third_party/JUCE`
- `third_party/aimp-sdk`

## Build

From the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target package_aimppack
```

The `package_aimppack` target builds both the lightweight AIMP DSP DLL and the out-of-process VST3 host, then invokes `cmake/PackageAimpPack.ps1` to create the `.aimppack` ZIP package with PowerShell `Compress-Archive`.

The package will be regenerated at:

```text
dist/dsp_vst3_bridge.aimppack
```

The DLL inside the package is laid out for AIMP as:

```text
dsp_vst3_bridge/x64/dsp_vst3_bridge.dll
dsp_vst3_bridge/x64/VST3BridgeHost.exe
```

`dsp_vst3_bridge.dll` runs inside AIMP and only handles the DSP callback and IPC. `VST3BridgeHost.exe` owns JUCE, loads the selected VST3, processes audio through shared memory, and displays the VST3 editor. Audio IPC is asynchronous and adds two AIMP DSP blocks of latency so the AIMP/ASIO feeding thread never blocks waiting for another process. If the helper crashes, stops responding, or is temporarily starved by the system, AIMP continues in dry bypass until processing recovers.

## Notes for Users

- Close AIMP before replacing or reinstalling the plug-in.
- Use the bridge configuration window to load one `.vst3` explicitly.
- Avoid using folder scanners against large VST3 collections; this project intentionally does not include scanning.
- If a VST3 disappears from the bridge dropdown after a failure, reload it manually only if you are testing compatibility.

## Logs

Runtime logs are written to the Windows temp folder:

```text
%TEMP%\dsp_vst3_bridge.log
```

## Licensing

VST3 Bridge for AIMP is licensed under the GPL-3.0 license. See `LICENSE`.

This repository also vendors third-party code required to build the bridge:

- JUCE: the JUCE Framework modules are dual-licensed under AGPLv3 and the commercial JUCE 8 license. See `third_party/JUCE/LICENSE.md`.
- JUCE examples: ISC license. See `third_party/JUCE/LICENSE.md`.
- JUCE bundled dependencies: see the dependency list in `third_party/JUCE/LICENSE.md`. Notable dependencies used by JUCE include the Steinberg VST3 SDK under MIT terms at `third_party/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt`.
- AIMP SDK: original SDK files and documentation are included in `third_party/aimp-sdk`.

Build/tooling credits:

- Microsoft MSVC / Visual Studio C++ toolchain: https://microsoft.com/
- C++ language created by Bjarne Stroustrup.

Review the vendored third-party licenses before redistributing binaries or source releases. If you do not use a commercial JUCE license, your use of JUCE is governed by its AGPLv3 terms.
