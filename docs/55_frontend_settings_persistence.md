# Frontend Settings Persistence

Opening a settings dropdown or moving focus between controls previously called the same save path as an actual edit. Each call wrote the frontend preferences and posted a settings-reload message to the embedded game window. The reload could refresh the game layout even though the user had changed nothing.

The settings window now accepts selection changes only from recognized combo boxes with a valid selection, and checkbox changes only on `BN_CLICKED`. The volume handler accepts only its own slider and ignores an unchanged position. Focus, dropdown-open, unknown-control and unrelated scroll notifications do not write preferences or notify the running game.

## Preferred and active controllers

`input.device` records the user's preferred controller. A disconnected preferred device may temporarily lead to another controller being active. Saving volume, video options or another unrelated preference must preserve the preferred device so it can be selected again when connected.

`FrontendSettings::save` stores `savedController` in `input.device`, using the active device only when no preference exists yet. Mapping changes are still stored in the active device's `controller-<device>` section. Explicit controller selection in the settings window updates `savedController` and therefore changes the preference.

## Validation

The ROM-free `launcher_defaults_win32` suite reproduces eight failed assertions with the previous implementation: two preferred-device checks and six notification checks. All pass with the correction. It also verifies that genuine audio checkbox and slider edits persist, controller mappings stay in their own device sections, explicit selection changes the preference, and the seven interface languages and ten cartridge-language options still save correctly.

A separately started process reads the newly saved audio checkbox and volume values immediately, without an additional INI flush. This check does not reproduce the proposed INI-cache failure and does not justify adding a flush to every save.

Run `make build/launcher_defaults_win32.exe` and `build/launcher_defaults_win32.exe`, or run the complete frontend suite with `make test-windows`. The test uses a separate settings file and does not alter the user's preferences. The complete frontend suite passes, including controller repaint stability, fullscreen transitions, DXGI/GDI presentation, audio lifecycle and MP3 storage management.

These corrections do not change guest timing or establish faster cartridge execution. The [host CPU sample profile](54_host_cpu_samples.md) describes the separate performance investigation.
