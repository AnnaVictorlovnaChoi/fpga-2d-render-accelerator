[CmdletBinding()]
param(
    [string]$ToolchainBin = $env:RISCV_TOOLCHAIN_BIN,
    [string]$Python = 'python',
    [string]$OutputRoot = $env:G225_OUTPUT_ROOT,
    [ValidateSet(0,1)][int]$ProbeEnabled = 0,
    [ValidateSet(0,1)][int]$SchedulerWfiEnabled = 0,
    [ValidateSet(0,1)][int]$RuntimeLogEnabled = 0,
    [switch]$CheckOnly,
    [switch]$PrebuildOnly,
    [ValidateSet('source/resources/test')][string]$FinalStop
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = $PSScriptRoot
if (-not $OutputRoot) { $OutputRoot = Join-Path (Split-Path $root -Parent) '临时开发1-性能增强/g225/build' }
$env:G225_OUTPUT_ROOT = $OutputRoot
$env:PYTHONPYCACHEPREFIX = Join-Path $OutputRoot 'pycache'
$mapVersion = & $Python -c "import sys;sys.path.insert(0,r'$root/scripts');from g225_contract import MAP_VERSION;print(MAP_VERSION)"
if ($LASTEXITCODE -ne 0) { throw 'Current map identity unavailable' }
$platformJson = & $Python -c "import sys,json;sys.path.insert(0,r'$root/scripts');from g225_contract import platform_contract;print(json.dumps(platform_contract(r'$root')))"
if ($LASTEXITCODE -ne 0) { throw 'Current soc.h frequency/cache contract failed' }
$platformConfig = $platformJson | ConvertFrom-Json
if (-not $ToolchainBin) {
    $cmd = Get-Command riscv-none-elf-gcc -ErrorAction SilentlyContinue
    if ($cmd) { $ToolchainBin = Split-Path $cmd.Source }
    elseif (Test-Path 'F:\Efinity\efinity-riscv-ide-2026.1\toolchain\bin\riscv-none-elf-gcc.exe') {
        $ToolchainBin = 'F:\Efinity\efinity-riscv-ide-2026.1\toolchain\bin'
    } else { throw 'Set RISCV_TOOLCHAIN_BIN or pass -ToolchainBin (directory containing riscv-none-elf-gcc).' }
}
$gcc = Join-Path $ToolchainBin 'riscv-none-elf-gcc.exe'
$objcopy = Join-Path $ToolchainBin 'riscv-none-elf-objcopy.exe'
$size = Join-Path $ToolchainBin 'riscv-none-elf-size.exe'
$objdump = Join-Path $ToolchainBin 'riscv-none-elf-objdump.exe'
foreach ($tool in @($gcc,$objcopy,$size,$objdump)) { if (-not (Test-Path $tool)) { throw "Missing tool: $tool" } }
if (-not (Get-Command $Python -ErrorAction SilentlyContinue)) { throw "Missing Python: $Python" }
$platformFiles = @('platform/gpu/2D_DMA.c','platform/gpu/2D_DMA.h','platform/gpu/userDef.h',
    'platform/common/start.S','platform/common/trap.S','platform/common/syscalls.c',
    'platform/bsp/include/bsp.h','platform/bsp/include/soc.h','platform/driver/type.h',
    'platform/hdmi_ddr.ld','platform/v8_platform.h','scripts/bin_to_hex.py')
foreach ($file in $platformFiles) { if (-not (Test-Path (Join-Path $root $file))) { throw "Missing local dependency: $file" } }
$appFiles = @('src/enemy.c','src/enemy.h','src/enemy_gpu.c','src/enemy_gpu.h','src/enemy_data.c','src/enemy_data.h','resources/enemy/external_streams.json','src/newwave_state.c','src/newwave_state.h','src/wave_helper.c','src/wave_helper.h','src/countdown_data.c','src/countdown_data.h','resources/countdown/external_streams.json','src/player_support.c','src/player_support.h','src/visible.c','src/visible.h','src/main.c','src/pause.c','src/pause.h','src/runtime.c','src/runtime.h','src/player.c','src/renderer.c','src/runtime_data.c','src/runtime_data.h','src/external_assets.c','src/external_assets.h','src/flash_storage.c','src/puff.c','src/puff.h','src/flow_data.c','src/flow_data.h','resources/flow/external_streams.json')
$appFiles += @('src/cpu_probe.c','src/cpu_probe.h','src/cpu_scheduler.c','src/cpu_scheduler.h','src/render_templates.c','src/render_templates.h','src/game_rules.c','src/game_rules.h','src/hud.c','src/hud.h','src/hud_data.c','resources/hud/external_streams.json')
$missing = @($appFiles | Where-Object { -not (Test-Path (Join-Path $root $_)) })
if ($CheckOnly) {
    & $gcc --version
    if ($LASTEXITCODE -ne 0) { throw 'Compiler preflight failed' }
    Write-Host 'Platform/toolchain preflight OK. No firmware was generated.'
    if ($missing.Count) { Write-Host ('Waiting for application/assets: ' + ($missing -join ', ')) }
    else { Write-Host 'Application source names are present; this is not a compile/link validation.' }
    return
}
if ($missing.Count) { throw ('Application/assets incomplete; no build attempted: ' + ($missing -join ', ')) }
if (-not $PrebuildOnly) {
    if ($ProbeEnabled -ne 0) { throw 'Formal probe must be zero' }
    if ($SchedulerWfiEnabled -ne 0) { throw 'Formal WFI disabled: new frequency/cache confirmation grants no WFI authorization' }
    if ($FinalStop -ne 'source/resources/test') { throw 'Parent source/resources/test FINALSTOP required before formal build' }
    $finalReport = Join-Path $root 'docs/test_g225_final.json'
    if (-not (Test-Path $finalReport) -or (Get-Content -Raw $finalReport | ConvertFrom-Json).status -ne 'PASS') { throw 'G2.25 functional aggregate PASS required before build' }
} elseif ($FinalStop) { throw 'PrebuildOnly cannot carry formal FINALSTOP' }
# Parent message NEXT/PREBUILD is execution authorization; flags do not grant it.
# Frozen build never rewrites consumer inputs. Canonical pack preparation is separate.
& $Python (Join-Path $root 'scripts/external_manifest.py') (Join-Path $root 'external_assets.manifest.json') --check
if ($LASTEXITCODE -ne 0) { throw 'Frozen external consumer manifest mismatch' }
# Always compile every TU: no stale objects/header dependency issue and no LTO
# across original ISR-shared driver state. Objects preserve relative source paths.
$build = $OutputRoot
$runPrefix = if ($PrebuildOnly) { 'precheck-' } else { 'run-' }
$stage = Join-Path $build ($runPrefix + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + "-log$RuntimeLogEnabled")
New-Item -ItemType Directory -Force $stage | Out-Null
$config = [ordered]@{project='G2.25_V10Display';pipeline_model='one_pending_plus_render_ready';alpha_count_scope='exclude_persistent_private_cache_prep';alpha_display_scope='visible_game_objects_with_blended_support';sprite_display_policy='identity_to_400_then_double_excess';fps_display_policy='count_curve_58_59_60_below450_linear60to45_until600';display_policy_evidence='HUD_MAPPING_CONFIGURATION_NOT_MEASURED_FPS';opaque_diagnostic_glyphs=$true;probe_enabled=$ProbeEnabled;scheduler_wfi_enabled=$SchedulerWfiEnabled;sleep_feature_capability='DISABLED_PENDING_USER_AUTHORIZATION';hardware_audit='NEW_RTL_NOT_AUDITED';prior_hardware_audit='RTL_NO_STALL_PATH';scheduler_wfi_supported=$false;cpu_deadline_polling=$true;scheduler_timer_irq_enabled=$true;deadline_clocksource='CLINT';deadline_poll_source='RAM_DUE_OR_VSYNC';diagnostic_only=[bool]$PrebuildOnly;status='INCOMPLETE';stack_bytes=4096}
foreach ($property in $platformConfig.PSObject.Properties) { $config[$property.Name]=$property.Value }
$config | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $stage 'build_config.json')
& $Python (Join-Path $root 'scripts/source_inputs.py') (Join-Path $stage 'source_hashes.before.json')
if ($LASTEXITCODE -ne 0) { throw 'Initial source hash inventory failed' }

$includes = @($root,(Join-Path $root 'src'),(Join-Path $root 'platform'),(Join-Path $root 'platform/gpu'),
    (Join-Path $root 'platform/bsp/include'),(Join-Path $root 'platform/driver'))
$flags = @('-march=rv32im_zicsr_zifencei','-mabi=ilp32','-O2','-g','-DUSE_GP','-fcommon',
    '-ffunction-sections','-fdata-sections','-ffreestanding','-fno-lto','-fstack-usage','-DWAVE_SCHEDULE_DEFAULT_ASSET=0',
    '-DSYSTEM_UART_A_APB=SYSTEM_UART_0_IO_APB','-DSYSTEM_GPIO_A_APB=SYSTEM_GPIO_0_IO_APB',
    '-DSYSTEM_I2C_A_APB=SYSTEM_I2C_0_IO_APB')
$flags += "-DAD_RUNTIME_LOG=$RuntimeLogEnabled"
$flags += "-DAD_PERF_VERBOSE=0"
$flags += "-DAD_CPU_PROBE=$ProbeEnabled"
$flags += "-DAD_SCHEDULER_WFI=$SchedulerWfiEnabled"
$config['runtime_log_enabled']=$RuntimeLogEnabled
$config['compile_flags']=@($flags)
$config | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $stage 'build_config.json')
foreach ($inc in $includes) { $flags += ('-I' + $inc) }
$sources = @((Get-ChildItem -LiteralPath (Join-Path $root 'src') -File -Recurse | Where-Object { $_.Extension -cin @('.c','.S') } | Sort-Object FullName).FullName)
# Explicit probe TU ownership: exactly one compile and its object joins the link list.
$probeSource = Join-Path $root 'src/cpu_probe.c'
$explicitSources = @('src/main.c','src/game_rules.c','src/hud.c','src/hud_data.c','src/cpu_probe.c','src/cpu_scheduler.c','src/render_templates.c','src/render_metrics.c') | ForEach-Object { Join-Path $root $_ }
$sources = @($sources | Where-Object { $_ -notin $explicitSources }) + $explicitSources
$sources += @('platform/gpu/2D_DMA.c','platform/common/start.S','platform/common/trap.S','platform/common/syscalls.c') | ForEach-Object { Join-Path $root $_ }
# External pack is the sole resource owner: never silently link legacy blob assembly.
$sources = @($sources | Where-Object { [IO.Path]::GetFileName($_) -notmatch '^(runtime|arrow|scene|dragon|assets)_blob\.S$' })
foreach ($src in $sources) {
    if (Select-String -LiteralPath $src -Pattern '\.incbin\b' -Quiet) { throw "Embedded resource .incbin forbidden in app ELF: $src" }
}
$objects = @()
$compileCommands = @()
Push-Location $root # assets_blob.S .incbin paths are project-root relative
try {
    foreach ($src in $sources) {
        # All sources are rooted beneath this project. Substring is compatible
        # with Windows PowerShell 5 / .NET Framework (no Path.GetRelativePath).
        $prefix = $root.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
        if (-not $src.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Source outside project: $src" }
        $relative = $src.Substring($prefix.Length)
        $obj = Join-Path $stage ($relative + '.o')
        New-Item -ItemType Directory -Force (Split-Path $obj) | Out-Null
        Write-Host "CC $relative"
        # PowerShell 5 may turn native stderr warnings into ErrorRecords.
        # Keep diagnostics visible; only the compiler exit code decides success.
        $ErrorActionPreference = 'Continue'
        & $gcc @flags "-fopt-info-inline-optimized=$obj.inline.txt" "-fdump-ipa-cgraph=$obj.cgraph.txt" '-MMD' '-MP' '-c' $src '-o' $obj 2> "$obj.stderr.txt"
        $compileExit = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ((Get-Item "$obj.stderr.txt").Length) { Get-Content -LiteralPath "$obj.stderr.txt" | Write-Host }
        if ($compileExit -ne 0) { throw "Compilation failed: $relative" }
        $objects += $obj
        $compileCommands += [ordered]@{source=$relative.Replace('\','/');flags=@($flags);object=$obj;object_sha256=(Get-FileHash $obj -Algorithm SHA256).Hash}
    }
    $compileCommands | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $stage 'compile_commands.json')
    $base = Join-Path $stage (Split-Path $root -Leaf)
    $ld = Join-Path $root 'platform/hdmi_ddr.ld'
    # Windows PowerShell 5 surfaces native stderr warnings as ErrorRecords.
    # Preserve linker diagnostics, but decide success only by its exit code.
    $ErrorActionPreference = 'Continue'
    & $gcc @flags @objects '-o' "$base.elf" '-nostartfiles' '-specs=nosys.specs' "-Wl,-T,$ld,-Map,$base.map,--gc-sections,--print-memory-usage" '-Wl,--start-group' '-lc' '-lm' '-lgcc' '-Wl,--end-group'
    $linkExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($linkExit -ne 0) { throw 'Link failed' }
    & $objcopy '-O' 'binary' "$base.elf" "$base.bin"
    if ($LASTEXITCODE -ne 0) { throw 'BIN conversion failed' }
    $bytes = (Get-Item "$base.bin").Length
    if ($bytes -le 0 -or $bytes -gt 2MB) { throw "BIN is $bytes bytes; boot budget is 1..2097152 bytes. Not publishing artifacts." }
    & $Python (Join-Path $root 'scripts/bin_to_hex.py') "$base.bin" "$base.hex"
    if ($LASTEXITCODE -ne 0) { throw 'Streaming HEX conversion failed' }
    & $size "$base.elf"
    if ($LASTEXITCODE -ne 0) { throw 'ELF size check failed' }
    & $objdump '-d' "$base.elf" | Set-Content -Encoding utf8 (Join-Path $stage 'rv32_disassembly.txt')
    if ($LASTEXITCODE -ne 0) { throw 'RV32 disassembly failed' }
    & $Python (Join-Path $root 'scripts/rv32_hotspots.py') (Join-Path $stage 'rv32_disassembly.txt') (Join-Path $stage 'rv32_hotspots.json')
    if ($LASTEXITCODE -ne 0) { throw 'RV32 static hotspot report failed' }
    # Compiler-derived ABI sizes from actual current source; never host sizeof.
    $probe = Join-Path $stage 'g225_sizes.c'
    $enemySource = (Join-Path $root 'src/enemy.c').Replace('\','/')
    $pauseHeader = (Join-Path $root 'src/pause.h').Replace('\','/')
    @("#include `"$enemySource`"", "#include `"$pauseHeader`"", '#include "hud.h"', '#include "renderer.h"', '#include "render_metrics.h"', 'char g225_size_world[sizeof(RendererSnapshot)];', 'char g225_size_metrics[sizeof(RenderMetrics)];', 'char g225_size_metrics_snapshot[sizeof(RenderMetricsSnapshot)];', 'char g225_size_hud[sizeof(HudState)];', 'char g225_size_player[sizeof(Player)];', 'char g225_size_system[sizeof(System)];', 'char g225_size_pause[sizeof(PauseSnapshot)];', 'char g225_size_pause_control[sizeof(PauseControl)];', 'char g225_size_snapshot[sizeof(EnemySnapshot)];', 'char g225_size_hot[sizeof(HotMob)];', 'char g225_size_cold[sizeof(ColdMob)];', 'char g225_size_visual[sizeof(VisualCache)];', 'char g225_size_cursor_cache[sizeof(mob_route)];') | Set-Content -Encoding utf8 $probe
    & $gcc @flags '-S' $probe '-o' (Join-Path $stage 'g225_sizes.s')
    if ($LASTEXITCODE -ne 0) { throw 'Current runtime ABI sizeof probe failed' }
    # Exact owner-local metadata sizes; these probe objects never join APP.
    foreach ($owner in @('main','renderer','render_metrics','scene','hud')) {
        $ownerSource = (Join-Path $root "src/$owner.c").Replace('\','/')
        $expressions = if ($owner -eq 'main') { @('pipeline') } elseif ($owner -eq 'renderer') { @('submitted_enemy','submitted_world','submitted_valid','hud_plan','pause_world_metadata','pause_metadata_valid','collected_alpha_bits','collected_alpha_count','collecting_alpha','pause_alpha','flow_alpha','flow_world_alpha','pause_player_alpha','flow_player_alpha') } elseif ($owner -eq 'scene') { @('built_alpha_mask') } elseif ($owner -eq 'hud') { @('display_index','display_epoch') } else { @('alpha_bits','frozen_alpha','pending_player_alpha','completed_player_alpha') }
        $lines = @("#include `"$ownerSource`"")
        foreach ($expression in $expressions) { $lines += "char g225_size_${expression}[sizeof($expression)];" }
        $ownerProbe = Join-Path $stage "g225_${owner}_sizes.c"
        $lines | Set-Content -Encoding utf8 $ownerProbe
        & $gcc @flags '-S' $ownerProbe '-o' (Join-Path $stage "g225_${owner}_sizes.s")
        if ($LASTEXITCODE -ne 0) { throw "Current $owner static sizeof probe failed" }
    }
    & $Python (Join-Path $root 'scripts/stack_report.py') $stage
    if ($LASTEXITCODE -ne 0) { throw 'Stack report safety bound failed' }
    & $Python (Join-Path $root 'scripts/verify_elf.py') "$base.elf"
    if ($LASTEXITCODE -ne 0) { throw 'Final ELF range/resource gate failed' }
    & $Python (Join-Path $root 'scripts/source_inputs.py') (Join-Path $stage 'source_hashes.before.json') --compare
    if ($LASTEXITCODE -ne 0) { throw 'Build inputs changed before completion; refusing success' }
    if ($PrebuildOnly) {
        Write-Host "NONPUBLISHING PRECHECK PASS: $base; no formal manifest/latest-success pointer."
        return
    }
    Copy-Item -LiteralPath (Join-Path $stage 'source_hashes.before.json') -Destination (Join-Path $stage 'source_hashes.json')
    $manifest = [ordered]@{ built_at=(Get-Date -Format o); toolchain=$gcc; architecture='rv32im_zicsr_zifencei'; abi='ilp32'; optimization='O2'; project='G2.25_V10Display';pipeline_model='one_pending_plus_render_ready';alpha_count_scope='exclude_persistent_private_cache_prep';alpha_display_scope='visible_game_objects_with_blended_support';sprite_display_policy='identity_to_400_then_double_excess';fps_display_policy='count_curve_58_59_60_below450_linear60to45_until600';display_policy_evidence='HUD_MAPPING_CONFIGURATION_NOT_MEASURED_FPS';opaque_diagnostic_glyphs=$true; map_version=$mapVersion; probe_enabled=$ProbeEnabled;scheduler_wfi_enabled=$SchedulerWfiEnabled;sleep_feature_capability='DISABLED_PENDING_USER_AUTHORIZATION';hardware_audit='NEW_RTL_NOT_AUDITED';prior_hardware_audit='RTL_NO_STALL_PATH';scheduler_wfi_supported=$false;cpu_deadline_polling=$true;scheduler_timer_irq_enabled=$true;deadline_clocksource='CLINT';deadline_poll_source='RAM_DUE_OR_VSYNC'; runtime_log_enabled=$RuntimeLogEnabled;compile_flags=@($flags | Where-Object { -not $_.StartsWith('-I') });initial_hp=160;initial_coins=1000;max_waves=3;stamina=8;melee_action_scale='13/10';ranged_action_scale='3/2';action_scale_relative_to='G221';player_arrow_velocity_scale='unchanged';speed_scale='3/5'; wave_scale='13/10'; wave_default_asset=0; wave_default_profile='standard'; cpu_budget_us=5000; target_enemy=600; target_enemy_unit='count'; comparison='strictly_less_than'; cpu_budget_scope='total_cpu_work'; board_cpu_budget='NOT_VERIFIED'; finalstop=$FinalStop; runtime_bytes=95MB; binary_bytes=$bytes; files=@{} }
    foreach ($property in $platformConfig.PSObject.Properties) { $manifest[$property.Name]=$property.Value }
    foreach ($ext in @('elf','bin','hex','map')) { $manifest.files[$ext] = (Get-FileHash "$base.$ext" -Algorithm SHA256).Hash }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $stage 'build_manifest.json')
    # Latest pointer updates only after all steps succeed; failed runs cannot impersonate a new firmware.
    $stage | Set-Content -Encoding utf8 (Join-Path $build "latest-success-log$RuntimeLogEnabled.txt")
    Write-Host "SUCCESS: $base.{elf,bin,hex,map}; BIN=$bytes bytes"
} finally { Pop-Location }
