#!/usr/bin/env bash
set -e

# Optional: only needed if SDCC is not installed system-wide (adjust the path)
# export PATH="$HOME/tools/sdcc/bin:$PATH"

# Move to the folder where the script is located
cd "$(dirname "$(readlink -f "$0")")"

# Create the folders if they don't exist
mkdir -p bin build

# Compile: all generated files go into build/
sdcc -mmcs51 --model-small --iram-size 128 --xram-loc 0x7E00 --xram-size 0x200 --code-size 0x8000 -o build/ ./src/sb16_3.00.c

# Generate HEX and BIN files
packihx build/sb16_3.00.ihx > build/sb16_3.00.hex
makebin -p build/sb16_3.00.ihx build/sb16_3.00.bin

# Move the BIN file to bin/ (overwrites without asking)
mv -f build/sb16_3.00.bin bin/sb16_3.00.bin