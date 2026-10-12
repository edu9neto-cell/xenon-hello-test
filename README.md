# xenon-hello-test

Independent POC for Xbox 360 (RGH + XeLL Reloaded) homebrew built remotely with
**GitHub Actions** and the official container **`free60/libxenon:latest`**.

- **Etapa 1 (done):** `Hello world!` on the framebuffer + gamepad read. Verified
  running on the console.
- **Etapa 2 (this):** draw a coloured 2D rectangle on a dark background and move
  it with the D-pad.

It does **not** depend on any other project.

## What it does

- `xenos_init(VIDEO_MODE_AUTO)` + `console_init()` — video + framebuffer console
- `xenon_make_it_faster(XENON_SPEED_FULL)` — full clock
- `usb_init()` + `usb_do_poll()` + `get_controller_data(&pad, 0)` — read player 1
- draws a filled **rectangle** (`console_pset`) on a dark background
- **D-pad** moves the rectangle; **Guide** or **Y** exits

## Drawing method (and its limitation)

libXenon has **no OpenGL** and **no high-level 2D library**. Its examples are only
`template`, `cube` (needs GPU shaders compiled by hand via `genffs.py`) and
`filebrowser`. So the minimal, real drawing path is the framebuffer console:

- `console.h` exports
  `void console_pset(int x, int y, unsigned char r, unsigned char g, unsigned char b);`
  which writes one 32bpp pixel into the **same Xenos framebuffer** the text
  console uses (tiled BGRA layout and overscan offset are handled internally).
- After CPU writes we flush the data cache (`ppc/cache.h` → `memdcbst`) over the
  whole framebuffer, exactly as `console.c` does, so the display engine sees the
  pixels.

Trade-offs: this is a CPU pixel-fill (no GPU acceleration, no double buffering),
which is fine for a rectangle but would be too slow for real graphics. A GPU path
would require hand-written Xenon shaders, which is out of scope for this POC.

## Layout

```
template/source/main.c      # the POC program (Etapa 2)
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

## Run on the console

1. Latest **Actions** run → job **build** → **Artifacts** → download
   **`xenon-hello-elf`** (a `.zip`).
2. Extract `template.elf32` and rename it to **`xenon.elf`**.
3. Format a USB drive as **FAT32** and copy `xenon.elf` to its **root**.
4. Boot into **XeLL Reloaded** (e.g. power on via **Eject**); it loads
   `xenon.elf` automatically.
5. Expected: a bright rectangle on a dark background; **D-pad** moves it;
   **Guide/Y** exits.

## Revert to Etapa 1 (Hello world)

The Etapa 1 program is preserved in git history. To restore it:

```
git checkout 9e2ae6b -- template/source/main.c
git commit -m "revert to Etapa 1 hello world"
```

## Status / scope

- Compilation is validated by GitHub Actions (**not** by running on a console).
- Image/input behaviour must be confirmed on a real Xbox 360 with **RGH + XeLL
  Reloaded**; this repo does not and cannot verify that.

## License / attribution

The `template` example comes from
[Free60Project/xenon-examples](https://github.com/Free60Project/xenon-examples);
libXenon is free software (see
[Free60Project/libxenon](https://github.com/Free60Project/libxenon)).
