#!/usr/bin/env python3
"""
Repack an existing xiaozhi mmap_assets binary with additional files.
Reads the original binary, extracts all assets, adds new files,
and writes a new combined binary in the same format.

Format:
  Header: file_count(4) + checksum(4) + data_length(4) = 12 bytes
  Metadata table: N * (name[32] + size(4) + offset(4) + width(2) + height(2)) = N * 44 bytes
  Data: each file prefixed with 0x5A5A (2 bytes magic)
"""

import struct
import os
import sys
import glob


def compute_checksum(data):
    """Compute simple sum checksum matching xiaozhi's format (16-bit mask)."""
    return sum(data) & 0xFFFF


def parse_assets_bin(filepath):
    """Parse an existing mmap_assets binary and return list of (name, data) tuples."""
    with open(filepath, 'rb') as f:
        raw = f.read()

    if len(raw) < 12:
        print(f"Error: file too small ({len(raw)} bytes)")
        return []

    file_count = struct.unpack_from('<I', raw, 0)[0]
    checksum = struct.unpack_from('<I', raw, 4)[0]
    data_length = struct.unpack_from('<I', raw, 8)[0]

    print(f"Original binary: {len(raw)} bytes, {file_count} files, checksum=0x{checksum:08X}")

    header_size = 12
    entry_size = 44  # 32 + 4 + 4 + 2 + 2
    table_start = header_size
    data_start = header_size + file_count * entry_size

    assets = []
    for i in range(file_count):
        offset = table_start + i * entry_size
        name_bytes = raw[offset:offset+32]
        name = name_bytes.split(b'\x00')[0].decode('utf-8', errors='replace')
        size = struct.unpack_from('<I', raw, offset + 32)[0]
        data_offset = struct.unpack_from('<I', raw, offset + 36)[0]
        width = struct.unpack_from('<H', raw, offset + 40)[0]
        height = struct.unpack_from('<H', raw, offset + 42)[0]

        # Data starts at data_start + data_offset, skip 0x5A5A prefix
        abs_offset = data_start + data_offset + 2  # +2 for 0x5A5A magic
        file_data = raw[abs_offset:abs_offset + size]

        assets.append({
            'name': name,
            'data': file_data,
            'width': width,
            'height': height,
        })
        print(f"  [{i}] {name}: {size} bytes (w={width}, h={height})")

    return assets


def pack_assets_bin(assets, output_path, max_name_len=32):
    """Pack a list of asset dicts into mmap_assets binary format."""
    merged_data = bytearray()
    file_info_list = []

    for asset in assets:
        name = asset['name']
        data = asset['data']
        width = asset.get('width', 0)
        height = asset.get('height', 0)

        file_info_list.append((name, len(merged_data), len(data), width, height))
        # Add 0x5A5A prefix
        merged_data.extend(b'\x5A\x5A')
        merged_data.extend(data)

    total_files = len(file_info_list)

    # Build metadata table
    mmap_table = bytearray()
    for name, offset, size, width, height in file_info_list:
        if len(name) > max_name_len:
            print(f'Warning: "{name}" exceeds {max_name_len} bytes, truncating.')
        fixed_name = name.ljust(max_name_len, '\0')[:max_name_len]
        mmap_table.extend(fixed_name.encode('utf-8'))
        mmap_table.extend(struct.pack('<I', size))
        mmap_table.extend(struct.pack('<I', offset))
        mmap_table.extend(struct.pack('<H', width))
        mmap_table.extend(struct.pack('<H', height))

    combined_data = mmap_table + merged_data
    combined_checksum = compute_checksum(combined_data)
    combined_data_length = len(combined_data)

    header = struct.pack('<III', total_files, combined_checksum, combined_data_length)
    final_data = header + combined_data

    with open(output_path, 'wb') as f:
        f.write(final_data)

    print(f"\nPacked {total_files} files into {output_path} ({len(final_data)} bytes)")
    return output_path


def main():
    # Paths
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)

    original_bin = os.path.expanduser("~/Downloads/assets (4).bin")
    sounds_dir = os.path.join(project_dir, 'spiffs_assets', 'sounds')
    output_bin = os.path.join(project_dir, 'build', 'generated_assets.bin')

    if not os.path.exists(original_bin):
        print(f"Error: original assets not found: {original_bin}")
        sys.exit(1)

    # Parse original assets
    print("=== Parsing original assets ===")
    assets = parse_assets_bin(original_bin)

    # Add PCM sound files
    print(f"\n=== Adding PCM sounds from {sounds_dir} ===")
    pcm_files = sorted(glob.glob(os.path.join(sounds_dir, '*.pcm')))
    if not pcm_files:
        print("Warning: no PCM files found!")
    else:
        for pcm_path in pcm_files:
            filename = os.path.basename(pcm_path)
            asset_name = f"sounds/{filename}"
            with open(pcm_path, 'rb') as f:
                data = f.read()
            assets.append({
                'name': asset_name,
                'data': data,
                'width': 0,
                'height': 0,
            })
            print(f"  + {asset_name}: {len(data)} bytes")

    # Pack combined binary
    print(f"\n=== Packing combined assets ===")
    os.makedirs(os.path.dirname(output_bin), exist_ok=True)
    pack_assets_bin(assets, output_bin)


if __name__ == '__main__':
    main()
