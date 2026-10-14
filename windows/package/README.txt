DLSSNR-AMD-Vulkan (Windows, experimental preview)
=================================================

Runs the neural rendering (NR) model of DLSS 5 in games on AMD graphics cards. The network is
reimplemented in Vulkan; NVIDIA's runtime is neither needed nor called.

This is an experimental preview and has not been tested much: game crashes, driver resets and
other unexpected problems can happen. It is slower than the Linux version and updated less
often, and some releases may be Linux only. If something breaks, uninstall with option 5 of
install.bat.

DX10/11/12 games keep the system's own D3D; nothing is translated. NR runs on a Vulkan device
of its own on the same graphics card, and the frame is handed between the two through shared
textures and a shared fence, all on the GPU. Only DX9 games are moved to Vulkan with DXVK
(D3D9 cannot share a frame with another API).

Requirements
------------
- RX 9000 series (RDNA4) graphics card, AMD Software 26.9.2 (tested)
- 64-bit games
- Do not use it in online games with anti-cheat; start games with EasyAntiCheat with EAC off

Install
-------
Double-click install.bat, pick the game's exe (the one that actually runs - for Unreal Engine
games it is under Binaries\Win64, not the launcher outside), then pick a route:

  1) optiscaler             the game has a DLSS, FSR or XeSS option (DX11 / DX12, OptiScaler)
  2) reshade-feeder         other DX10/11/12 games (ReShade)
  3) reshade-feeder-dx9     old DX9 games (ReShade)
  4) reshade-feeder-vulkan  Vulkan games (ReShade)
  5) Uninstall
  6) Collect logs           when something goes wrong: writes a zip to the desktop to attach to an
                            issue

The old route names reshade, dx9 and vulkan are now reshade-feeder, reshade-feeder-dx9 and
reshade-feeder-vulkan. On the command line, optiscaler-renodx means optiscaler with the RenoDX style
on.

You can also drag the game's exe onto install.bat. Games under Program Files ask for
administrator rights.

After the route the installer asks for the network, three combinations of two settings in
dlssnr-amd.ini:

  1) default network, compiled by the AMD driver   ACO Mode = 0, Int4Mixed Enabled = 0 (default)
  2) default network, ACO machine code             ACO Mode = 1, Int4Mixed Enabled = 0
  3) int4 mixed, ACO machine code                  ACO Mode = 1, Int4Mixed Enabled = 1

ACO machine code: the network as machine code made in advance by the Linux driver's compiler (ACO),
shipped in the package and loaded by the AMD driver as it is, instead of the driver compiling the
network itself; it is faster than the driver's own compile. Tested on one driver version only (AMD
Software 26.9.2); if it causes problems on another driver, set ACO Mode back to 0.
int4 mixed: part of the network runs in int4 and other lower precisions; faster, the picture differs
somewhat. It runs with ACO machine code only (with ACO Mode = 0 the default network runs). It is
always installed (its weights are made from the model at install); Ctrl+F11 in game switches
between it and the default network, which needs ACO Mode = 1.
Both settings can be changed in dlssnr-amd.ini later without installing again; a change of ACO Mode
takes effect after a game restart.

Last, the installer asks whether to turn on the RenoDX style (see "RenoDX style" below); Enter =
off. On the command line: -RenoDX on or off.

Model
-----
The package does not contain NVIDIA's model. The first install asks for nvngx_dlssnr.dll
(version 310.8.0 only) or a zip that contains it. The installer extracts the model with
model-tools\dlssnr_extract_model.exe (it only reads the weight data; the DLL is never loaded or
run), checks every entry against known hashes and writes dlssnr-amd\dlssnr.bin only if all
match. The model stays in this package, so later installs from it (into other games too) do not
ask again; with a newer package, choose the DLL once more or copy that file over.

Use
---
OptiScaler: turn on DLSS (or FSR / XeSS) in the game's graphics settings. Insert opens the
            OptiScaler menu, the NR settings are on the DLSS Neural Rendering page (with the
            RenoDX style on they are in NR Control instead, see below).
            End opens NR Control, this project's own NR menu: it shows what NR is doing (frame
            and network sizes, passes, NR's cost split into the network and the rest of the
            processing), and switches int4 mixed and Preprocess; with the RenoDX style on it also
            holds every NR setting. Changes are written to dlssnr-amd.ini at once and apply in the
            game within about a second. While the menu is open it takes the keyboard and mouse; End
            or Esc closes it. The menu is drawn over the game's picture (DX11 / DX12); while it is
            closed it draws nothing. A game that uses only raw input may still receive input while
            the menu is open.
            If the NR page keeps showing "Waiting for the upscaler to run", set
            [Spoofing] Dxgi=false in OptiScaler.ini and try again.
ReShade:    Home opens ReShade, the settings are on the Add-ons page; the same settings are
            kept in dlssnr-amd.ini in the game folder and changes apply live.
            Model resolution, and Model passes of 5 or more, rebuild the network; for the few
            seconds that takes the picture is shown without NR. Switching Model passes between 1
            and 2 or more also rebuilds in the background; until it is done NR keeps running with
            the previous number of passes.
            NoiseEveryFrame in dlssnr-amd.ini (in the file only, default false): on frames without
            motion vectors (DLSS5_MV not bound) the network's noise seed changes every frame, as in
            NVIDIA's original; with no previous frame to average the noise, a still picture shimmers
            slightly. false: a fixed seed.
            When Model resolution is below 100%, Enlargement chooses how the model's result is
            brought back to full resolution: Matched residual (the default) and Edge-aware
            lighting + colour enlarge the change the model made and apply it to the
            full-resolution picture; Classic enlarges the model's output picture (with a choice
            of scalers, including FSR 1).
            HDR: scRGB (linear) HDR works; this route leaves HDR10 (PQ/HLG) pictures alone, so
            set the game to SDR or scRGB. With the RenoDX style on, HDR10 (PQ) is handled too, HLG
            is not.

Preprocess (optional, off by default): [Preprocess] in dlssnr-amd.ini in the game folder (the
installer writes the file; ReShade also shows it on the Add-ons page). It changes the picture
the network is shown (exposure, display curve, contrast, saturation), and so how NR edits the
picture. Two uses: a personal look in any game (it departs from the original look; the result
may be better or worse), and games that do not hand their exposure to the upscaler, which it
fixes (007 First Light turns green and grainy with NR otherwise). Ctrl+F10 switches it for the
current run, to compare. The file explains every setting.

RenoDX style (optional, off by default): the processing around NR (how the picture is handed to NR
and how NR's result is put back into the picture) follows RenoDX's DLSS5 add-on v8.5.0-rc10, with
RenoDX's own shaders (MIT, licence in dlssnr-amd\shaders\renodx\RenoDX-LICENSE.txt). The NR network
itself is the same.
  - Switch: [RenoDX] Enabled in dlssnr-amd.ini, applied after a game restart. The installer sets it
    when you choose it; the ReShade routes' Add-ons page has the switch too.
  - With it on, every NR setting is in [RenoDX]: on the OptiScaler route in NR Control (End), on the
    ReShade routes on the Add-ons page, or edit the file. Apart from Enabled, changes apply in
    the running game. The file explains every setting, and so does (?) in the menus. The settings
    keep RenoDX's own names, defaults and ranges.
  - OptiScaler route (with the RenoDX style on): OptiScaler only does the upscaling and frame
    generation (its upscaler, sharpening and frame generation settings work as usual); the settings
    on its DLSS Neural Rendering page have no effect (fixed while running: NR on, 1 pass, Model
    resolution 100%, Peripheral compression / Finished Picture / Deferred / debug views off).
    ReShade routes: the NR settings in [DlssNr] have no effect.
  - NR on/off (NeuralUplift; F6 in game switches it for the current run), the network's settings
    (Intensity, Style, Local tone, Local structure, Skin structure, Character mask), NR before or
    after the upscaler (NRHookPoint, OptiScaler route only; always after it when the game uses Ray
    Reconstruction), the network's resolution (NRResolutionScale).
  - Passes (NRPasses 1-4): each pass hands the previous pass's result to the network again. Passes
    2, 3 and 4 each have their own strengths (Transfer / Color) and network settings; with "Same as
    pass 1" on, the network settings follow pass 1, Intensity is always the pass's own.
    NRUnlockPasses = 1 allows up to 30 passes (as OptiScaler's UnlockPasses; RenoDX itself stops at
    4); passes 5 and later take pass 4's settings. Every pass runs the network once more, which
    lowers the frame rate a lot.
  - Adjust NR's look (NRLookMode and NRLook*): adjusts how much NR changed, directly on its result -
    brighten, darken, colour, hue, shadows / midtones / highlights, broad lighting and detail, dark
    halos, temporal smoothing. Off by default; off leaves the picture unchanged.
  - Screenshot (F5, also a button in NR Control and on the Add-ons page): the same frame without
    and with NR, saved to NR Screenshots\ in the game folder. By default the final picture: on the
    ReShade routes the picture sent to the display; on the OptiScaler route NR moves to
    OptiScaler's finished picture for one frame and moves back afterwards. HDR pictures are saved
    as 16-bit PQ PNG (with cICP and ICC, so they stay HDR), SDR as 8-bit sRGB. JPEG, both PNG and
    JPEG, or an extra side-by-side picture can be chosen; NRScreenshotSource = 2 saves the model's
    input and output (for diagnosis, without the game's tone mapping and UI). Nothing is saved
    while NR is off.
  - The automatic mask (NRAutoMask) is off by default, as in RenoDX (the Character mask in
    OptiScaler's menu on the OptiScaler route is on by default); when it is on, skin follows only
    Skin structure by default (NRSkinIndependent, as RenoDX).
  - Preprocess can be used with it.

The first time NR runs in a game the network has to compile; it takes effect after about a
minute (much longer than on Linux, where it takes 10-20 seconds). Until then the picture looks
as without NR, which does not mean the mod is not working: give it a minute. After that it is
cached. This happens once for each game.

Files put into the game folder
------------------------------
    dlssnr-amd\                  model and shaders
    dlssnr-amd-install.txt       the installation record, which uninstall follows
    dlssnr-amd.ini               settings, written at install
    OptiScaler route:            dxgi.dll (OptiScaler), dlssnr_core.dll and OptiScaler's own files
    ReShade route (DX10/11/12):  dxgi.dll (ReShade), dlssnr_amd.addon64, ReShade's settings and
                                 shaders
    ReShade route (DX9):         d3d9.dll dxgi.dll (DXVK), vulkan-1.dll (Vulkan loader; never
                                 calls DXGI and loads the ReShade layer from the game folder),
                                 ReShade's files
    ReShade route (Vulkan):      vulkan-1.dll and ReShade's files
Files of the same name already in the game folder are first moved to dlssnr-amd-backup\ and
put back on uninstall. Screenshots of the RenoDX style go to NR Screenshots\ (made at the first
screenshot, not removed by uninstall).
Installing again (another route, another package) first uninstalls by the installation record,
and dlssnr-amd.ini is replaced with a new default file: earlier settings are not kept, so back
the file up first if you need them.

Logs: dlssnr-amd.log, and OptiScaler.log or ReShade.log, all in the game folder.

Known limits
------------
- DX9 games run entirely on DXVK; the frame rate can differ from native D3D9, and overlays such
  as Steam's may not show.
- In DX10 games the game's depth buffer is not passed to NR yet (not implemented).
- Games whose FSR runs in their own shaders never hand it to OptiScaler: pick the game's DLSS
  option instead, which OptiScaler offers.
- When video memory or system memory (including virtual memory) runs short, the network pauses
  by itself and the picture returns to normal until memory frees up. Set the page file to
  "System managed size".
- 64-bit package only.

Third-party components
----------------------
DXVK (zlib licence) comes from GE-Proton 11-7; the version is in dxvk\version.txt. Source:
https://github.com/doitsujin/dxvk . The licence files of ReShade 6.8.0, OptiScaler-NR 0.8.91,
the Vulkan Loader (with a description of the changes), vort_Shaders and DLSS5-Feeder are in
their folders.
