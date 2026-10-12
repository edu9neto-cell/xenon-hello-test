# xenon-hello-test

Independent 2D homebrew for Xbox 360 (RGH + XeLL Reloaded) built remotely with
**GitHub Actions** and the official container **`free60/libxenon:latest`**.

- **Etapa 1 (done):** `Hello world!` on the framebuffer + gamepad read. Verified
  running on the console.
- **Etapa 2 (done):** draw a coloured 2D rectangle on a dark background and move
  it with the D-pad. Verified running on the console.
- **Etapa 3 (research only):** how to reach the program from the **Aurora**
  dashboard (the ELF is bare-metal and cannot be launched by Aurora directly;
  see [Launching from Aurora](#launching-from-aurora)).
- **Etapa 4 (this):** a small playable 2D platformer. The scene (sky, ground and
  one platform) is drawn **once**; every frame only the player and its previous
  area are redrawn, and only the touched 32×32 tiles are flushed. Frame time is
  printed on the console (PowerPC timebase), so fps is measured, not guessed.

It does **not** depend on any other project.

## What it does

- `xenos_init(VIDEO_MODE_AUTO)` + `console_init()` — video + framebuffer console
- `xenon_make_it_faster(XENON_SPEED_FULL)` — full clock
- `usb_init()` + `usb_do_poll()` + `get_controller_data(&pad, 0)` — player 1 input
- draws a player (yellow head, green body) with **gravity**, **jump**, and
  collision with the **ground** and one **platform** (`console_pset`)
- **D-pad / left stick** moves; **A or D-pad up** jumps (one jump per press);
  **Guide** or **Y** exits
- prints `fps` and `frame_avg_ms` on the console every ~2.5 s (real measurement)

## Drawing method (and its limitation)

libXenon has **no OpenGL** and **no high-level 2D library**. Its examples are only
`template`, `cube` (needs GPU shaders compiled by hand via `genffs.py`) and
`filebrowser`. So the minimal, real drawing path is the framebuffer console:

- `console.h` exports
  `void console_pset(int x, int y, unsigned char r, unsigned char g, unsigned char b);`
  which writes one 32bpp pixel into the **same Xenos framebuffer** the text
  console uses (tiled BGRA layout and overscan offset are handled internally).
- After CPU writes we flush the data cache (`ppc/cache.h` → `memdcbst`) so the
  display engine sees the pixels, exactly as `console.c` does.

> Improvement over Etapa 2 (which cleared and redrew the whole screen each
> frame): the static scene is painted only once, and each frame we repaint just
> the player's old area and flush the bounding box of the touched 32×32 tiles
> (`flush_tiles`, using tile row stride `cw*128` and column stride 4096, the
> same layout as `console_pset32`).

Trade-offs: this is a CPU pixel-fill (no GPU acceleration, no double buffering),
so the cost is proportional to the drawn area — fine for a small prototype. No
fps is claimed without measuring: the program itself prints its real average
frame time on the console.

## Layout

```
template/source/main.c      # the program (Etapa 4)
template/Makefile           # official libXenon Makefile (produces template.elf32)
.github/workflows/build.yml # remote build in free60/libxenon:latest
README.md
```

## Build (remote, no local toolchain)

The workflow runs inside the official `free60/libxenon:latest` container
(`ubuntu-latest`), exports `DEVKITXENON=/usr/local/xenon`, builds the `template`
directory with `make`, and uploads **`template.elf32`** as the artifact
**`xenon-hello-elf`**.

- Trigger: on `push`, or manually via **Actions → build-xenon-test → Run workflow**.
- Permissions: `contents: read` only. No secrets or tokens are used.

> The build output is `template.elf32` (ELF32 PowerPC big-endian). It is **not**
> named `xenon.elf`; rename it when preparing the USB drive.

## Run on the console (plain XeLL)

1. Latest **Actions** run → job **build** → **Artifacts** → download
   **`xenon-hello-elf`** (a `.zip`).
2. Extract `template.elf32` and rename it to **`xenon.elf`**.
3. Format a USB drive as **FAT32** and copy `xenon.elf` to its **root**.
4. Boot into **XeLL Reloaded** (e.g. power on via **Eject**); it loads
   `xenon.elf` automatically.
5. Expected: dark sky, a ground bar and one floating platform; the player starts
   on the left; **D-pad/analog** moves, **A/D-pad up** jumps, **Guide/Y** exits.
   A line at the bottom of the text header shows measured `fps` / `frame_avg_ms`.

## Launching from Aurora

A libXenon ELF is **bare-metal**: it can only be run by **XeLL**, which Aurora
does not integrate (launching `.elf` from Aurora was only ever a feature
request). So from Aurora the program is reached by going through XeLL, using
**XellLaunch** — a small XEX (by cOz) that boots XeLL. This is a documented,
working community setup (see libxenon.org guide / ConsoleMods).

Confirmed steps:

1. On the USB (FAT32): keep `xenon.elf` at the **root**, and place the
   **XellLaunch** XEX in its own folder (e.g. `Usb0:/XellLaunch/default.xex`,
   sharing the folder with the `xell.bin`/ELFs you want XeLL to find).
   XeLL Reloaded auto-loads `xenon.elf` from the USB root (it also supports
   loading from the HDD).
2. In **Aurora → Settings → Content → Manage Paths**, add the folder that
   contains the XellLaunch folder, set its content type to **Homebrew** and
   choose the scan depth, then back out to re-scan.
3. XellLaunch now appears in the Aurora game/library list as a homebrew title;
   launching it boots XeLL, which then loads `xenon.elf` and runs the prototype.
4. Name/cover in the Aurora coverflow are **not** automatic: cover art is
   assigned per-title with the **Aurora Asset Editor**, and the pop-up dashboard
   shows only the XellLaunch shortcut (the ELF itself never appears as a cover).

Convenience option (DashLaunch): `launch.ini` (`DashLaunch` settings) can map a
controller button straight to the XellLaunch XEX — e.g. `button_Y =>
Usb0:/XellLaunch/default.xex` — so a single button press takes you from the dash
into XeLL and then to the program. This exact pattern (button bound to
`.../XellLaunch/default.xex` with `xenon.elf` on the USB root) is a known
working libxenon setup.

It is **not** possible to make the `.elf` itself appear in Aurora's coverflow
without converting the program to a real XEX (needs the MS XDK) — renaming the
ELF to `.xex` does **not** work.

## Revert to previous etapas

Both previous programs are preserved in git history:

```
git checkout 9e2ae6b -- template/source/main.c    # Etapa 1 (hello world)
git checkout 0d5f77e -- template/source/main.c    # Etapa 2 (rectangle POC)
```

## Status / scope

- Compilation is validated by GitHub Actions; `fps`/`frame_avg_ms` are measured
  **on the console** when the program runs.
- Image/input/performance on a real Xbox 360 with **RGH + XeLL Reloaded** must
  be confirmed on the hardware; this repo cannot verify that itself.

## License / attribution

The `template` example comes from
[Free60Project/xenon-examples](https://github.com/Free60Project/xenon-examples);
libXenon is free software (see
[Free60Project/libxenon](https://github.com/Free60Project/libxenon)).