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

echo "Setup complete .. Project ready to build!"
