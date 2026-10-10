# Changelog

## 1.4.8

- Fixed the RAM-capacity denominator again: `sceKernelGetDirectMemorySize()` is an allocatable direct-memory arena (~11.4 GiB on the tested console), not installed RAM.
- `physical_total` is now 16 GiB for the PS5 unified GDDR6 capacity, with page-table/`hw.physmem` validation when available.
- Added `physical_total_source` to `/api/v1/memory` for diagnostics.
- The RAM usage estimator itself is unchanged from 1.4.7.

## 1.4.7

- Fixed installed RAM capacity detection: `vm.stats.vm.v_page_count` is retained only for diagnostics and is no longer presented as physical RAM.
- Physical RAM capacity now prefers the PS5 direct-memory size (16 GiB on the supported retail PS5), with guarded fallbacks.

- Corrected unified RAM accounting: user-facing RAM no longer adds CPU and GPU page-table proxies.
- System usage now comes from `get_page_table_stats(vm=0, type=1)`.
- Game usage is added from the previously validated `get_page_table_stats(vm=1, type=1)` source when a real game allocation is present.
- GPU `type=2` page-table values remain diagnostic only because shared pages can appear in both CPU and GPU mappings.
- `/api/v1/memory` now exposes `system_vm_used`, `game_vm_used`, and `game_vm_active` for validation.
- Release artifact: `ps5-monitor-v1.4.7.elf`.

## 1.4.5

- Fixed system RAM telemetry being effectively frozen around the same value.
- Preserve the PS5-specific `get_page_table_stats()` memory sample instead of overwriting it with `vm.stats.vm.v_free_count`.
- Added dynamic FreeBSD active/wired/cache fallback only when the PS5 page-table collector is unavailable.
- Added explicit `physical_used`, `physical_available`, `physical_used_percent`, and `memory_source` fields to `/api/v1/memory`.
- Web UI now consumes the authoritative memory values returned by the payload.
- Release artifact: `ps5-monitor-v1.4.5.elf`.

## 1.4.4

- Simplified RAM presentation to one console-wide physical-memory view.
- Removed Game RAM from Game Performance; the block now shows one `RAM Used` value for the console alongside FPS and CPU Average.
- Removed CPU Pool and GPU Pool bars from CPU & Memory.
- CPU & Memory now shows one System Memory bar with used, total, free, and percentage.
- Removed the game-RAM collector and game-RAM fields from `/api/v1/perf` to reduce unnecessary sampling.
- Added monochrome blue navigation icons matching the Web UI theme.
- Kept network throughput fields omitted because the exposed counters do not reliably represent real PS5 WAN/LAN transfer speed.
- Kept DNS, interface, address, gateway, MAC, and MTU monitoring.
- Release artifact: `ps5-monitor-v1.4.4.elf`.

## 1.4.3

- Reordered Dashboard: System Information, Game Performance, then Live Sensors.
- Removed per-core CPU grid from Dashboard; it remains in CPU & Memory.
- Added CPU Average to Game Performance.
- Added Primary/Secondary DNS to Network.
- Network page focuses on active interface configuration instead of unreliable traffic counters.
- CPU/GPU clock display is live-sampled from PS5 APIs; no game-state clock assumptions are used.

## 1.4.2

- Added visual storage usage bars.
- Removed redundant storage Name rows.
- Added Console, M.2 SSD, and USB Extended Storage views.
- Fixed Dashboard CPU flicker by making `/api/v1/cpu` the authoritative CPU source.
- Build-workspace ELF renamed to `ps5-monitor.elf`.

## 1.4.0

- Renamed product to **PS5 Monitor**.
- Product branding: **Developed by Sparrow · Powered by AI**.
- Removed non-monitoring Web modules and client functionality.
- Internal collector bridge binds to loopback only.
- FPS sampling continues whenever a game is resident instead of resetting on transient UNKNOWN context.