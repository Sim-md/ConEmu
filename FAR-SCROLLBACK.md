# Mouse-wheel scrollback with hidden Far panels

To scroll console output with the mouse wheel while both Far panels are hidden,
use [Maximus5's Far LuaMacro](https://github.com/ConEmu/ConEmu/pull/2648#issuecomment-7023268234).
This replaces the former ConEmu setting on x86, x64, and ARM64.

1. Start Far with `Far.exe -w` in a regular ConEmu tab and enable a console
   buffer taller than the window.
2. Save the macro from the linked comment as `Macros\scripts\Panel.Wheel.lua`
   in your Far profile and restart Far. This requires Far's LuaMacro plugin.
3. Hide both panels with Ctrl+O and scroll with the wheel.
