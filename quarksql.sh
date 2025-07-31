#!/bin/bash
set -e

# Update package lists and install essential build tools and libraries
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake wget curl libsnappy-dev zlib1g-dev \
    libbz2-dev liblz4-dev libzstd-dev libssl-dev librocksdb-dev

# Install Boost 1.69 (required for Crow)
BOOST_VERSION="1.69.0"
BOOST_DIR="/tmp/boost_${BOOST_VERSION//./_}"
BOOST_ARCHIVE="boost_${BOOST_VERSION//./_}.tar.gz"
if [ ! -d "$BOOST_DIR" ]; then
    cd /tmp
    # Use SourceForge mirror to ensure proper download
    wget -q -O "$BOOST_ARCHIVE" "https://sourceforge.net/projects/boost/files/boost/$BOOST_VERSION/$BOOST_ARCHIVE/download"
    tar -xzf "$BOOST_ARCHIVE"
    cd "$BOOST_DIR"
    ./bootstrap.sh --prefix=/usr/local/boost_${BOOST_VERSION//./_}
    ./b2 install
fi

# Set up environment variables for Boost (only for this script run)
export BOOST_ROOT="/usr/local/boost_${BOOST_VERSION//./_}"
export LD_LIBRARY_PATH="$BOOST_ROOT/lib:$LD_LIBRARY_PATH"
export CPATH="$BOOST_ROOT/include:$CPATH"
export LIBRARY_PATH="$BOOST_ROOT/lib:$LIBRARY_PATH"

# Return to project root (script directory)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Create build directory
mkdir -p build
cd build

# Compile the project
g++ -std=c++17 \
    -I "$SCRIPT_DIR/include" \
    -I "$SCRIPT_DIR/third-party/crow/include" \
    -I "$BOOST_ROOT/include" \
    -L "$BOOST_ROOT/lib" \
    -L /usr/lib \
    -pthread "$SCRIPT_DIR/src/"*.cpp -o quarksql \
    -lrocksdb -lboost_system -lssl -lcrypto

echo "Build complete! Executable is located at build/quarksql"
