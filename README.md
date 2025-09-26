# qt-spy

qt-spy is an inspector prototype that attaches to a running Qt 5/6 MMI, enumerates the QObject/QWidget hierarchy, and returns a JSON snapshot of properties. Phase 1 extends the spike with a reusable bridge client, incremental update plumbing, and a CLI that can reconnect or inject the probe into plain Qt processes on demand.

## Building

```bash
cmake -S . -B build
cmake --build build
```

The build produces two executables in `build/`:

- `sample_mmi`: a demo Qt Widgets application that automatically embeds the qt-spy probe.
- `qt_spy_cli`: a console client that connects to the probe and prints a hierarchy dump.

In addition, `libqt_spy_bridge.a` exposes the reusable bridge client in `bridge/include/qt_spy/bridge_client.h` for the upcoming inspector UI.

## Running the Phase 0 Spike

1. Launch the sample MMI in one terminal:
   ```bash
   ./build/sample_mmi/sample_mmi &
   ```
   The window titled “Sample MMI” appears. The probe starts automatically and logs the server name to stdout (e.g. `qt_spy_sample_mmi_12345`).

2. Discover the PID (if needed) to derive the server name:
   ```bash
   pidof sample_mmi
   ```

3. From another terminal, run the CLI with either the PID or the explicit server name:
   ```bash
   ./build/cli/qt_spy_cli --pid <PID>
   # or
   ./build/cli/qt_spy_cli --server qt_spy_sample_mmi_<PID>
   ```

   When `--pid` is supplied, the CLI looks up `/proc/<PID>/comm` (on Unix-like systems) to reuse the process
   name and derive the matching server identifier. The CLI then prints a formatted JSON document containing:
   - Application metadata (`applicationName`, `applicationPid`, `serverName`).
   - All top-level widgets/windows.
   - Each QObject/QWidget child with properties, geometry info, and dynamic properties if present.

### Example Output (excerpt)

```json
{
    "applicationName": "sample_mmi",
    "applicationPid": 12345,
    "roots": [
        {
            "className": "QWidget",
            "objectName": "mmiMainWindow",
            "widget": {
                "geometry": {
                    "height": 360,
                    "width": 420,
                    "x": 100,
                    "y": 100
                },
                "visible": true,
                "windowTitle": "Sample MMI"
            },
            "children": [ ... ]
        }
    ]
}
```

## Attaching to Existing Applications

For applications that weren't started with the probe embedded, qt-spy provides several approaches:

### Interactive Mode (Recommended)

qt-spy includes interactive scripts for easy process selection and connection:

#### 1. Interactive Injection

```bash
./scripts/inject_qt_spy.sh
```

This script will:

- Automatically discover running Qt processes
- Present an interactive menu for process selection
- Use GDB to inject the probe into the selected process
- Handle the injection process automatically

#### 2. Interactive Connection

```bash
./scripts/connect_qt_spy.sh
```

This script will:

- Discover processes with qt-spy probes already injected
- Present an interactive menu for connection
- Establish a live connection showing real-time object tree updates
- Handle graceful disconnection when cancelled (Ctrl+C)

**Example workflow:**

```bash
# Step 1: Inject into a running Qt process
./scripts/inject_qt_spy.sh
# Select process from menu (e.g., "2" for rmmi)

# Step 2: Connect and monitor in real-time
./scripts/connect_qt_spy.sh  
# Select same process, see live object tree updates
# Press Ctrl+C to disconnect gracefully
```

### Method 1: Automatic Injection (CLI Direct)

The CLI tool can automatically inject the probe into running Qt processes using ptrace:

```bash
./build/cli/qt_spy_cli --pid <PID>
```

This works for most standard Qt applications running with system libraries.

### Method 2: LD_PRELOAD (Recommended for Custom Environments)

For applications using custom runtime environments or non-standard library paths (common in cross-compiled or containerized environments), use LD_PRELOAD to inject the probe at startup:

1. **Stop the target application** if it's currently running.

2. **Restart the application** with the probe library preloaded:

   ```bash
   LD_PRELOAD=/path/to/qt-spy/build/bootstrap/libqt_spy_probe_bootstrap.so your_application [args]
   ```

3. **Connect with the CLI** using the new PID:

   ```bash
   ./build/cli/qt_spy_cli --pid <NEW_PID>
   ```

#### Example: Attaching to a Custom MMI

```bash
# Stop the existing application
kill <old_pid>

# Restart with probe preloaded
LD_PRELOAD=/home/user/qt-spy/build/bootstrap/libqt_spy_probe_bootstrap.so \
  python /path/to/start_script.py rmmi -s 0.5

# Find the new PID
pidof rmmi

# Connect to the application
./build/cli/qt_spy_cli --pid <new_pid>
```

#### When to Use Each Method

- **Use automatic injection** for standard Qt applications with system libraries
- **Use LD_PRELOAD** when:
  - Automatic injection fails with library resolution errors
  - The target uses custom libc or runtime environments
  - The application runs in containers or cross-compiled environments
  - You have control over application startup

## Limitations and Notes

- The probe currently walks `QWidget` hierarchies; QML/Qt Quick items are not covered yet.
- Only properties readable via `QMetaProperty::read` and dynamic properties are emitted; complex types fall back to string serialization.
- The server name schema is `qt_spy_<applicationName>_<pid>`; the CLI derives it automatically when given a PID.
- Automatic probe injection currently relies on ptrace/`dlopen` and is supported on Unix-like systems. Use `--no-inject` to skip it when elevated debugging is unavailable.
- `--pid` lookups currently rely on `/proc/<PID>/comm`, so they are limited to Unix-like systems; use `--server` on other platforms.
- LD_PRELOAD method requires restarting the target application but is more reliable across different runtime environments.
