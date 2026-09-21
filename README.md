# hypr-r-r-r

A collection of Hyprland plugins, installable via `hyprpm`.

| Plugin | What it does |
| --- | --- |
| [hyprstretch](hyprstretch/) | Resize windows without changing their resolution (stretched resolutions for games). |
| [hyprkbptr](hyprkbptr/) | Drive the mouse with arrow keys, with a crosshair overlay. |

## Installation

```bash
hyprpm add https://github.com/Rommmmaha/hypr-r-r-r

hyprpm enable hyprstretch
hyprpm enable hyprkbptr

hyprpm reload
```

Enable only the plugins you want. See each plugin's directory for configuration.

> **Note:** make sure you have `hl.on("hyprland.start", function() hl.exec_cmd("hyprpm reload") end)` in your `hyprland.lua` so plugins load automatically on startup.

## Testing locally

```bash
./reload.sh load <plugin-name>     # build + load, e.g. ./reload.sh load hyprkbptr
./reload.sh restore <plugin-name>  # unload dev build, restore hyprpm versions
```
