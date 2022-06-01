#!/usr/bin/env python3

import os
import sys
import argparse
import pathlib
import shutil
import struct

RCP_FILETAG_VERSION = 0
RCP_FILETAG_FLASH_ARGS = 1
RCP_FILETAG_BOOTLOADER = 2
RCP_FILETAG_PARTITION_TABLE = 3
RCP_FILETAG_FIRMWARE = 4
RCP_FILETAG_IMAGE_HEADER = 0xff

RCP_IMAGE_HEADER_SIZE = 3 * 4 * 6
RCP_FLASH_ARGS_SIZE = 2 * 4 * 3


def append_subfile_header(fout, tag, size, offset):
    fout.write(struct.pack('<LLL', tag, size, offset))
    return offset + size


def append_subfile(fout, target_file):
    buf_size = 4096
    with open(target_file, 'rb') as fin:
        data = fin.read(buf_size)
        while data and len(data) > 0:
            fout.write(data)
            data = fin.read(buf_size)


def append_flash_args(fout, flash_args_path):
    with open(flash_args_path, 'r') as f:
        # skip first line
        next(f)
        partition_info_list = [l.split() for l in f]
    for offset, partition_file in partition_info_list:
        offset = int(offset, 0)
        if partition_file.find('bootloader') >= 0:
            fout.write(struct.pack('<LL', RCP_FILETAG_BOOTLOADER, offset))
        elif partition_file.find('partition_table') >= 0:
            fout.write(struct.pack('<LL', RCP_FILETAG_PARTITION_TABLE, offset))
        else:
            fout.write(struct.pack('<LL', RCP_FILETAG_FIRMWARE, offset))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-b', '--base-dir', type=str)
    parser.add_argument('-t', '--target_dir', type=str)
    args = parser.parse_args()
    base_dir = args.base_dir
    target_dir = args.target_dir
    pathlib.Path(target_dir).mkdir(parents=True, exist_ok=True)
    rcp_version_path = os.path.join(base_dir, 'rcp_version')
    flash_args_path = os.path.join(base_dir, 'flash_args')
    bootloader_path = os.path.join(base_dir, 'bootloader', 'bootloader.bin')
    partition_table_path = os.path.join(
        base_dir, 'partition_table', 'partition-table.bin')
    rcp_firmware_path = os.path.join(base_dir, 'esp_ot_rcp.bin')
    with open(os.path.join(target_dir, 'rcp_image'), 'wb') as fout:
        offset = append_subfile_header(
            fout, RCP_FILETAG_IMAGE_HEADER, RCP_IMAGE_HEADER_SIZE, 0)
        offset = append_subfile_header(
            fout, RCP_FILETAG_VERSION, os.path.getsize(rcp_version_path), offset)
        offset = append_subfile_header(
            fout, RCP_FILETAG_FLASH_ARGS, RCP_FLASH_ARGS_SIZE, offset)
        offset = append_subfile_header(
            fout, RCP_FILETAG_BOOTLOADER, os.path.getsize(bootloader_path), offset)
        offset = append_subfile_header(
            fout, RCP_FILETAG_PARTITION_TABLE, os.path.getsize(partition_table_path), offset)
        offset = append_subfile_header(
            fout, RCP_FILETAG_FIRMWARE, os.path.getsize(rcp_firmware_path), offset)
        append_subfile(fout, rcp_version_path)
        append_flash_args(fout, flash_args_path)
        append_subfile(fout, bootloader_path)
        append_subfile(fout, partition_table_path)
        append_subfile(fout, rcp_firmware_path)


if __name__ == '__main__':
    main()
