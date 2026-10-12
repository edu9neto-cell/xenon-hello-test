# xenon-hello-test

Minimal, independent test to prove the **remote build chain** for an Xbox 360
(RGH + XeLL Reloaded) homebrew ELF, using **GitHub Actions** and the official
container **`free60/libxenon:latest`**.

It does **not** depend on any other project. It shows text on the framebuffer
and reads the gamepad.

## What it does

Based on the official `template` example from
[Free60Project/xenon-examples](https://github.com/Free60Project/xenon-examples):

- `xenos_init(VIDEO_MODE_AUTO)` + `console_init()` — initialize the framebuffer console
- prints `Hello world!` / `Press the Guide button or Y to exit.`
- `usb_init()` + `usb_do_poll()` + `get_controller_data(&pad, 0)` — read player 1
- exits when the **Guide** or **Y** button is pressed

## Layout

```
template/source/main.c      # the test program
template/Makefile           # official libXenon Makefile (produces template.elf32)
.github/workflows/build.yml # remote build in free60/libxenon:latest
README.md
```

## Build (remote, no local toolchain)

The workflow runs inside the official `free60/libxenon:latest` container
(`ubuntu-latest`), exports `DEVKITXENON=/usr/local/xenon`, builds the
`template` directory with `make`, and uploads the resulting **`template.elf32`**
as the artifact **`xenon-hello-elf`**.

- Trigger: on `push`, or manually via **Actions → build-xenon-test → Run workflow**.
- Permissions: `contents: read` only. No secrets or tokens are used.

> The build output is `template.elf32` (an ELF32 PowerPC big-endian executable).
> It is **not** named `xenon.elf`; rename it when preparing the USB drive.

## Get the artifact and run it on the console

1. Open the latest **Actions** run → job **build** → section **Artifacts** →
   download **`xenon-hello-elf`** (a `.zip`).
2. Extract `template.elf32` and rename it to **`xenon.elf`**.
3. Format a USB drive as **FAT32** and copy `xenon.elf` to its **root**.
4. On the console, boot into **XeLL Reloaded** (e.g. power on via **Eject**).
   XeLL loads `xenon.elf` automatically.
5. Expected: `Hello world! / Press the Guide button or Y to exit.` on screen,
   and the controller responds (Guide/Y exits).

## Status / scope

- Compilation is validated by GitHub Actions (**not** by running on a console).
- Running on a real Xbox 360 requires **RGH + XeLL Reloaded**; this repo does
  not and cannot verify that.

## License / attribution

The `template` example comes from
[Free60Project/xenon-examples](https://github.com/Free60Project/xenon-examples);
libXenon is free software (see
[Free60Project/libxenon](https://github.com/Free60Project/libxenon)).
