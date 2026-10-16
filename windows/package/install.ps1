# DLSSNR-AMD Windows installer (run by install.bat)
#
#   install.bat                      a window asks for the game's exe, then for the route
#   install.bat <game exe or its folder> [optiscaler|reshade-feeder|reshade-feeder-dx9|reshade-feeder-vulkan|remove|logs]
#               [-Dll <nvngx_dlssnr.dll or its zip>] [-Network default|aco|aco-int4-mixed] [-RenoDX on|off]
#
# Network (chosen at install, written to [Network] ACO Mode and [Int4Mixed] Enabled in dlssnr-amd.ini): default = 0 / 0,
# aco = 1 / 0, aco-int4-mixed = 1 / 1 (int4 mixed runs with ACO's machine code only). int4 mixed is always installed
# (its weights are made at install); the choice can be changed in the ini later.
#
# RenoDX style (asked at install, -RenoDX on|off; [RenoDX] Enabled in dlssnr-amd.ini): the picture before and after NR
# is processed the way RenoDX's DLSS5 add-on does it, and every NR setting moves to [RenoDX] (on the OptiScaler route in
# the NR Control menu, on the ReShade routes on the Add-ons page).
#
# The package carries no model: on the first install the user gives NVIDIA's nvngx_dlssnr.dll (310.8.0 or 310.9.16)
# or a zip that contains it, and model-tools\dlssnr_extract_model.exe makes dlssnr.bin from it (it only
# reads the weight data; the DLL is never loaded or run). The model is kept in the package's dlssnr-amd\,
# so later installs from this package need no DLL.
#
# Every file put into the game folder is listed in dlssnr-amd-install.txt; existing files it would
# overwrite are first moved to dlssnr-amd-backup\, and uninstall deletes ours and puts them back.
param([string]$Target = '', [string]$Route = '', [string]$Dll = '', [string]$Network = '', [string]$RenoDX = '',
      [switch]$Pause)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$ManifestName = 'dlssnr-amd-install.txt'
$BackupName = 'dlssnr-amd-backup'

function Say([string]$text) { Write-Host $text }
# Re-run as administrator it is a separate window: pause before it closes so the result can be read.
function Finish([int]$code) { if ($Pause) { Read-Host 'Press Enter to close' | Out-Null }; exit $code }
function Fail([string]$text) { Write-Host $text -ForegroundColor Red; Finish 1 }

# ---- game folder -----------------------------------------------------------------------------
if (-not $Target) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = "Choose the game's exe (the one that actually runs, not the launcher)"
    $dialog.Filter = 'Game program (*.exe)|*.exe'
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { Fail 'No game chosen.' }
    $Target = $dialog.FileName
}
$Target = $Target.Trim('"')
if (Test-Path -LiteralPath $Target -PathType Leaf) {
    $exe = (Resolve-Path -LiteralPath $Target).Path
    $game = Split-Path -Parent $exe
} elseif (Test-Path -LiteralPath $Target -PathType Container) {
    $game = (Resolve-Path -LiteralPath $Target).Path
    $exe = Get-ChildItem -LiteralPath $game -Filter *.exe | Select-Object -First 1 -ExpandProperty FullName
} else {
    Fail "Not found: $Target"
}
$manifest = Join-Path $game $ManifestName
$backup = Join-Path $game $BackupName

function Get-ExeBits([string]$path) {
    try {
        $b = [System.IO.File]::ReadAllBytes($path)
        $pe = [BitConverter]::ToInt32($b, 0x3C)
        switch ([BitConverter]::ToUInt16($b, $pe + 4)) { 0x8664 { return 64 } 0x14C { return 32 } }
    } catch { }
    return 0
}

# ---- choose the route ------------------------------------------------------------------------
if (-not $Route) {
    Say ''
    Say "Game folder: $game"
    Say 'Choose a route:'
    Say '  1) optiscaler             the game has a DLSS, FSR or XeSS option (DX11 / DX12, OptiScaler)'
    Say '  2) reshade-feeder         other DX10/11/12 games (ReShade)'
    Say '  3) reshade-feeder-dx9     old DX9 games (ReShade)'
    Say '  4) reshade-feeder-vulkan  Vulkan games (ReShade)'
    Say '  5) Uninstall'
    Say '  6) Collect logs           when something goes wrong; the zip goes to the desktop'
    $pick = Read-Host '1-6'
    switch ($pick) {
        '1' { $Route = 'optiscaler' } '2' { $Route = 'reshade-feeder' } '3' { $Route = 'reshade-feeder-dx9' }
        '4' { $Route = 'reshade-feeder-vulkan' } '5' { $Route = 'remove' } '6' { $Route = 'logs' }
        default { Fail 'Invalid choice.' }
    }
}
# The old route names were renamed; optiscaler-renodx means optiscaler with -RenoDX on.
$renamed = @{ 'reshade' = 'reshade-feeder'; 'dx9' = 'reshade-feeder-dx9'; 'vulkan' = 'reshade-feeder-vulkan' }
if ($renamed.ContainsKey($Route)) { Fail "Unknown route: $Route (this route is now called $($renamed[$Route]))" }
if ($Route -eq 'optiscaler-renodx') { $Route = 'optiscaler'; if (-not $RenoDX) { $RenoDX = 'on' } }
if ('optiscaler', 'reshade-feeder', 'reshade-feeder-dx9', 'reshade-feeder-vulkan', 'remove', 'logs' -notcontains $Route) {
    Fail "Unknown route: $Route"
}

# ---- choose the network ----------------------------------------------------------------------
$haveInt4 = Test-Path -LiteralPath (Join-Path $here 'dlssnr-amd\int4') -PathType Container
if ($Route -ne 'remove' -and $Route -ne 'logs' -and -not $Network) {
    Say ''
    Say 'Choose the network (both settings can be changed in dlssnr-amd.ini later):'
    Say '  1) default network, compiled by the AMD driver   ACO Mode = 0, Int4Mixed Enabled = 0 (default)'
    Say '  2) default network, ACO machine code             ACO Mode = 1, Int4Mixed Enabled = 0'
    $menu = @('default', 'aco')
    if ($haveInt4) {
        Say '  3) int4 mixed, ACO machine code                  ACO Mode = 1, Int4Mixed Enabled = 1'
        $menu += 'aco-int4-mixed'
        Say 'int4 mixed: part of the network runs in int4 and other lower precisions; faster, the picture differs somewhat.'
        Say 'It runs with ACO machine code only. Ctrl+F11 in game switches between int4 mixed and the default network.'
    }
    Say 'ACO machine code: the network as machine code made in advance by the Linux driver''s compiler (ACO), shipped in'
    Say 'the package and loaded by the AMD driver as it is; faster than the AMD driver''s own compile. Tested on one driver'
    Say 'version only (AMD Software 26.9.2); if it causes problems on another driver, set ACO Mode back to 0.'
    $pick = Read-Host "1-$($menu.Count) (Enter = 1)"
    if ($pick -eq '') { $pick = '1' }
    $n = 0
    if ([int]::TryParse($pick, [ref]$n) -and $n -ge 1 -and $n -le $menu.Count) { $Network = $menu[$n - 1] }
    if (-not $Network) { Fail 'Invalid choice.' }
}
if (-not $Network) { $Network = 'default' }
if ('default', 'aco', 'aco-int4-mixed' -notcontains $Network) { Fail "Unknown network: $Network" }
if ($Network -eq 'aco-int4-mixed' -and -not $haveInt4) { Fail 'This package has no int4 mixed.' }

# ---- RenoDX style ----------------------------------------------------------------------------
if ($Route -ne 'remove' -and $Route -ne 'logs' -and -not $RenoDX) {
    Say ''
    Say 'RenoDX style (optional): the picture before and after NR is processed the way RenoDX''s DLSS5 add-on does it (the NR network itself is the same).'
    if ($Route -eq 'optiscaler') {
        Say '  y: turn it on. Every NR setting is in NR Control (End in game); the OptiScaler menu only handles upscaling and frame generation.'
    } else {
        Say '  y: turn it on. Every NR setting is on ReShade''s Add-ons page ([RenoDX] in dlssnr-amd.ini).'
    }
    Say '  N: leave it off, as before. To turn it on later, set [RenoDX] Enabled = 1 in dlssnr-amd.ini and restart the game.'
    $pick = Read-Host 'Turn on the RenoDX style? [y/N]'
    $RenoDX = if ($pick -eq 'y' -or $pick -eq 'Y') { 'on' } else { 'off' }
}
if (-not $RenoDX) { $RenoDX = 'off' }
if ('on', 'off' -notcontains $RenoDX) { Fail "Unknown RenoDX choice: $RenoDX (on or off)" }

# ---- games under Program Files need administrator rights -------------------------------------
if ($Route -ne 'logs') {
    $probe = Join-Path $game ('dlssnr-amd-write-test-' + [guid]::NewGuid().ToString('N'))
    try { [System.IO.File]::WriteAllText($probe, ''); Remove-Item -LiteralPath $probe -Force }
    catch {
        $me = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
        if ($me.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { Fail "Cannot write to $game" }
        Say 'This folder needs administrator rights, asking for them...'
        $elevated = "-NoProfile -ExecutionPolicy Bypass -STA -File `"$PSCommandPath`" -Target `"$game`" -Route $Route -Network $Network -RenoDX $RenoDX -Pause"
        if ($Dll) { $elevated += " -Dll `"$Dll`"" }
        $p = Start-Process powershell -Verb RunAs -ArgumentList $elevated -PassThru -Wait
        exit $p.ExitCode
    }
}

# ---- installation record ---------------------------------------------------------------------
function Record([string]$line) { Add-Content -LiteralPath $manifest -Value $line -Encoding UTF8 }

# A target that exists and was not installed by us: move it to the backup folder, put back on uninstall.
function Save-Existing([string]$rel) {
    $dst = Join-Path $game $rel
    if (-not (Test-Path -LiteralPath $dst)) { return }
    $keep = Join-Path $backup $rel
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $keep) | Out-Null
    Move-Item -LiteralPath $dst -Destination $keep -Force
    Record "B $rel"
}
function Put-File([string]$src, [string]$rel) {
    Save-Existing $rel
    Copy-Item -LiteralPath $src -Destination (Join-Path $game $rel) -Force
    Unblock-File -LiteralPath (Join-Path $game $rel) -ErrorAction SilentlyContinue
    Record "F $rel"
}
function Put-Tree([string]$src, [string]$rel) {
    Save-Existing $rel
    Copy-Item -LiteralPath $src -Destination (Join-Path $game $rel) -Recurse -Force
    Get-ChildItem -LiteralPath (Join-Path $game $rel) -Recurse -File | Unblock-File -ErrorAction SilentlyContinue
    Record "D $rel"
}

function Remove-Installed {
    if (-not (Test-Path -LiteralPath $manifest)) { Say 'No installation record found, nothing to uninstall.'; return }
    $lines = @(Get-Content -LiteralPath $manifest -Encoding UTF8)
    [array]::Reverse($lines)
    foreach ($line in $lines) {
        $line = $line.TrimStart([char]0xFEFF)
        if ($line.Length -lt 3) { continue }
        $kind = $line.Substring(0, 1); $rel = $line.Substring(2)
        if ($rel -match '(^|[\\/])\.\.([\\/]|$)' -or [System.IO.Path]::IsPathRooted($rel)) { Say "Skipping suspicious entry: $rel"; continue }
        $path = Join-Path $game $rel
        switch ($kind) {
            { 'F', 'D', 'L' -contains $_ } { if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force } }
            'B' {
                $keep = Join-Path $backup $rel
                if (Test-Path -LiteralPath $keep) {
                    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
                    Move-Item -LiteralPath $keep -Destination $path -Force
                }
            }
        }
    }
    if (Test-Path -LiteralPath $manifest) { Remove-Item -LiteralPath $manifest -Force }
    if (Test-Path -LiteralPath $backup) {
        if (@(Get-ChildItem -LiteralPath $backup -Recurse -File).Count -eq 0) { Remove-Item -LiteralPath $backup -Recurse -Force }
        else { Say "Note: files in $backup were not put back; please check them by hand." }
    }
    Say "Uninstalled from $game."
}

# Files the game creates while running; removed on uninstall as well.
function Record-Logs([string[]]$names) { foreach ($n in $names) { Record "L $n" } }

# ---- collect logs ----------------------------------------------------------------------------
if ($Route -eq 'logs') {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $tmp = Join-Path $env:TEMP "dlssnr-amd-logs-$stamp"
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    $patterns = 'dlssnr-amd.log', 'dlssnr-amd.ini', $ManifestName, 'OptiScaler.log', 'OptiScaler.ini',
                'ReShade.log', 'ReShade.ini', 'ReShadePreset.ini', '*_dxgi.log', '*_d3d11.log', '*_d3d9.log',
                '*_d3d12.log', 'vkd3d-proton.log', 'dlssnr-amd-crash.dmp'
    foreach ($p in $patterns) {
        Get-ChildItem -LiteralPath $game -Filter $p -File -ErrorAction SilentlyContinue | Copy-Item -Destination $tmp
    }
    $info = @("game: $game", "exe: $exe", "exe bits: $(Get-ExeBits $exe)", "package: $here", '')
    $info += Get-CimInstance Win32_VideoController | ForEach-Object { "GPU: $($_.Name) driver $($_.DriverVersion) ($($_.DriverDate))" }
    $info += Get-CimInstance Win32_OperatingSystem | ForEach-Object { "OS: $($_.Caption) $($_.Version)" }
    $info += ''
    $info += Get-ChildItem -LiteralPath $game | ForEach-Object { '{0,12} {1}' -f $_.Length, $_.Name }
    $info | Set-Content -LiteralPath (Join-Path $tmp 'system.txt') -Encoding UTF8

    # The last two days of graphics driver events (driver stopped responding and recovered = 4101) and game crashes.
    $since = (Get-Date).AddDays(-2)
    $events = @()
    try {
        # Every error and warning in the System log (a driver reset is not always logged as 4101).
        $events += Get-WinEvent -FilterHashtable @{ LogName = 'System'; StartTime = $since; Level = 1, 2, 3 } -MaxEvents 300 -ErrorAction Stop
    } catch { }
    try {
        $events += Get-WinEvent -FilterHashtable @{ LogName = 'Application'; StartTime = $since; Id = 1000, 1001, 1002 } -ErrorAction Stop |
            Where-Object { $_.Message -match [regex]::Escape([System.IO.Path]::GetFileName($exe)) -or $_.Message -match 'LiveKernelEvent' }
    } catch { }
    $events | Sort-Object TimeCreated | ForEach-Object {
        "[$($_.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss'))] $($_.LogName) $($_.ProviderName) id=$($_.Id)`r`n$($_.Message)`r`n"
    } | Set-Content -LiteralPath (Join-Path $tmp 'events.txt') -Encoding UTF8

    # File permissions: for finding out why ReShade says it cannot save its settings.
    $perm = @("user: $([Security.Principal.WindowsIdentity]::GetCurrent().Name)",
              "admin: $(([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))", '')
    foreach ($name in '.', 'ReShade.ini', 'ReShadePreset.ini', 'dlssnr-amd.ini') {
        $path = Join-Path $game $name
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $item = Get-Item -LiteralPath $path -Force
        $perm += "== $name  attributes: $($item.Attributes)"
        $perm += (& icacls.exe $path 2>&1 | Out-String)
    }
    $perm | Set-Content -LiteralPath (Join-Path $tmp 'permissions.txt') -Encoding UTF8
    $zip = Join-Path ([Environment]::GetFolderPath('Desktop')) "dlssnr-amd-logs-$stamp.zip"
    Compress-Archive -Path (Join-Path $tmp '*') -DestinationPath $zip -Force
    Remove-Item -LiteralPath $tmp -Recurse -Force
    Say "Logs packed: $zip"
    Finish 0
}

if ($Route -eq 'remove') { Remove-Installed; Finish 0 }

# ---- install ---------------------------------------------------------------------------------
$bits = Get-ExeBits $exe
if ($bits -eq 32) { Fail "This is a 32-bit game ($exe); this package supports 64-bit games only." }
# ---- model -----------------------------------------------------------------------------------
# The package's dlssnr-amd\dlssnr.bin, or one extracted now from the user's nvngx_dlssnr.dll. Its SHA256
# has to be the tested model's.
$ModelSha = '2B41C888CF4155B8958C665BA64018AB0BD25C85FC71A2B6DB86D0D04D1F7FBD'
$SmallModelSha = 'DC7EDF0C5DA0AF3C3B2048BAFA72F928F4F2EB89F117A37BC4C8863BB2645D8C'
$model = Join-Path $here 'dlssnr-amd\dlssnr.bin'
$modelFrom = $model
if ($Dll -or -not (Test-Path -LiteralPath $model)) {
    if (-not $Dll) {
        Say ''
        Say 'The package has no model. The first install needs NVIDIA''s nvngx_dlssnr.dll (310.8.0 for HT, 310.9.16 for Small), or a zip that contains it.'
        Say 'Only its weight data is read; it is never loaded or run. The model is kept in this package, so later installs do not ask again.'
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Title = 'Choose nvngx_dlssnr.dll (310.8.0 or 310.9.16) or its zip'
        $dialog.Filter = 'nvngx_dlssnr.dll or zip (*.dll;*.zip)|*.dll;*.zip'
        if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { Fail 'No nvngx_dlssnr.dll chosen; cannot install.' }
        $Dll = $dialog.FileName
    }
    $Dll = $Dll.Trim('"')
    if (-not (Test-Path -LiteralPath $Dll -PathType Leaf)) { Fail "Not found: $Dll" }
    $work = Join-Path $env:TEMP ("dlssnr-amd-model-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    try {
        $src = (Resolve-Path -LiteralPath $Dll).Path
        if ([System.IO.Path]::GetExtension($src) -ieq '.zip') {
            Say 'Unpacking the zip ...'
            Expand-Archive -LiteralPath $src -DestinationPath (Join-Path $work 'zip') -Force
            $found = @(Get-ChildItem -LiteralPath (Join-Path $work 'zip') -Recurse -File -Filter 'nvngx_dlssnr.dll')
            if ($found.Count -ne 1) { Fail "The zip should hold exactly one nvngx_dlssnr.dll; found $($found.Count)." }
            $src = $found[0].FullName
        }
        Say 'Extracting the model ...'
        $tmpModel = Join-Path $work 'dlssnr.bin'
        & (Join-Path $here 'model-tools\dlssnr_extract_model.exe') $src $tmpModel
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $tmpModel)) { Fail 'Extracting the model failed (see above).' }
        try {
            Copy-Item -LiteralPath $tmpModel -Destination $model -Force
            Say "The model is kept in $model; later installs from this package need no DLL."
        } catch {
            # The package folder is not writable: install from the temporary copy; the next install asks again.
            $modelFrom = Join-Path $env:TEMP 'dlssnr-amd-model.bin'
            Copy-Item -LiteralPath $tmpModel -Destination $modelFrom -Force
            Say 'Note: the model could not be kept in the package folder (not writable?); the next install needs the DLL again.'
        }
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
$actualModelSha = (Get-FileHash -LiteralPath $modelFrom -Algorithm SHA256).Hash
if ($actualModelSha -notin @($ModelSha, $SmallModelSha)) {
    Fail "$modelFrom is damaged or not this version's model. Delete it and install again with nvngx_dlssnr.dll."
}
if ($actualModelSha -eq $SmallModelSha) {
    Say 'Model: CC_SMALL_FP8 (31 blocks), original FP8 weights.'
    if ($Network -eq 'aco-int4-mixed') {
        Say 'The HT int4 calibration does not apply to Small; using Small FP8 with ACO Mode.'
        $Network = 'aco'
    }
    $haveInt4 = $false
} else { Say 'Model: HT (71 blocks).' }
# int4 mixed's weights: made from this model with the package's data, checked against the tested result.
$int4ModelFrom = $null
$int4Data = Join-Path $here 'dlssnr-amd\int4'
if ($haveInt4) {
    $generator = Join-Path $here 'model-tools\dlssnr-int4-weights.exe'
    $hashFile = Join-Path $here 'model-tools\dlssnr-int4.sha256'
    if (-not (Test-Path -LiteralPath $generator) -or -not (Test-Path -LiteralPath $hashFile)) {
        Fail 'The int4 mixed files of the package are incomplete; unpack the package again.'
    }
    $int4Sha = ((Get-Content -LiteralPath $hashFile -Raw).Trim() -split '\s+')[0]
    $int4ModelFrom = Join-Path ([System.IO.Path]::GetTempPath()) ("dlssnr-int4-$([guid]::NewGuid().ToString('N')).bin")
    Say 'Making the int4 mixed weights from the model ...'
    & $generator $modelFrom $int4Data $int4ModelFrom
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $int4ModelFrom) -or
        (Get-FileHash -LiteralPath $int4ModelFrom -Algorithm SHA256).Hash -ne $int4Sha) {
        Remove-Item -LiteralPath $int4ModelFrom -Force -ErrorAction SilentlyContinue
        Fail 'Making or checking the int4 mixed weights failed; the game folder was not changed.'
    }
}
if (Test-Path -LiteralPath $manifest) { Say 'Found a previous installation, removing it first.'; Remove-Installed }

Set-Content -LiteralPath $manifest -Value "F $ManifestName" -Encoding UTF8
Put-Tree (Join-Path $here 'dlssnr-amd') 'dlssnr-amd'
if ($modelFrom -ne $model) { Copy-Item -LiteralPath $modelFrom -Destination (Join-Path $game 'dlssnr-amd\dlssnr.bin') -Force }
if ($int4ModelFrom) {
    try { Put-File $int4ModelFrom 'dlssnr-amd\dlssnr-int4.bin' }
    finally { Remove-Item -LiteralPath $int4ModelFrom -Force -ErrorAction SilentlyContinue }
}
$exeName = [System.IO.Path]::GetFileNameWithoutExtension($exe)
Record-Logs @('dlssnr-amd.log', 'dlssnr-amd-crash.dmp')

# Other ReShade add-ons in the game folder (installed earlier) would load too; move them to the backup folder.
function Move-OtherAddons {
    foreach ($item in Get-ChildItem -LiteralPath $game -File | Where-Object { $_.Extension -in '.addon', '.addon64' }) {
        if ($item.Name -ne 'dlssnr_amd.addon64') { Save-Existing $item.Name; Say "Moved another ReShade add-on aside: $($item.Name)" }
    }
}
# ReShade's files. $asLayer: as a Vulkan layer (DX9 through DXVK, Vulkan games), found by the Vulkan loader in the
# game folder; otherwise ReShade itself goes in as dxgi.dll (DX10/11/12 games).
function Put-ReShade([bool]$asLayer) {
    Move-OtherAddons
    foreach ($item in Get-ChildItem -LiteralPath (Join-Path $here 'reshade')) {
        if ($item.Name -eq 'ReShadePreset-d3d9.ini') { continue }
        if (-not $asLayer -and ($item.Name -eq 'vk-override' -or $item.Name -eq 'ReShade64.dll')) { continue }
        if ($item.PSIsContainer) { Put-Tree $item.FullName $item.Name } else { Put-File $item.FullName $item.Name }
    }
    if (-not $asLayer) { Put-File (Join-Path $here 'reshade\ReShade64.dll') 'dxgi.dll' }
    Record-Logs @('ReShade.log')
}

if ($Route -eq 'optiscaler') {
    # The game keeps the system's own D3D11/D3D12; NR runs on a Vulkan device of its own and shares the frame with it.
    foreach ($item in Get-ChildItem -LiteralPath (Join-Path $here 'optiscaler\game')) {
        if ($item.Name -eq 'OptiScaler.dll') { continue }
        if ($item.PSIsContainer) { Put-Tree $item.FullName $item.Name } else { Put-File $item.FullName $item.Name }
    }
    Put-File (Join-Path $here 'optiscaler\game\OptiScaler.dll') 'dxgi.dll'
    foreach ($f in 'dlssnr_core.dll', 'nvngx.dll_dlssnr.dll', 'nvngx_dlssnr.dll') { Put-File (Join-Path $here "optiscaler\$f") $f }
    Record-Logs @('OptiScaler.log')

    $ini = Join-Path $game 'OptiScaler.ini'
    $wanted = [ordered]@{
        'DlssNr|Enabled'               = 'true'
        'Libraries|NvngxPath'          = (Join-Path $game 'dlssnr_core.dll')
        # DX11 games: the upscaler runs on OptiScaler's D3D12 device (FSR, DX11 on 12), which is where NR runs.
        'Upscalers|Dx11Upscaler'       = 'ffx_12'
        # OptiScaler's own log is on while this is experimental.
        'Log|LogToFile'                = 'true'
        'Log|LogLevel'                 = '1'
    }
    # Rewritten when present, no note when absent: the white point follows the game's own exposure (v0.8.4's default;
    # from v0.8.5 the default is a fixed white point).
    $optional = [ordered]@{
        'DlssNr|WhitePointSource'      = '1'
    }
    $lines = [System.IO.File]::ReadAllLines($ini)
    $section = $null; $seen = @{}
    for ($i = 0; $i -lt $lines.Length; $i++) {
        if ($lines[$i] -match '^\s*\[([^\]]+)\]') { $section = $Matches[1]; continue }
        if ($section -and $lines[$i] -match '^\s*([A-Za-z0-9_]+)\s*=') {
            $key = "$section|$($Matches[1])"
            if ($wanted.Contains($key) -and -not $seen.ContainsKey($key)) {
                $lines[$i] = "$($Matches[1])=$($wanted[$key])"; $seen[$key] = 1
            } elseif ($optional.Contains($key) -and -not $seen.ContainsKey($key)) {
                $lines[$i] = "$($Matches[1])=$($optional[$key])"; $seen[$key] = 1
            }
        }
    }
    [System.IO.File]::WriteAllLines($ini, $lines)
    foreach ($key in $wanted.Keys) { if (-not $seen.ContainsKey($key)) { Say "Note: $key not found in OptiScaler.ini" } }
} elseif ($Route -eq 'reshade-feeder') {
    # DX10/11/12 games keep the system's own D3D; ReShade goes in as dxgi.dll.
    Put-ReShade $false
} else {
    # DX9: DXVK moves the game to Vulkan, ReShade runs as a Vulkan layer. Vulkan games: only the ReShade layer.
    # The Vulkan loader in the game folder never calls DXGI and finds ReShade's layer beside it.
    Put-File (Join-Path $here 'vulkan\vulkan-1.dll') 'vulkan-1.dll'
    if ($Route -eq 'reshade-feeder-dx9') {
        Put-File (Join-Path $here 'dxvk\d3d9.dll') 'd3d9.dll'
        Put-File (Join-Path $here 'dxvk\dxgi.dll') 'dxgi.dll'
        Record-Logs @("${exeName}_dxgi.log", "${exeName}_d3d9.log")
    }
    Put-ReShade $true
    if ($Route -eq 'reshade-feeder-dx9') {
        Copy-Item -LiteralPath (Join-Path $here 'reshade\ReShadePreset-d3d9.ini') -Destination (Join-Path $game 'ReShadePreset.ini') -Force
    }
}

# dlssnr-amd.ini is written at install with every setting and its description (a reinstall writes the defaults
# again). Uninstall restores any configuration that existed before installation.
if ($Route -eq 'optiscaler' -and $RenoDX -eq 'on') { $iniSrc = "ini\$Network\dlssnr-amd-optiscaler-renodx.ini" }
elseif ($Route -eq 'optiscaler') { $iniSrc = "ini\$Network\dlssnr-amd-optiscaler.ini" }
else { $iniSrc = "ini\$Network\dlssnr-amd-addon.ini" }
$iniDst = Join-Path $game 'dlssnr-amd.ini'
Put-File (Join-Path $here $iniSrc) 'dlssnr-amd.ini'
if ($Route -ne 'optiscaler' -and $RenoDX -eq 'on') {
    # RenoDX style: Enabled = 1 in the [RenoDX] section
    $lines = [System.IO.File]::ReadAllLines($iniDst, [System.Text.Encoding]::UTF8)
    $inRenoDX = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\[') { $inRenoDX = $lines[$i] -eq '[RenoDX]' }
        elseif ($inRenoDX -and $lines[$i] -eq 'Enabled = 0') { $lines[$i] = 'Enabled = 1' }
    }
    [System.IO.File]::WriteAllText($iniDst, (($lines -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding($false)))
}

Say ''
Say "Installed into: $game$(if ($RenoDX -eq 'on') { ' (RenoDX style)' })"
$aco = if ($Network -eq 'aco' -or $Network -eq 'aco-int4-mixed') { 1 } else { 0 }
Say "Network: ACO Mode = $aco ([Network] in dlssnr-amd.ini; a change takes effect after a game restart)"
if ($int4ModelFrom) {
    $i4 = if ($Network -eq 'aco-int4-mixed') { 1 } else { 0 }
    Say "int4 mixed: Enabled = $i4 ([Int4Mixed] in dlssnr-amd.ini). Ctrl+F11 in game switches between int4 mixed and the default network."
    if ($aco -eq 0) { Say 'Switching to int4 mixed with Ctrl+F11 needs ACO Mode = 1.' }
}
if ($Route -eq 'optiscaler') {
    if ($RenoDX -eq 'on') {
        Say 'In the game, turn on DLSS (or FSR / XeSS) in the graphics settings. Insert opens the OptiScaler menu (with the RenoDX style on, it only handles upscaling and frame generation).'
    } else {
        Say 'In the game, turn on DLSS (or FSR / XeSS) in the graphics settings. Insert opens the OptiScaler menu;'
        Say 'the NR settings are on the DLSS Neural Rendering page.'
    }
    Say 'If colours turn green or grainy after NR (007 First Light, for example), try [Preprocess] in dlssnr-amd.ini, or press Ctrl+F10 in the game.'
    Say "End opens NR Control (this project's NR menu: int4 mixed, Preprocess$(if ($RenoDX -eq 'on') { ', and every NR setting of the RenoDX style' }))."
    if ($RenoDX -eq 'on') {
        Say 'The RenoDX style is on ([RenoDX] Enabled = 1 in dlssnr-amd.ini). With it on, every NR setting is in NR Control ([RenoDX]);'
        Say 'the OptiScaler menu only handles upscaling and frame generation, and its NR page has no effect. Turning the RenoDX style off'
        Say '(Enabled = 0, restart the game) brings back the previous behaviour, with the NR settings in the OptiScaler menu.'
        Say 'F5 saves a screenshot (the same frame without and with NR) to NR Screenshots in the game folder.'
    }
} else {
    Say 'In the game, Home opens ReShade; the settings are on the Add-ons page, or edit dlssnr-amd.ini in the game folder.'
    if ($RenoDX -eq 'on') {
        Say 'The RenoDX style is on ([RenoDX] Enabled = 1 in dlssnr-amd.ini). With it on, every NR setting is in the RenoDX part of the'
        Say 'Add-ons page ([RenoDX]), and the NR settings of [DlssNr] have no effect. Turning it off (Enabled = 0, restart the game)'
        Say 'brings back the previous behaviour. F5 saves a screenshot (the same frame without and with NR) to NR Screenshots in the game folder.'
    }
}
Say 'The first time NR runs in a game the network has to compile; it takes effect after about a minute. This happens once for each game.'
Say 'Uninstall: run install.bat again and choose 5.'
Finish 0
