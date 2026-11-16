[CmdletBinding()]
param(
    [string]$ToolchainBin = $env:RISCV_TOOLCHAIN_BIN,
    [string]$Python = 'python',
    [ValidateSet('both','serial','clean')][string]$Mode = 'both',
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = $PSScriptRoot
if (-not $ToolchainBin) {
    $cmd = Get-Command riscv-none-elf-gcc -ErrorAction SilentlyContinue
    if ($cmd) { $ToolchainBin = Split-Path $cmd.Source }
    elseif (Test-Path 'F:\Efinity\efinity-riscv-ide-2026.1\toolchain\bin\riscv-none-elf-gcc.exe') {
        $ToolchainBin = 'F:\Efinity\efinity-riscv-ide-2026.1\toolchain\bin'
    } else { throw 'Set RISCV_TOOLCHAIN_BIN or pass -ToolchainBin.' }
}
$ToolchainBin = (Resolve-Path -LiteralPath $ToolchainBin).Path
$tools = @{}
foreach ($name in @('gcc','objcopy','size','nm','objdump')) {
    $path = Join-Path $ToolchainBin "riscv-none-elf-$name.exe"
    if (-not (Test-Path -LiteralPath $path)) { $path = Join-Path $ToolchainBin "riscv-none-elf-$name" }
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing tool: $path" }
    $tools[$name] = $path
}
if (-not (Get-Command $Python -ErrorAction SilentlyContinue)) { throw "Missing Python: $Python" }
# Every native command's exit code is authoritative, including on Windows PowerShell 5.
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $Program @Arguments
    $code = $LASTEXITCODE
    $ErrorActionPreference = $saved
    if ($code -ne 0) { throw "Command failed ($code): $Program $($Arguments -join ' ')" }
}
$required = @('platform/common/start.S','platform/common/trap.S','platform/common/syscalls.c',
    'platform/hdmi_ddr.ld','platform/gpu/2D_DMA.c','tests/test_pack.py','scripts/pack_flash.py','scripts/bin_to_hex.py','scripts/verify_build.py')
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $file))) { throw "Missing local input: $file" }
}
if ($CheckOnly) {
    Invoke-Checked $tools['gcc'] @('--version')
    Write-Host 'Toolchain/local platform preflight PASS; no prepare, compile, or publishing performed.'
    foreach ($file in @('scripts/prepare_assets.py','src/main.c','src/game.c','src/render.c','src/render.h','src/assets.h','tests/test_game.py')) {
        if (-not (Test-Path -LiteralPath (Join-Path $root $file))) { Write-Host "Waiting for: $file" }
    }
    return
}
foreach ($file in @('scripts/prepare_assets.py','src/main.c','src/game.c','src/render.c','src/render.h','src/assets.h','tests/test_game.py')) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $file))) { throw "Application not ready: $file" }
}
# No old-project paths or generated objects are consumed. Unique directory, no -Force reuse.
$run = 'run-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
$stage = Join-Path $root "deliverables/$run"
New-Item -ItemType Directory -Path $stage | Out-Null
Push-Location $root
try {
    if (-not (Test-Path -LiteralPath 'tests/test_triple_pipeline.py')) { throw 'Mandatory pipeline test not ready: tests/test_triple_pipeline.py' }
    if (-not (Test-Path -LiteralPath 'tests/test_speed_1p2.py')) { throw 'Mandatory speed test not ready: tests/test_speed_1p2.py' }
    # Host logic is mandatory and runs before prepare/compile.
    Invoke-Checked $Python @('tests/test_game.py')
    Invoke-Checked $Python @('tests/test_pack.py')
    @{ status='PASS'; tests=@('tests/test_game.py','tests/test_pack.py'); verified_at=[datetime]::UtcNow.ToString('o') } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'host_tests.json') -Encoding utf8
    Invoke-Checked $Python @('scripts/prepare_assets.py')
    Invoke-Checked $Python @('scripts/generate_fire_schedule.py')
    Invoke-Checked $Python @('scripts/generate_alpha_tags.py')
    Invoke-Checked $Python @('scripts/generate_cache_support.py')
    foreach ($file in @('resources/external_assets.manifest.json','src/assets_generated.c','src/assets.c','resources/assets.pack')) {
        if (-not (Test-Path -LiteralPath $file)) { throw "prepare_assets did not produce $file" }
    }
    Invoke-Checked $Python @('tests/test_render.py')
    Invoke-Checked $Python @('tests/test_real_game.py')
    Invoke-Checked $Python @('tests/test_printf_regression.py')
    Invoke-Checked $Python @('tests/test_gpu_cache.py')
    Invoke-Checked $Python @('tests/test_assets_cache.py')
    Invoke-Checked $Python @('tests/test_clock_300mhz.py')
    foreach ($test in @('test_fire_schedule.py','test_alpha_tags.py','test_perf_assets.py','test_alpha_split.py','test_main_metrics.py','test_triple_pipeline.py','test_speed_1p2.py')) {
        Invoke-Checked $Python @('tests/' + $test)
    }
    @{ status='PASS'; tests=@('tests/test_game.py','tests/test_pack.py','tests/test_render.py','tests/test_real_game.py','tests/test_printf_regression.py','tests/test_gpu_cache.py','tests/test_assets_cache.py','tests/test_clock_300mhz.py','tests/test_fire_schedule.py','tests/test_alpha_tags.py','tests/test_perf_assets.py','tests/test_alpha_split.py','tests/test_main_metrics.py','tests/test_triple_pipeline.py','tests/test_speed_1p2.py'); verified_at=[datetime]::UtcNow.ToString('o') } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'host_tests.json') -Encoding utf8
    Invoke-Checked $Python @('scripts/verify_build.py','preflight')
    Invoke-Checked $Python @('scripts/verify_build.py','freeze',"$stage/source_hashes.before.json")
    $flags = @('-march=rv32im_zicsr_zifencei','-mabi=ilp32','-O2','-g','-DUSE_GP','-fcommon',
        '-ffunction-sections','-fdata-sections','-ffreestanding','-fno-lto','-fstack-usage',
        '-DSYSTEM_UART_A_APB=SYSTEM_UART_0_IO_APB','-DSYSTEM_GPIO_A_APB=SYSTEM_GPIO_0_IO_APB',
        '-DSYSTEM_I2C_A_APB=SYSTEM_I2C_0_IO_APB')
    foreach ($inc in @('.','src','platform','platform/gpu','platform/bsp/include','platform/driver')) {
        $flags += '-I' + (Join-Path $root $inc)
    }
    # Compile all top-level application C plus the V10G2.19 GPU driver and startup.
    $sources = @((Get-ChildItem -LiteralPath (Join-Path $root 'src') -File -Filter '*.c' | Sort-Object Name).FullName)
    $sources += @('platform/gpu/2D_DMA.c','platform/common/start.S','platform/common/trap.S','platform/common/syscalls.c') | ForEach-Object { Join-Path $root $_ }
    Copy-Item -LiteralPath 'resources/assets.pack' -Destination (Join-Path $stage 'assets.pack')
    $flavors = if ($Mode -eq 'both') { @('serial','clean') } else { @($Mode) }
    foreach ($flavor in $flavors) {
        $flavorStage = Join-Path $stage $flavor
        New-Item -ItemType Directory -Path $flavorStage | Out-Null
        Copy-Item -LiteralPath (Join-Path $stage 'host_tests.json') -Destination $flavorStage
        Copy-Item -LiteralPath (Join-Path $stage 'source_hashes.before.json') -Destination $flavorStage
        $flavorFlags = $flags + @('-DG24_SERIAL=' + $(if ($flavor -eq 'serial') { '1' } else { '0' }))
        $objects = @()
        foreach ($source in $sources) {
            $relative = $source.Substring($root.TrimEnd('\','/').Length + 1)
            $obj = Join-Path $flavorStage ('objects/' + $relative + '.o')
            New-Item -ItemType Directory -Force -Path (Split-Path $obj) | Out-Null
            Write-Host "CC $relative"
            Invoke-Checked $tools['gcc'] ($flavorFlags + @('-MMD','-MP','-c',$source,'-o',$obj))
            $objects += $obj
        }
        $base = Join-Path $flavorStage 'app'
        $ld = Join-Path $root 'platform/hdmi_ddr.ld'
        Invoke-Checked $tools['gcc'] ($flavorFlags + $objects + @('-o',"$base.elf",'-nostartfiles','-specs=nosys.specs',
            "-Wl,-T,$ld,-Map,$base.map,--gc-sections,--print-memory-usage",'-Wl,--start-group','-lc','-lm','-lgcc','-Wl,--end-group'))
        Invoke-Checked $tools['objcopy'] @('-O','binary',"$base.elf","$base.bin")
        $bytes = (Get-Item -LiteralPath "$base.bin").Length
        if ($bytes -le 0 -or $bytes -gt 2MB) { throw "app size $bytes outside 1..2097152" }
        # Non-burnable app-only check is kept under .hexcheck, not .hex, so only
        # the full application+assets HEX is presented to the operator.
        Invoke-Checked $Python @('scripts/bin_to_hex.py',"$base.bin","$base.hexcheck")
        Copy-Item -LiteralPath (Join-Path $stage 'assets.pack') -Destination (Join-Path $flavorStage 'assets.pack')
        Invoke-Checked $Python @('scripts/pack_flash.py','full',"$base.bin","$flavorStage/assets.pack","$flavorStage/full.flash.bin")
        Invoke-Checked $Python @('scripts/bin_to_hex.py',"$flavorStage/full.flash.bin","$flavorStage/full.flash.hex",'--limit','33554432')
        Invoke-Checked $tools['size'] @("$base.elf")
        Invoke-Checked $Python @('scripts/verify_build.py','verify',$flavorStage,'--toolchain-bin',$ToolchainBin,'--flavor',$flavor)
        Copy-Item -LiteralPath (Join-Path $flavorStage 'full.flash.hex') -Destination (Join-Path $stage ('G2.6_' + $flavor + '.full.flash.hex'))
    }
    Invoke-Checked $Python (@('scripts/verify_build.py','finish',$stage,'--flavors') + $flavors)
    # The sole mutable success pointer is updated atomically only after all gates PASS.
    $pointer = Join-Path $root 'deliverables/latest-success.txt'
    $temp = Join-Path $root "deliverables/latest-success-$run.tmp"
    [IO.File]::WriteAllText($temp, $stage + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temp -Destination $pointer -Force
    Write-Host "SUCCESS: $stage ($($flavors -join ", "))"
} catch {
    # Leave this fresh stage and diagnostics available; never touch latest-success on failure.
    Write-Warning "FAILED stage retained: $stage; latest-success unchanged. $($_.Exception.Message)"
    throw
} finally { Pop-Location }
