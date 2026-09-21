# hyprkbptr

A Hyprland plugin to **drive the mouse with the keyboard**. While active, the arrow keys move the cursor (with acceleration while held), `PageUp`/`PageDown` act as left/right mouse buttons, and a half-transparent white crosshair overlay shows the cursor position.

All other keys pass through to applications untouched.

> [!WARNING]
> **Vibe-Coded Project**: This plugin was built on pure vibes and AI assistance. It uses low-level C++ function hooks into Hyprland internals. It works, but it might break, segfault, or behave unexpectedly when Hyprland updates. Use at your own risk!

## Behavior

- **Inactive by default.** Nothing is intercepted until you toggle it on.
- **Arrow keys** move the cursor relative to its position. Speed ramps up the longer a key is held (acceleration), so taps give precise nudges and holds cross the screen. Diagonals work by holding two arrows.
- **PageUp** holds the **left** mouse button, **PageDown** holds the **right** mouse button. Hold the key to drag, release to drop — same as a physical button.
- **Super stands down.** While Super is held, kbptr passes every key through, so your `SUPER + ...` keybinds keep working. Key releases always clean up tracked state, so nothing gets stuck.
- **Overlay.** While active, each monitor shows fullscreen horizontal + vertical crosshair lines through the cursor plus a small cross marker at the cursor itself, in white at 50% opacity.
- Movement is routed through the normal input pipeline, so hover, window focus, and relative-pointer motion (games) behave exactly like a real mouse.

---

## Installation

Install via `hyprpm` (see the [repo root](../) for repository setup):

```bash
hyprpm enable hyprkbptr
hyprpm reload
```

---

## Configuration

Toggle it from your `hyprland.lua`:

```lua
-- Toggle mouse-keys mode (pick any bind you like)
hl.bind("SUPER + O", function() if hl.plugin.hyprkbptr then hl.plugin.hyprkbptr.toggle() end end)
```

Available Lua functions under `hl.plugin.hyprkbptr`:

| Function | What it does |
| --- | --- |
| `toggle()` | Switch active/inactive, returns the new state. |
| `enable()` | Activate. |
| `disable()` | Deactivate (also releases any held mouse buttons). |

There is also a `hyprkbptr:toggle` dispatcher registered for keybind use.

---

## Tuning

Speeds are currently hardcoded in `main.cpp`:

| Constant | Default | Meaning |
| --- | --- | --- |
| `MOVE_BASE_SPEED` | `420 px/s` | Speed right after key press. |
| `MOVE_ACCEL` | `2800 px/s²` | Acceleration while held. |
| `MOVE_MAX_SPEED` | `2500 px/s` | Speed cap. |
| `TICK_STEP` | `8 ms` | Movement update interval. |

Rebuild and reload the plugin after changing them.
