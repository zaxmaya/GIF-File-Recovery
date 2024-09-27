## Overview
gif_finder is a file recovery program designed to recover deleted .gif files from a USB or drive formatted with the ext3 file system. The program searches the drive for known GIF signatures and attempts to recover the GIF files by reconstructing them using their inode and block data.

**Key Features**:
 - Scans the drive for blocks containing GIF file signatures (GIF87a or GIF89a).
 - Retrieves inode information to recover the GIF file data.
 - Uses Linux utilities such as debugfs and dd to perform the recovery.
 - Recovers the file as a .gif format with a slightly larger size than the original (due to fragmentation or extra blocks being recovered).

## Requirments
 - A Linux system with access to debugfs and dd.
 - The drive/USB must be formatted in the ext3 filesystem.
 - Sudo/root access to perform recovery operations on the device.

## How to Run
 - Compile: `gcc -o gif_finder gif_finder.c`
 - Run: `sudo ./gif_finder /dev/sdX`
 - Run the program with root privileges to scan and recover GIF files.

## Usage
- Connect your drive or USB that is formatted in ext3 and ensure you know its device name (e.g., /dev/sdb).
- Run the program with root privileges to scan and recover GIF files:
```
  sudo ./gif_finder /dev/sdX
```
    Replace /dev/sdX with the correct device name.
 - The recovered files will be saved in the current working directory as `recovery_X.gif`.

## Future Improvements
 - File Size Optimization: Fixing the issue where recovered files are larger than their original size by improving the block and inode handling logic.
 - Fragmentation Handling: Better handling of fragmented files to ensure more accurate reconstruction of the original GIF.
 - Filesystem Compatibility: Extend compatibility to other file systems like `ext4`.
