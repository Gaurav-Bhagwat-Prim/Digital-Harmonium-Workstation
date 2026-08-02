# Phase 2E — Developer Workbench Architecture & User Guide

**Version:** 2.0  
**Executable:** `build/bin/vhl_workbench.exe`  
**Purpose:** Workflow automation layer for the VHL toolchain.

---

## 1. Overview

Phase 2E introduces the **VHL Developer Workbench** (`vhl_workbench`), a lightweight workflow automation tool that eliminates repetitive manual command typing during day-to-day instrument creation and debugging.

### Architecture Freeze Compliance
The Workbench is strictly an outer automation layer. It does **not** modify or replace:
- `vhl_builder` (Phases 2A/2B/2C)
- `vhl_tools` (Phase 2D Inspector, Validator, Diff)
- `vhl_structs.h` (Frozen binary spec)

---

## 2. Directory Layout Convention

The Workbench assumes the standard VHL ecosystem directory layout:

```text
Project/
    Builder/
        build/
            bin/
                vhl_builder.exe
                vhl_tools.exe
                vhl_workbench.exe
    Instruments/
        Harmonium/
            000_first try.sfz
            000_first try.vhl
        Flute/
            Flute.sfz
            Flute.vhl
        samples/              ← Ignored by instrument discovery
```

Both `.sfz` source files and compiled `.vhl` binary files live together inside their respective instrument directory under `Instruments/`. No separate `Output/` folder exists.

---

## 3. Automated Features

### 3.1 Instrument Discovery
- Scans `Instruments/` subdirectories at launch and on refresh (`R`).
- Skips non-instrument folders like `samples/`, `Samples/`, `.git`, or hidden folders (`.`).
- Matches `.sfz` files and automatically computes the expected output `.vhl` path in the same directory.

### 3.2 Automated Build Workflow (Single Click)
When selecting **Option 1 (Build Instrument)** from the Instrument Menu:
1. Executes `vhl_builder "<sfzPath>"`
2. If compilation succeeds (`exit code 0`), automatically executes `vhl_tools validate "<vhlPath>" --strict`
3. If validation succeeds (`exit code 0`), automatically executes `vhl_tools inspect "<vhlPath>" --all`

This eliminates manual CLI typing and reduces the build-validate-inspect feedback loop to a single keypress.

### 3.3 Direct Tool Access
- **Inspect (Option 2)**: Executes `vhl_tools inspect "<vhlPath>" --all`
- **Validate (Option 3)**: Executes `vhl_tools validate "<vhlPath>" --strict`
- **Diff (Option 4)**: Interactively lists other compiled `.vhl` instruments in the workspace and launches `vhl_tools diff "<vhlA>" "<vhlB>" --detailed`
- **Open Instrument Folder (Option 5)**: Launches the OS native file explorer (`explorer.exe` on Windows) directly to the instrument's directory.
- **Build Builder (Option B from Main Menu)**: Re-compiles all C++ tool binaries using `cmake --build build --config Release`.

---

## 4. Usage Guide

### Starting the Workbench

```powershell
.\build\bin\vhl_workbench.exe
```

### Main Menu
```text
========================================
VHL Developer Workbench
========================================
Workspace : D:\...\Esp32 Digital Musical Workstation
----------------------------------------
Available Instruments:
  1. Harmonium [.vhl ready]
----------------------------------------
  B. Build Builder & Tools
  R. Refresh Instruments
  Q. Quit
----------------------------------------
Select option:
```

### Instrument Menu
```text
========================================
Instrument: Harmonium
========================================
  SFZ : D:\...\Instruments\Harmonium\000_first try.sfz
  VHL : D:\...\Instruments\Harmonium\000_first try.vhl
----------------------------------------
  1. Build Instrument (Build -> Validate -> Inspect)
  2. Inspect
  3. Validate
  4. Diff
  5. Open Instrument Folder
  6. Back to Main Menu
========================================
Select option: 1
```

---

## 5. Error Handling & Edge Cases

| Condition | Workbench Behavior |
|---|---|
| Missing `vhl_builder.exe` | Displays error message guiding developer to run Option `B` (Build Builder) |
| Missing `vhl_tools.exe` | Displays error message, skips auto-validation/inspect step gracefully |
| Instrument without `.vhl` | Disables Inspect/Validate options until `Build Instrument` is executed |
| Multiple `.sfz` in folder | Picks primary `.sfz` and issues warning |
| Build / Validation Failure | Halts pipeline early and displays failure code |
