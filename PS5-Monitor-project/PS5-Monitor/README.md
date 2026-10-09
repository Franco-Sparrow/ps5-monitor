# PS5 Monitor

**Developed by Sparrow · Powered by AI**

PS5 Monitor is a focused PS5 hardware/game telemetry payload with an embedded Web UI. It does not require a Windows, Android, or desktop client.

## Current version

`1.4.4`

## Web UI

Open from a browser on the same LAN:

```text
http://PS5-IP:9843/
```

## Monitoring scope

- Model / firmware / CPU cores / uptime
- CPU temperature
- SoC temperature
- CPU clock
- GPU clock
- Fan duty
- SoC power
- CPU utilization per core + average
- Total system RAM used / free / total
- Game detection / Title ID
- FPS from `/dev/dce`
- Active network configuration including DNS
- Console storage total / used / free
- M.2 SSD storage at `/mnt/ext1`
- USB Extended Storage at `/mnt/ext0`

The Web UI intentionally contains no file manager, game manager, save manager, shell, package installer, module manager, or desktop/mobile client.

## 1.4.4 behavior

- The Dashboard Game Performance block shows FPS, CPU Average, and one unified RAM-used value for the whole console.
- Game-specific RAM and CPU/GPU memory-pool presentation were removed to avoid implying that the PS5 has separate user-visible RAM banks.
- CPU & Memory shows one system-memory usage bar based on the 16 GB physical total, plus used and free values.
- The left navigation uses monochrome theme-matched icons for Dashboard, System Info, CPU & Memory, Network, and Storage.
- CPU utilization is updated only by the dedicated CPU collector, with the last complete sample held until a new one arrives.
- FPS sampling continues while a game is resident, even if the ShellUI context detector temporarily reports UNKNOWN.
- Network traffic-speed fields remain intentionally omitted because the available counters did not match real router throughput reliably.
- Storage reports Console Storage, M.2 SSD (`/mnt/ext1`), and USB Extended Storage (`/mnt/ext0`) with visual usage bars.
- The running payload requests the name `ps5-monitor.elf`.

## Build

The existing PS5 development rootfs can be reused.

```bash
cd deployment
ansible-playbook -i inventory/ps5-dev-tools.yml playbooks/build_ps5_monitor.yml
```

Expected release artifact:

```text
deployment/public/ps5-monitor-v1.4.4.elf
```

The unversioned ELF produced in the build workspace is:

```text
ps5-monitor.elf
```

## Web port

TCP `9843`.

The internal monitoring command dispatcher is bound to loopback only and accepts only the read-only monitoring commands used by the Web API.

Third-party source provenance is kept in `THIRD_PARTY_NOTICES.md` and is not part of the product UI/branding.
