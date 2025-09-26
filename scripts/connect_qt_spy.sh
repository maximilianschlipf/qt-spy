#!/bin/bash

# qt-spy connection script
# Usage: ./connect_qt_spy.sh [<pid>]
# Interactive mode: ./connect_qt_spy.sh
# Direct mode: ./connect_qt_spy.sh 3845100

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
PID=""

# Function to select process interactively
select_process() {
    echo "Available Qt processes:"
    
    # Use the CLI tool to discover Qt processes
    local processes_output
    processes_output=$("$PROJECT_ROOT/build/cli/qt_spy_cli" --list 2>/dev/null || true)
    
    if [[ "$processes_output" == *"No Qt processes found"* ]]; then
        echo "No Qt processes found."
        echo ""
        echo "To inject qt-spy into a running process, use:"
        echo "  ./inject_qt_spy.sh"
        exit 1
    fi
    
    # Extract process list (skip the header lines)
    local process_lines
    process_lines=$(echo "$processes_output" | grep -E '^\s*\[[0-9]+\]' || true)
    
    if [ -z "$process_lines" ]; then
        echo "No Qt processes available for connection."
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
    
    echo "Connecting to process with PID: $PID"
    echo ""
}

# Parse command line arguments
if [ $# -eq 0 ]; then
    # Interactive mode
    select_process
elif [ $# -eq 1 ]; then
    # Direct mode with PID
    PID="$1"
    echo "Connecting to process with PID: $PID"
else
    echo "Usage: $0 [<pid>]"
    echo "Interactive mode: $0"
    echo "Direct mode: $0 3845100"
    exit 1
fi

# Setup signal handling for graceful disconnect
cleanup() {
    echo ""
    echo "Disconnecting gracefully..."
    # Kill any background qt_spy_cli process if it exists
    if [ -n "$CLI_PID" ]; then
        # Send SIGINT to allow graceful detach
        kill -INT "$CLI_PID" 2>/dev/null || true
        # Wait a moment for graceful shutdown
        sleep 1
        # Force kill if still running
        kill -9 "$CLI_PID" 2>/dev/null || true
    fi
    exit 0
}

trap cleanup SIGINT SIGTERM EXIT

echo "Press Ctrl+C to disconnect gracefully"
echo "=========================================="
echo ""

# Connect to the process
cd "$PROJECT_ROOT"

# Start qt_spy_cli in background so we can handle signals properly
./build/cli/qt_spy_cli --pid "$PID" &
CLI_PID=$!

# Wait for the background process
if wait $CLI_PID; then
    echo ""
    echo "Connection completed successfully."
else
    exit_code=$?
    echo ""
    if [ $exit_code -eq 130 ]; then
        echo "Disconnected by user (Ctrl+C)."
    else
        echo "Connection failed or terminated unexpectedly (exit code: $exit_code)."
        echo ""
        echo "Possible reasons:"
        echo "  - Process $PID doesn't exist or has no qt-spy probe"
        echo "  - Process is not a Qt application"
        echo ""
        echo "To inject qt-spy first, run:"
        echo "  ./inject_qt_spy.sh"
        exit 1
    fi
fi
