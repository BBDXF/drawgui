# Platform notes

Measured findings from P1 environment reconnaissance on Linux/WSLg. Everything
here is a recorded measurement, not a guess; see design.md section 12 and
section 10 for how it feeds back into the design.

## SDL3

Already installed: 3.4.2 (`libsdl3-dev` 3.4.2+ds-1ubuntu1). No install needed.
Both `x11` and `wayland` SDL video drivers work under WSLg (`DISPLAY=:0`,
`WAYLAND_DISPLAY=wayland-0`).

## GPU selection on WSL - the non-obvious part

`/dev/dri` is absent under WSL. That looks like "no GPU available", and an
earlier note in this project recorded exactly that conclusion. It is wrong.

WSL exposes GPU access through `/dev/dxg` plus Mesa's `d3d12_dri.so`, and the
choice of driver is controlled by an environment variable, not by device node
presence:

| configuration | result |
|---|---|
| (default, no override) | llvmpipe (LLVM 21.1.8, 256 bits) - software |
| `GALLIUM_DRIVER=d3d12` | D3D12 (Intel(R) Iris(R) Xe Graphics) - hardware |
| `MESA_LOADER_DRIVER_OVERRIDE=d3d12` | llvmpipe - does **not** switch the driver |

GL 4.5 Compatibility, Mesa 26.0.8, when the D3D12 path is selected.

The asymmetry between the two environment variables is the expensive part:
`MESA_LOADER_DRIVER_OVERRIDE` looks like the right knob and silently fails to
switch anything, while `GALLIUM_DRIVER` is the one that actually works. Get
this backwards and you burn an afternoon convinced hardware acceleration is
unavailable.

This matters for more than convenience. design.md section 5.15.7 sets the
baseline hardware floor at "Intel UHD 620 class integrated graphics". An Iris
Xe sits comfortably in that class, so performance numbers measured on this
WSL host under `GALLIUM_DRIVER=d3d12` are representative of the design's
floor rather than being an artifact of a faster machine.

## GL under wayland: a known limitation

`SDL_VIDEODRIVER=wayland` combined with a GL context emits:

```
libEGL warning: failed to get driver name for fd -1
```

`SDL_VIDEODRIVER=x11` combined with GL is clean, no warning. Prefer x11 for
GL work until wayland GL is investigated separately; this is recorded as a
known limitation, not a blocker.

## The `egl*` proc-loader trap

Under x11, `SDL_GL_GetProcAddress` forwards to `glXGetProcAddress`, which
returns a **non-null bogus stub** for function names it does not recognize -
in particular `eglQueryString` and `eglGetCurrentDisplay`. Skia's
`GrGLMakeAssembledInterface` probes both of those while assembling its GL
interface. It calls the stub pointer it was handed, and the process segfaults
with no diagnostic from Skia at all.

The fix belongs in the proc loader that drawgui hands to Skia: return
`nullptr` for any name beginning with `egl`, before ever calling
`SDL_GL_GetProcAddress` on it.

## Ganesh linking

No `-lGL` is required. The `ganesh-gl` prebuilt ships
`GrGLMakeNativeInterface_none.o`; `GrGLMakeNativeInterface()` returns null and
every GL entry point is resolved at runtime through the caller's own proc
loader instead.

`SK_GANESH` and `SK_GL` are switches that control how Skia itself is
*compiled*, not how a consumer of the prebuilt library uses it. The Ganesh API
(`GrDirectContext`, `GrDirectContexts::MakeGL`, etc.) is declared
unconditionally in the headers, with no defines needed on the drawgui side.

## The popup spike

design.md section 5.2 requires that popups (dropdowns, context menus,
tooltips) be real OS windows on desktop, because they must be able to escape
their parent window's bounds - an application-internal overlay layer cannot
do that. This was the P1 spike that section 12 flagged as required before
`PopupHost`'s desktop implementation path could be chosen.

Setup: parent window 400x300, popup requested at position (350,250) with size
200x120 - deliberately placed so it would overhang the parent if popups are
real windows. Measured on **both** the x11 and wayland SDL video drivers:

| check | x11 | wayland |
|---|---|---|
| `SDL_CreatePopupWindow` | OK | OK |
| `SDL_GetWindowParent` linkage | correct | correct |
| reported position / size | (350,250) 200x120 | (350,250) 200x120 |
| overhangs parent bounds | YES (right edge 550 > parent width 400) | YES |
| `SDL_WINDOW_TOOLTIP` | supported | supported |

**Conclusion**: popups created through `SDL_CreatePopupWindow` are real OS
windows that are free to escape the parent's bounds, on both drivers tested.
`PopupHost` (design.md section 5.2) can take the `caps.native_popup == true`
path on Linux desktop. The fallback design.md section 10 lists for this risk
- hand-writing three native popup implementations - is not needed on Linux.

**Limit of this claim**: this was measured on Linux/WSLg only, covering the
x11 and wayland SDL drivers. Windows and macOS are not measured yet. The
risk in design.md section 10 is reduced by this result, not closed; closing
it requires the same spike run on Windows and macOS.
