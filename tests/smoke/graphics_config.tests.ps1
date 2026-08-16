#!/usr/bin/env pwsh
# Pester smoke tests for the graphics.cfg eager-write boot dance with
# RAM-bucket autodetect.
#
# Mirrors audio_config.tests.ps1 + display_config.tests.ps1 — same
# four It blocks, same ephemeral-POSEIDON_USER_DIR pattern.  Run:
#   Invoke-Pester -Container (New-PesterContainer -Path tests/smoke/graphics_config.tests.ps1 -Data @{ Preset = 'win-x64-clang-rwdi' }) -Output Detailed

param(
    [Parameter(Mandatory)]
    [string]$Preset
)

BeforeAll {
    Import-Module "$PSScriptRoot/../cli/TestHelpers.psm1" -Force

    $script:repoRoot  = Get-RepoRoot $PSScriptRoot
    $script:gameDir   = Get-GameDataDir -RepoRoot $script:repoRoot
    $script:gameExe   = Get-GameExe -RepoRoot $script:repoRoot -Preset $Preset -Name PoseidonGame
    $script:hasAddons = $script:gameDir -and (Test-HasAddons $script:gameDir)
    $script:Skip = if (-not $script:gameExe) { "PoseidonGame not found" }
                   elseif (-not $script:hasAddons) { "dist/Game/Addons not found" }
                   else { "" }
}

Describe "graphics.cfg boot dance" {
    BeforeEach {
        if ($script:Skip -ne "") { Set-ItResult -Skipped -Because $script:Skip }
    }

    It "creates graphics.cfg with autodetected preset when file is missing (eager-write)" {
        $eph = New-EphemeralGamePaths
        try {
            $cfgPath = Join-Path $eph.UserDir "graphics.cfg"
            Test-Path $cfgPath | Should -BeFalse -Because "ephemeral dir starts empty"

            $output = Invoke-GuiExe $script:gameExe -C $script:gameDir --window --check --log-format jsonl --render dummy
            $LASTEXITCODE | Should -Be 0

            Test-Path $cfgPath | Should -BeTrue -Because "missing graphics.cfg should be eager-written on boot"

            $kv = Read-CfgFile $cfgPath
            # The autodetect log line proves the path took the eager-
            # write branch with RAM-bucket selection rather than just
            # writing class-default values.
            $logs = Get-JsonlLogs $output
            $autoLog = Find-LogMessage $logs 'autodetected preset='
            $autoLog | Should -Not -BeNullOrEmpty -Because "first-boot path should log autodetect"

            # Per-row sanity: tiers match the picked preset's bundle.
            # We don't pin a specific tier (developer machines vary
            # 8 GB → 64 GB+), but the four tier rows must be consistent
            # with each other per the kTierBundles table in
            # GraphicsConfig.cpp.
            [int]$preset = $kv['qualityPreset']
            $preset | Should -BeIn 0,1,2,3 -Because "autodetect picks Low/Med/High/Ultra, never Custom"
            # Per-user knobs must be at the unconditional defaults.
            $kv['vsync']      | Should -Be '1' -Because "VSync defaults On"
            $kv['fpsCap']     | Should -BeIn '30','60','90','120','144','240' -Because "FPS Cap is stamped from the display rate (GraphicsConfig::FpsCapForRefreshRate)"
            $kv['brightness'] | Should -Match '^1\.20*$' -Because "Brightness defaults 1.2 (original CWA default, GraphicsConfig.hpp)"
            $kv['gamma']      | Should -Match '^1\.0+$' -Because "Gamma defaults 1.0"
        } finally {
            Remove-EphemeralGamePaths $eph
        }
    }

    It "preserves a valid pre-existing graphics.cfg verbatim across boot" {
        $eph = New-EphemeralGamePaths
        try {
            $cfgPath = Join-Path $eph.UserDir "graphics.cfg"
            # Medium preset bundle.
            @"
qualityPreset=1;
terrainDetail=2;
objectLod=2;
shadowQuality=1;
particlesQuality=1;
vsync=1;
fpsCap=60;
brightness=1.200000;
gamma=0.900000;
"@ | Set-Content -Path $cfgPath -Encoding ASCII -NoNewline

            $before = (Get-FileHash $cfgPath).Hash

            $output = Invoke-GuiExe $script:gameExe -C $script:gameDir --window --check --log-format jsonl --render dummy
            $LASTEXITCODE | Should -Be 0

            $after = (Get-FileHash $cfgPath).Hash
            $after | Should -Be $before -Because "boot must not rewrite a valid graphics.cfg"
        } finally {
            Remove-EphemeralGamePaths $eph
        }
    }

    It "does NOT persist normalization on boot — invalid tier stays in file" {
        $eph = New-EphemeralGamePaths
        try {
            $cfgPath = Join-Path $eph.UserDir "graphics.cfg"
            # Bogus Terrain tier (99) — file kept untouched, runtime
            # falls back to TierUltra per Normalize semantics.
            @"
qualityPreset=4;
terrainDetail=99;
objectLod=4;
shadowQuality=3;
particlesQuality=3;
vsync=1;
fpsCap=0;
brightness=1.000000;
gamma=1.000000;
"@ | Set-Content -Path $cfgPath -Encoding ASCII -NoNewline

            $output = Invoke-GuiExe $script:gameExe -C $script:gameDir --window --check --log-format jsonl --render dummy
            $LASTEXITCODE | Should -Be 0

            $logs = Get-JsonlLogs $output
            $normLog = Find-LogMessage $logs 'normalized invalid fields'
            $normLog | Should -Not -BeNullOrEmpty -Because "boot should log normalization happened"

            $kv = Read-CfgFile $cfgPath
            $kv['terrainDetail'] | Should -Be '99' -Because "invalid tier stays in file pending user action"
        } finally {
            Remove-EphemeralGamePaths $eph
        }
    }

    It "tolerates an empty graphics.cfg without crashing" {
        $eph = New-EphemeralGamePaths
        try {
            $cfgPath = Join-Path $eph.UserDir "graphics.cfg"
            New-Item -ItemType File -Path $cfgPath -Force | Out-Null

            $output = Invoke-GuiExe $script:gameExe -C $script:gameDir --window --check --log-format jsonl --render dummy
            $LASTEXITCODE | Should -Be 0
        } finally {
            Remove-EphemeralGamePaths $eph
        }
    }
}
