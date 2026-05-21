# Windows Update Blocker
**Author:** Ari Sohandri Putra  
**Version:** 2.0  
**Build:** Visual Studio 2010 C++

---

## What's New in v2.0

### Bug Fixes
- **Error Code 5 (Access Denied) resolved** - The error was caused by `WaaSMedicSvc`
  (Windows Update Medic Service), which is a *protected* system service on Windows 10/11.
  It uses a special self-healing mechanism that prevents it from being disabled via
  standard `ChangeServiceConfig` calls, even with full Administrator rights.
  
  **Fix applied:**
  - The core service `wuauserv` (Windows Update) and `bits` / `UsoSvc` are disabled as normal.
  - `WaaSMedicSvc` is attempted but silently skipped on failure — it does not block the overall operation.
  - Registry Group Policy keys (`NoAutoUpdate`, `AUOptions`, `DisableWindowsUpdateAccess`)
    are written to provide a second layer of blocking.
  - The toggle state now reflects actual service status, not just the last operation result.

### Auto Administrator (No More Right-Click!)
- The `app.manifest` file sets `level="requireAdministrator"`.
- The `resource.rc` file embeds this manifest into the `.exe` using `RT_MANIFEST`.
- **Result:** Windows automatically shows the UAC elevation prompt when you
  double-click the `.exe`. No need to right-click → "Run as administrator".
- A fallback `ShellExecute("runas", ...)` is also in code for edge cases.

### UI Redesign - Light & Classic Modern
- Replaced dark theme with a clean **light** palette (white, soft blue-grey, green/red accents).
- Classic modern layout: header strip with shield icon + title, white card panel, footer bar.
- Custom-drawn toggle switch with smooth animation.
- Custom-painted Refresh button with hover effect.
- Admin status indicator in the footer bar.
- System tray integration (double-click to restore, right-click for menu).

### Full English UI
- All labels, messages, and dialogs are now in English.
- Program title: **Windows Update Blocker**

---

## How to Build

1. Open `WindowsUpdateBlocker.sln` in Visual Studio 2010.
2. Select **Release | Win32**.
3. Build > Build Solution (F7).
4. Output: `Release\WindowsUpdateBlocker.exe`

> **Important:** Ensure `resource.rc` and `app.manifest` are in the same folder
> as `main.cpp` before building. The manifest must be compiled into the `.exe`
> for UAC auto-elevation to work.

---

## Services Managed

| Service       | Name                                      | Notes                        |
|---------------|-------------------------------------------|------------------------------|
| `wuauserv`    | Windows Update                            | Core — always managed        |
| `bits`        | Background Intelligent Transfer Service   | Managed                      |
| `UsoSvc`      | Update Orchestrator Service (Win10+)      | Managed if present           |
| `WaaSMedicSvc`| Windows Update Medic Service (Win10+)     | Best-effort (may be protected)|

Registry keys written when blocking:
- `HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU` → `NoAutoUpdate=1`, `AUOptions=1`
- `HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate` → `DisableWindowsUpdateAccess=1`

---

## License
Free to use and modify. Credit appreciated.

© 2026 Ari Sohandri Putra
