#!/usr/bin/env python3

import os
import sys
import argparse
import pathlib
import shutil

def remap_flash_args(flash_args_path, target_path):
    with open(flash_args_path, 'r') as f:
        # skip first line
        next(f)
        partition_info_list = [l.split() for l in f]
    with open(target_path, 'w') as f:
        for offset, partition_file in partition_info_list:
            if partition_file.find('bootloader') >= 0:
                f.write('{} bt/bt.bin\n'.format(offset))
            elif partition_file.find('partition_table') >= 0:
                f.write('{} pt/pt.bin\n'.format(offset))
            else:
                f.write('{} {}\n'.format(offset, partition_file))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-b', '--base-dir', type=str)
    parser.add_argument('-t', '--target_dir', type=str)
    args = parser.parse_args()
    base_dir = args.base_dir
    target_dir = args.target_dir
    target_partition_dir = os.path.join(target_dir, 'pt')
    target_bootloader_dir = os.path.join(target_dir, 'bt')
    pathlib.Path(target_partition_dir).mkdir(parents=True, exist_ok=True)
    pathlib.Path(target_bootloader_dir).mkdir(parents=True, exist_ok=True)
    shutil.copy2(os.path.join(base_dir, 'rcp_version'), target_dir)
    shutil.copy2(os.path.join(base_dir, 'esp_ot_rcp.bin'), target_dir)
    shutil.copy2(os.path.join(base_dir, 'partition_table', 'partition-table.bin'),
                 os.path.join(target_partition_dir, 'pt.bin'))
    shutil.copy2(os.path.join(base_dir, 'bootloader', 'bootloader.bin'),
                 os.path.join(target_bootloader_dir, 'bt.bin'))
    remap_flash_args(os.path.join(base_dir, 'flash_args'),
                     os.path.join(target_dir, 'flash_args'))


if __name__ == '__main__':
    main()
