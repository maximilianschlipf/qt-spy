# qt-spy

qt-spy is an inspector tool that attaches to running Qt 5/6 applications, enumerates the QObject/QWidget hierarchy, and provides real-time monitoring of object properties. It includes both a command-line interface and a graphical inspector application.

## Purpose & Architecture

qt-spy consists of several components:

- **Probe**: A library (`libqt_spy_probe_bootstrap.so`) injected into target Qt applications that walks the object hierarchy and exposes data via QLocalSocket
- **Bridge Client**: Reusable library (`libqt_spy_bridge.a`) that connects to probe sockets and handles protocol communication
- **CLI Tool**: Command-line interface (`qt_spy_cli`) for scripting and automated inspection
- **GUI Inspector**: Qt-based graphical application (`qt_spy_inspector`) with tree view and property inspector
- **Sample Applications**: Test applications with and without embedded probes

## Technology Stack

- **Qt**: 5.15+ (Qt6 compatible)
- **Build System**: CMake 3.16+
- **Language**: C++17
- **Platform**: Linux (x86_64), with Unix-specific injection features
- **Injection Method**: GDB-based injection via shell script (reliable across environments)
- **Protocol**: JSON over QLocalSocket

## Building

```bash
# Clean configuration and build
cmake -S . -B build
cmake --build build --parallel
```

The build produces several executables and libraries in `build/`:

**Main Applications:**

- `qt_spy_cli`: Command-line interface for scripting and automated inspection
- `qt_spy_inspector`: GUI application with interactive tree view and property inspector

**Sample Applications:**

- `sample_mmi`: Demo Qt Widgets application with automatically embedded qt-spy probe
- `sample_plain_mmi`: Plain Qt Widgets application without probe (for testing injection)

**Libraries:**

- `libqt_spy_bridge.a`: Reusable client library for connecting to probes
- `libqt_spy_probe_bootstrap.so`: Injectable probe library

## Quick Start

### Testing with Sample Applications

qt-spy includes two sample applications for testing:

- **`sample_mmi`**: Has the qt-spy probe embedded automatically (for basic testing)
- **`sample_plain_mmi`**: Plain Qt application without probe (for testing injection)

### Running with Embedded Probe

1. Launch the sample MMI in one terminal:

   ```bash
   ./build/sample_mmi/sample_mmi &
   ```

   The window titled "Sample MMI" appears. The probe starts automatically and logs the server name to stdout (e.g. `qt_spy_sample_mmi_12345`).

2. Discover the PID (if needed) to derive the server name:

   ```bash
   pidof sample_mmi
   ```

3. From another terminal, run the CLI with either the PID or the explicit server name:

   ```bash
   ./build/cli/qt_spy_cli --pid <PID>
   # or
   ./build/cli/qt_spy_cli --server qt_spy_sample_mmi_<PID>
   ```ations, enumerates the QObject/QWidget hierarchy, and provides real-time monitoring of object properties. It includes both a command-line interface and a graphical inspector application.

## Purpose & Architecture

qt-spy consists of several components:

- **Probe**: A library (`libqt_spy_probe_bootstrap.so`) injected into target Qt applications that walks the object hierarchy and exposes data via QLocalSocket
- **Bridge Client**: Reusable library (`libqt_spy_bridge.a`) that connects to probe sockets and handles protocol communication
- **CLI Tool**: Command-line interface (`qt_spy_cli`) for scripting and automated inspection
- **GUI Inspector**: Qt-based graphical application (`qt_spy_inspector`) with tree view and property inspector
- **Sample Applications**: Test applications with and without embedded probes

## Technology Stack

- **Qt**: 5.15+ (Qt6 compatible)
- **Build System**: CMake 3.16+
- **Language**: C++17
- **Platform**: Linux (x86_64), with Unix-specific injection features
- **Injection Method**: GDB-based injection via shell script (reliable across environments)
- **Protocol**: JSON over QLocalSocket

## Building

```bash
# Clean configuration and build
cmake -S . -B build
cmake --build build --parallel
```

The build produces several executables and libraries in `build/`:

**Main Applications:**

- `qt_spy_cli`: Command-line interface for scripting and automated inspection
- `qt_spy_inspector`: GUI application with interactive tree view and property inspector

**Sample Applications:**

- `sample_mmi`: Demo Qt Widgets application with automatically embedded qt-spy probe
- `sample_plain_mmi`: Plain Qt Widgets application without probe (for testing injection)

**Libraries:**

- `libqt_spy_bridge.a`: Reusable client library for connecting to probes
- `libqt_spy_probe_bootstrap.so`: Injectable probe library

## Quick Start

### Testing with Sample Applications

qt-spy includes two sample applications for testing:

- **`sample_mmi`**: Has the qt-spy probe embedded automatically (for basic testing)
- **`sample_plain_mmi`**: Plain Qt application without probe (for testing injection)

### Running with Embedded Probe

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

### Testing Injection with Plain Sample

To test the injection functionality, use the plain sample application:

```bash
# Terminal 1: Start plain Qt app (no embedded probe)
./build/sample_plain_mmi/sample_plain_mmi &

# Terminal 2: Inject and connect using interactive mode
./scripts/inject_qt_spy.sh    # Select sample_plain_mmi from menu
./scripts/connect_qt_spy.sh   # Select same process for monitoring
```

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

### Method 1: CLI Direct Mode

The CLI tool provides multiple ways to attach to Qt processes:

#### Basic Usage

```bash
# Attach by PID (with automatic injection)
./build/cli/qt_spy_cli --pid <PID>

# Connect to existing probe by server name
./build/cli/qt_spy_cli --server qt_spy_<app_name>_<PID>
```

#### Advanced Options

```bash
# Interactive process selection menu
./build/cli/qt_spy_cli --interactive

# List all available Qt processes
./build/cli/qt_spy_cli --list

# Auto-attach to most recent Qt process
./build/cli/qt_spy_cli --auto

# Attach by process name
./build/cli/qt_spy_cli --name rmmi

# Attach by window title
./build/cli/qt_spy_cli --title "My Application"

# One-shot snapshot (exit after first output)
./build/cli/qt_spy_cli --pid <PID> --snapshot-once

# Disable automatic probe injection
./build/cli/qt_spy_cli --pid <PID> --no-inject
```

#### Connection Management

```bash
# Set reconnection attempts (-1 for infinite)
./build/cli/qt_spy_cli --pid <PID> --retries 5

# Send specific requests
./build/cli/qt_spy_cli --pid <PID> --select first-root
./build/cli/qt_spy_cli --pid <PID> --properties <node_id>
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

## GUI Inspector

qt-spy includes a fully-functional graphical inspector application for interactive object hierarchy exploration:

```bash
# Launch the GUI inspector
./build/inspector/qt_spy_inspector
```

### Features

- **✅ Process Discovery**: Automatic detection of running Qt applications with probe status indication
- **✅ One-Click Injection**: Automatic probe injection into processes that don't have probes yet
- **✅ Real-time Connection**: Live monitoring with automatic reconnection handling
- **✅ Interactive Tree View**: Expandable/collapsible object hierarchy browser
- **✅ Property Inspector**: Detailed property viewer for selected objects with type information
- **✅ Connection Management**: Robust error handling and connection state management

### Usage

1. **Launch the inspector**: `./build/inspector/qt_spy_inspector`
2. **Connect to process**: Use "File → Connect to Process" menu (Ctrl+O)
3. **Select target**: Choose from auto-discovered Qt applications (probe status shown)
4. **Automatic setup**: Injection happens automatically for processes without probes
5. **Explore hierarchy**: Click to expand/collapse object tree nodes
6. **View properties**: Select any object to see its detailed properties in the right panel

**Verified working with**: MMI applications, sample applications, and standard Qt widgets

## Documentation

- `README.md` - This file, complete project overview
- `HANDOFF.md` - Technical handoff documentation for developers  
- `IMPLEMENTATION_PLAN.md` - Original planning document (archived, development complete)

## Recent Decisions & Changes

### Latest Updates (September 2025)

- **✅ COMPLETE**: Unified injection approach - Both CLI and GUI now use the proven `inject_qt_spy.sh` shell script
- **✅ COMPLETE**: Removed legacy ptrace injection code - Eliminated complex, platform-specific injection implementations
- **✅ COMPLETE**: Code cleanup - Removed redundant debug logging, unused includes, and obsolete injection methods
- **✅ COMPLETE**: Streamlined codebase - Single, reliable injection method shared across all components
- **✅ COMPLETE**: Enhanced error handling - Clean error messages and robust connection management
- **✅ VERIFIED**: Full workflow testing - Both CLI and GUI inspector successfully connect to MMI applications

### Technical Decisions

- **Injection Strategy**: GDB-based shell script (`inject_qt_spy.sh`) - proven reliable across environments
- **Connection Protocol**: JSON over QLocalSocket for simplicity and cross-platform compatibility  
- **GUI Framework**: Qt Widgets for native look and feel
- **Build System**: CMake with clean dependency management
- **Testing**: Comprehensive test suite covering injection, connection, and reconnection scenarios

## Limitations and Notes

- The probe currently walks `QWidget` hierarchies; QML/Qt Quick items are not covered yet.
- Only properties readable via `QMetaProperty::read` and dynamic properties are emitted; complex types fall back to string serialization.
- The server name schema is `qt_spy_<applicationName>_<pid>`; the CLI derives it automatically when given a PID.
- Probe injection currently relies on GDB and is supported on Unix-like systems. Use `--no-inject` to skip it when debugging tools are unavailable.
- `--pid` lookups currently rely on `/proc/<PID>/comm`, so they are limited to Unix-like systems; use `--server` on other platforms.
- LD_PRELOAD method requires restarting the target application but is more reliable across different runtime environments.
