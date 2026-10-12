# Controller Settings Repaint

The Controls settings page polls host input every 30 ms to update the controller illustration. Previously, each paint drew the illustration directly into the window without restoring its background. Antialiased edges and changed button highlights could accumulate drawing remnants, while intermediate drawing operations were visible.

The settings window now fills an offscreen bitmap with the system background, draws the complete controller illustration into it, and copies the invalidated region to the window in one operation. Child controls are excluded from parent drawing through `WS_CLIPCHILDREN`. Input polling and binding capture retain their existing interval and behavior.

The Windows launcher validation checks controller mapping, all six settings categories, rendering previews, and language switching. A pixel comparison of the controller illustration across 20 repeated repaints produces an identical image. Pressing a mapped button changes the image, and releasing it restores the original image exactly. These checks cover stable repainting and highlight cleanup; they do not establish behavior under every display driver or physical controller.

```powershell
make build/launcher_defaults_win32.exe
build/launcher_defaults_win32.exe
```
