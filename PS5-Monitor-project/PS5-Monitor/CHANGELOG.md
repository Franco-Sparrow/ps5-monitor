# PS5 Monitor Changelog

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
