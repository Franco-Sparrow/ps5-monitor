# PS5 Monitor

**Developed by Sparrow · Powered by AI**

PS5 Monitor is a lightweight PS5 hardware and game telemetry payload with an embedded Web UI.  
It is designed to run directly on the PS5 and be viewed from any browser on the same LAN.

No Windows client, Android app, desktop application, or external web server is required.

<img width="1827" height="869" alt="image" src="https://github.com/user-attachments/assets/de5b3d29-793b-46ba-b170-a92f9b8635d4" />

<img width="1850" height="873" alt="image" src="https://github.com/user-attachments/assets/39f01845-5356-413a-b314-afc455ded540" />

<img width="1843" height="870" alt="image" src="https://github.com/user-attachments/assets/939659ed-9b20-44c3-b2ec-17ae8dd445d7" />

<img width="1843" height="865" alt="image" src="https://github.com/user-attachments/assets/c3963612-ea65-4e77-b56d-28123921b7de" />

<img width="1830" height="870" alt="image" src="https://github.com/user-attachments/assets/97ce977f-09d2-4fb7-9a61-b45265dc1251" />

---

## Features

PS5 Monitor currently reports:

- PS5 model
- Firmware / Orbis OS version
- CPU core count
- System uptime
- CPU temperature
- SoC temperature
- CPU clock
- GPU clock
- Fan duty
- SoC power consumption
- CPU utilization per core
- CPU average utilization
- Physical RAM total / used / free
- Current game context
- Current Title ID
- FPS
- Active network configuration
- IPv4 address
- Netmask
- Default gateway
- Primary / secondary DNS
- MAC address
- MTU
- Console internal storage usage
- M.2 SSD storage usage (`/mnt/ext1`)
- USB Extended Storage usage (`/mnt/ext0`)

The Web UI is available on TCP port **9843**.

---

# Project layout

The important directories are:

```text
PS5-Monitor-project/
├── PS5-Monitor/
│   ├── payload/
│   │   ├── main.c
│   │   ├── monitor_perf.c
│   │   ├── monitor_perf.h
│   │   ├── monitor_web.c
│   │   ├── monitor_web.h
│   │   ├── Makefile
│   │   ├── web/
│   │   │   ├── index.html
│   │   │   ├── app.css
│   │   │   └── favicon.png
│   │   └── tools/
│   │       └── embed_web.py
│   └── THIRD_PARTY_NOTICES.md
│
└── deployment/
    ├── ansible.cfg
    ├── inventory/
    │   └── ps5-dev-tools.yml
    ├── playbooks/
    │   ├── prepare_ps5_dev_tools.yml
    │   └── build_ps5_monitor.yml
    ├── roles/
    │   ├── ps5_dev_tools_rootfs/
    │   └── ps5_monitor/
    └── public/
```

`PS5-Monitor/` contains the source code.

`deployment/` contains the Ansible automation used to prepare the isolated build environment and compile the payload.

---

# Build host requirements

The build machine should be a Debian/Ubuntu Linux host with:

- Ansible
- sudo/root privileges for the Ansible `become` tasks
- Internet access during initial setup
- Enough disk space for the Ubuntu rootfs and PS5 SDK

The supplied inventory builds locally:

```text
deployment/inventory/ps5-dev-tools.yml
```

By default it contains:

```yaml
ansible_connection: local
ansible_python_interpreter: "/opt/ansible/venv/bin/python3.13"
```

If the developer's Ansible/Python environment is installed somewhere else, adjust `ansible_python_interpreter` accordingly.

---

# 1. Prepare the PS5 development rootfs

This step **must be done before compiling PS5 Monitor**.

Enter the deployment directory:

```bash
cd PS5-Monitor-project/deployment
```

Run:

```bash
ansible-playbook -i inventory/ps5-dev-tools.yml \
  playbooks/prepare_ps5_dev_tools.yml
```

The playbook creates or repairs the isolated PS5 build environment under:

```text
/var/lib/ps5-dev-build/
```

The important directories are:

```text
/var/lib/ps5-dev-build/
├── ps5-dev-tools/              # Ubuntu build rootfs
├── ps5-dev-tools-workspace/    # persistent source/build workspace
└── ps5-dev-tools-artifacts/    # persistent artifact staging
```

Inside the rootfs the workspace and output directories are mounted as:

```text
/workspace
/output
```

## Rootfs contents

The playbook installs an Ubuntu **24.04 (Noble) amd64** rootfs with the build tools required by the project, including:

```text
clang-18
lld-18
llvm-18
build-essential
make
cmake
ninja-build
meson
pkg-config
python3
python3-pyelftools
git
binutils
rsync
```

It also installs:

```text
PS5 Payload SDK 0.43
```

at:

```text
/opt/ps5-payload-sdk
```

and configures:

```text
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
```

## Reusing the rootfs

The normal setting is:

```yaml
ps5_dev_tools_recreate_rootfs: false
```

so rerunning the preparation playbook reuses and validates the existing rootfs instead of deleting it.

It is safe and useful to rerun the rootfs playbook after a build-host reboot because it also restores the required bind mounts.

## Recreate the rootfs

Only when a completely clean build environment is required:

```bash
ansible-playbook -i inventory/ps5-dev-tools.yml \
  playbooks/prepare_ps5_dev_tools.yml \
  -e ps5_dev_tools_recreate_rootfs=true
```

This recreates only the PS5 rootfs. The persistent workspace and artifact directories are preserved.

A successful preparation ends with:

```text
failed=0
```

and a summary showing the rootfs, workspace, PS5 Payload SDK, and expected product workspace.

---

# 2. Build PS5 Monitor

After the rootfs preparation completes successfully, stay in:

```bash
cd PS5-Monitor-project/deployment
```

Run:

```bash
ansible-playbook -i inventory/ps5-dev-tools.yml \
  playbooks/build_ps5_monitor.yml
```

The build role performs the following steps:

1. Validates the PS5 development rootfs.
2. Validates the PS5 Payload SDK compiler.
3. Checks the PS5 Monitor source tree.
4. Installs the cross-compiled PS5 SQLite dependency when it is missing.
5. Copies the current payload source into the persistent build workspace.
6. Runs `make clean`.
7. Regenerates the embedded Web UI assets.
8. Compiles the payload with `prospero-clang`.
9. Strips the ELF.
10. Publishes a versioned ELF in `deployment/public/`.
11. Generates a SHA-256 checksum.

The SQLite dependency is obtained automatically from the PS5 PacBrew/homebrew library bundle when required. Do **not** install the normal Linux `libsqlite3-dev` package as a substitute for the PS5 cross-compiled library.

A successful build ends with:

```text
failed=0
```

and prints a result similar to:

```text
Product: PS5 Monitor
Monitor version: 1.4.4
Web port: 9843
Published ELF: .../deployment/public/ps5-monitor-v1.4.4.elf
Published SHA256: .../deployment/public/ps5-monitor-v1.4.4.elf.sha256
```

---

# Build output

The internal unversioned build output is:

```text
ps5-monitor.elf
```

The release artifact is versioned and published under:

```text
deployment/public/
```

For version `1.4.4`:

```text
deployment/public/
├── ps5-monitor-v1.4.4.elf
└── ps5-monitor-v1.4.4.elf.sha256
```

Older versioned ELF files can remain in `public/`; new releases do not need to overwrite them.

## Verify the checksum

From the deployment directory:

```bash
cd public
sha256sum -c ps5-monitor-v1.4.4.elf.sha256
```

Expected result:

```text
ps5-monitor-v1.4.4.elf: OK
```

---

# Using PS5 Monitor

Copy or send the versioned ELF to the PS5 using the developer's normal PS5 ELF loader.

Example artifact:

```text
ps5-monitor-v1.4.4.elf
```

After the payload is running, open a browser on a device connected to the same LAN as the PS5:

```text
http://PS5-IP:9843/
```

Example:

```text
http://192.168.88.100:9843/
```

The Web UI is served directly by the payload. No Apache, Nginx, Node.js, PHP, Windows client, or Android application is required.

The payload requests the runtime name:

```text
ps5-monitor.elf
```

---

# Web UI

The Web UI files are located at:

```text
PS5-Monitor/payload/web/
├── index.html
├── app.css
└── favicon.png
```

During every clean build:

```text
tools/embed_web.py
```

generates:

```text
web_assets.c
web_assets.h
```

and embeds the Web UI directly into the ELF.

Do **not** edit `web_assets.c` or `web_assets.h` manually. They are generated files and will be replaced by the next build.

After changing the Web UI, simply run the normal project build playbook again.

When testing a newly loaded payload version in a browser, use a hard refresh if the browser still displays cached content:

```text
Ctrl + F5
```

---

# Updating the project version

The project uses numeric three-part versions:

```text
x.y.z
```

When creating a new release, keep these two variables synchronized.

In:

```text
deployment/inventory/group_vars/all.yml
```

update:

```yaml
ps5_product_version: "1.4.4"
```

And in:

```text
deployment/roles/ps5_monitor/defaults/main.yml
```

update:

```yaml
ps5_monitor_version: "1.4.4"
```

For example, for version `1.4.5`:

```yaml
ps5_product_version: "1.4.5"
ps5_monitor_version: "1.4.5"
```

The resulting artifact will be:

```text
deployment/public/ps5-monitor-v1.4.5.elf
```

---

# Manual access to the build rootfs

For debugging, the rootfs can be entered manually from the Linux build host:

```bash
sudo chroot /var/lib/ps5-dev-build/ps5-dev-tools /bin/bash -l
```

The project workspace inside the chroot is:

```text
/workspace/ps5-monitor
```

A manual build can then be run with:

```bash
cd /workspace/ps5-monitor
make clean
make
```

Normal development should still use the Ansible build playbook so the build and published artifacts remain reproducible.

---

# Typical developer workflow

For a **new build machine**:

```bash
cd PS5-Monitor-project/deployment

ansible-playbook -i inventory/ps5-dev-tools.yml \
  playbooks/prepare_ps5_dev_tools.yml

ansible-playbook -i inventory/ps5-dev-tools.yml \
  playbooks/build_ps5_monitor.yml
```

For an **existing prepared build machine**:

```bash
cd PS5-Monitor-project/deployment

ansible-playbook -i inventory/ps5-dev-tools.yml \
  playbooks/build_ps5_monitor.yml
```

After a **build-host reboot**, rerun the rootfs preparation first so its runtime mounts are restored:

```bash
ansible-playbook -i inventory/ps5-dev-tools.yml \
  playbooks/prepare_ps5_dev_tools.yml
```

Then build normally.

---

# Troubleshooting

## Rootfs validation fails

Run the rootfs preparation playbook again:

```bash
ansible-playbook -i inventory/ps5-dev-tools.yml \
  playbooks/prepare_ps5_dev_tools.yml
```

Do not immediately recreate the rootfs unless repair/revalidation fails.

---

## `sqlite3.h` or `-lsqlite3` is missing

Use the normal build playbook:

```bash
ansible-playbook -i inventory/ps5-dev-tools.yml \
  playbooks/build_ps5_monitor.yml
```

The `ps5_monitor` role checks for the PS5 cross-compiled SQLite headers/library and installs the PacBrew homebrew bundle automatically when necessary.

Do not fix this by installing the host Linux SQLite development package.

---

## Web page does not open

Confirm:

1. The ELF is still running on the PS5.
2. The PC/browser and PS5 are reachable on the same LAN.
3. TCP port `9843` is reachable.
4. The URL uses the current PS5 IP:

```text
http://PS5-IP:9843/
```

---

## New Web UI changes do not appear

Use:

```text
Ctrl + F5
```

to bypass browser cache.

Also confirm a new ELF was actually rebuilt and loaded.

---

# Important paths

| Purpose | Path |
|---|---|
| Rootfs | `/var/lib/ps5-dev-build/ps5-dev-tools` |
| Persistent workspace | `/var/lib/ps5-dev-build/ps5-dev-tools-workspace` |
| Persistent artifact staging | `/var/lib/ps5-dev-build/ps5-dev-tools-artifacts` |
| Project inside chroot | `/workspace/ps5-monitor` |
| PS5 Payload SDK | `/opt/ps5-payload-sdk` |
| Source payload | `PS5-Monitor/payload/` |
| Published releases | `deployment/public/` |
| Web UI | `PS5-Monitor/payload/web/` |
| Web port | `9843/TCP` |

---

# Credits

**Developed by Sparrow**  
**Powered by AI**

Third-party source provenance and notices are kept in:

```text
PS5-Monitor/THIRD_PARTY_NOTICES.md
```
