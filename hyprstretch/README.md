# hyprstretch

A Hyprland plugin to **resize windows without changing their resolution**, automatically stretching the application content to fit the window and scaling mouse input.

Ideal for playing games in stretched resolution (e.g., 4:3 stretched in CS2) or scaling low-resolution apps to fill custom window dimensions.

> [!WARNING]
> **Vibe-Coded Project**: This plugin was built on pure vibes and AI assistance. It uses low-level C++ function hooks into Hyprland internals. It works, but it might break, segfault, or behave unexpectedly when Hyprland updates. Use at your own risk!

## Features

- **Fixed Resolution Sizing**: Resize window bounds while keeping the application's internal rendering resolution untouched.
- **Surface Stretching**: Stretches application buffers to fill the full window geometry without letterboxing.
- **Mouse Motion Scaling**: Corrects mouse cursor coordinates so clicks and aiming line up with stretched visuals.
- **Regex Class Targeting**: Select target applications via regex window class matching.
- **Initial Size (optional)**: Per-app `w`/`h` to set the window size once at launch; later self-resizes are left alone.

---

## Installation

Install via `hyprpm` (see the [repo root](../) for repository setup):

```bash
hyprpm enable hyprstretch
hyprpm reload
```

---

## Configuration

`hyprstretch` exposes functions in Hyprland's Lua bindings. Specify the target application's window class using a regex string:

```lua
if hl.plugin.hyprstretch then
  -- Target games that hate resizing
  hl.plugin.hyprstretch.app({ class = "(cs2|dota2)" })

  -- Target wine/proton windows
  hl.plugin.hyprstretch.app({ class = ".*\\.exe" })

  -- Stretch, but also size the window once at launch (w/h optional, left unset = stretch only)
  hl.plugin.hyprstretch.app({ class = "cs2", w = 1650, h = 1050 })
end
```

Each `app()` call registers a target; multiple calls are supported. `w`/`h` are optional and only applied once on the first resize at launch — subsequent self-resizes by the app are untouched.

### Toggle per window

Turn stretching on/off for the currently focused window via the `hyprstretch.toggle()` Lua function:

```lua
hl.bind("SUPER + J", function() hl.plugin.hyprstretch.toggle() end)
```
