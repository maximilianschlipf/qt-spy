#!/bin/bash

# qt-spy injection script
# Usage: ./inject_qt_spy.sh [<pid>]
# Interactive mode: ./inject_qt_spy.sh
# Direct mode: ./inject_qt_spy.sh 3845100

set -e

PID=""

# Function to select process interactively
select_process() {
    echo "Discovering Qt processes..."
    
    # Use the CLI tool to discover Qt processes
    local processes_output
    processes_output=$("$PROJECT_ROOT/build/cli/qt_spy_cli" --list 2>/dev/null || true)
    
    if [[ "$processes_output" == *"No Qt processes found"* ]]; then
        echo "No Qt processes found. Please start a Qt application first."
        exit 1
    fi
    
    # Extract process list (skip the header lines)
    local process_lines
    process_lines=$(echo "$processes_output" | grep -E '^\s*\[[0-9]+\]' || true)
    
    if [ -z "$process_lines" ]; then
        echo "No Qt processes available for injection."
        exit 1
    fi
    
    echo "$processes_output"
    echo ""
    echo -n "Select process [1-$(echo "$process_lines" | wc -l)] (or 0 to exit): "
    
    local choice
    read -r choice
    
    if [ "$choice" = "0" ]; then
        echo "Cancelled."
        exit 0
    fi
    
    # Validate choice
    local max_choice
    max_choice=$(echo "$process_lines" | wc -l)
    if ! [[ "$choice" =~ ^[0-9]+$ ]] || [ "$choice" -lt 1 ] || [ "$choice" -gt "$max_choice" ]; then
        echo "Invalid selection: $choice"
        exit 1
    fi
    
    # Extract PID from selected line
    local selected_line
    selected_line=$(echo "$process_lines" | sed -n "${choice}p")
    PID=$(echo "$selected_line" | grep -o 'PID: [0-9]*' | cut -d' ' -f2)
    
    if [ -z "$PID" ]; then
        echo "Failed to extract PID from selection."
        exit 1
    fi
    
    echo "Selected process with PID: $PID"
}

# Parse command line arguments
if [ $# -eq 0 ]; then
    # Interactive mode
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    select_process
elif [ $# -eq 1 ]; then
    # Direct mode with PID
    PID="$1"
else
    echo "Usage: $0 [<pid>]"
    echo "Interactive mode: $0"
    echo "Direct mode: $0 3845100"
    exit 1
fi
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BOOTSTRAP_LIB="$PROJECT_ROOT/build/bootstrap/libqt_spy_probe_bootstrap.so"

# Verify the process exists
if ! kill -0 "$PID" 2>/dev/null; then
    echo "Error: Process $PID does not exist or is not accessible"
    exit 1
fi

# Verify the bootstrap library exists
if [ ! -f "$BOOTSTRAP_LIB" ]; then
    echo "Error: Bootstrap library not found at $BOOTSTRAP_LIB"
    echo "Please build the project first: cd $PROJECT_ROOT && cmake --build build"
    exit 1
fi

echo "Injecting qt-spy into process $PID..."
echo "Bootstrap library: $BOOTSTRAP_LIB"

TARGET_ROOT="/proc/$PID/root"
TARGET_TMP_DIR="$TARGET_ROOT/tmp/qt_spy"
INJECTION_PATH="$BOOTSTRAP_LIB"
STAGED_HOST_PATH=""

if [ -d "$TARGET_ROOT" ]; then
    if mkdir -p "$TARGET_TMP_DIR" 2>/dev/null; then
        STAGED_BASENAME="libqt_spy_probe_bootstrap_${PID}_$(date +%s)_$$.so"
        STAGED_HOST_PATH="$TARGET_TMP_DIR/$STAGED_BASENAME"
        if cp "$BOOTSTRAP_LIB" "$STAGED_HOST_PATH" 2>/dev/null; then
            chmod 755 "$STAGED_HOST_PATH" 2>/dev/null || true
            INJECTION_PATH="/tmp/qt_spy/$STAGED_BASENAME"
            echo "Staged bootstrap inside target root at $INJECTION_PATH"
        else
            echo "Warning: failed to stage bootstrap inside target root, using host path." >&2
            STAGED_HOST_PATH=""
        fi
    else
        echo "Warning: unable to create staging directory inside target root, using host path." >&2
    fi
else
    echo "Warning: target root $TARGET_ROOT not accessible; using host path for injection." >&2
fi

# Create GDB script
GDB_SCRIPT=$(mktemp)

cleanup() {
    rm -f "$GDB_SCRIPT"
    if [ -n "$STAGED_HOST_PATH" ]; then
        rm -f "$STAGED_HOST_PATH" 2>/dev/null || true
        rmdir "$TARGET_TMP_DIR" 2>/dev/null || true
    fi
}
trap cleanup EXIT

cat > "$GDB_SCRIPT" << EOF
set confirm off
set pagination off
attach $PID
call (void*)dlopen("$INJECTION_PATH", 2)
detach
quit
EOF

# Run GDB injection
echo -n "Injecting... "
if gdb -batch -x "$GDB_SCRIPT" >/dev/null 2>&1; then
    echo "✓ Success!"
    echo ""
    
    # Give the probe a moment to initialize
    sleep 1
    
    echo "Probe is ready! You can now connect with:"
    echo "  cd $PROJECT_ROOT"
    echo "  ./build/cli/qt_spy_cli --pid $PID"
    echo ""
    echo "Or connect interactively:"
    echo "  ./build/cli/qt_spy_cli --interactive"
    echo ""
    echo "Tip: Use Ctrl+C in the CLI to disconnect gracefully without killing the target process"
else
    echo "✗ Failed!"
    echo ""
    echo "Possible causes:"
    echo "  - Process $PID does not exist or has exited"
    echo "  - Insufficient permissions (try with sudo)"
    echo "  - GDB is not available or not working properly"
    echo ""
    echo "You can try manually with:"
    echo "  cd $PROJECT_ROOT"
    echo "  ./build/cli/qt_spy_cli --pid $PID"
    exit 1
fi
