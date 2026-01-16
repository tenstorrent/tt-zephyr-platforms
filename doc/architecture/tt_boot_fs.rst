.. _tt_boot_fs:

Tenstorrent Boot Filesystem
*****************************

The Tenstorrent Boot Filesystem (TT Boot FS) is a simple file system designed to
store and manage firmware images and related data in the bootloader and firmware
of Tenstorrent platforms. It provides a structured way to organize and access
binary files necessary for booting and operating the device.

FileSystem Structure
--------------------

The fundamental structure of the TT Boot FS consists of file descriptors, which
are defined by the :c:struct:`tt_boot_fs_fd` structure. Each file descriptor
contains metadata about a binary file stored in the boot filesystem, such as its
location in SPI flash memory, size, type, and various flags indicating its
properties.

The TT Boot FS supports multiple tables of file descriptors, allowing for
flexibility in managing different sets of binaries. Tables are defined as an
array of file descriptors, with the final entry having the ``invalid`` flag set.
The file descriptor tables may be located anywhere in flash memory. A fixed
header at :c:macro:`TT_BOOT_FS_HEADER_ADDR` describes the location and number of
these tables.  The header is defined by the :c:struct:`tt_boot_fs_header`
structure. Addresses of each file descriptor table are stored as an array of
32-bit integers immediately following the header.

File Types and Flags
====================

Each file descriptor includes a ``flags`` field, which is a bitmask that defines
various properties of the file. The field is represented by the
:c:struct:`tt_boot_fs_flags` structure. Key flags include:

* ``executable``: Indicates whether the file is executable. If set, the bootloader
  will attempt to execute the binary after loading it.
* ``invalid``: Marks the file as invalid. If set, the bootloader will assume this
  descriptor marks the end of the table.
* ``image_size``: Specifies the size of the binary image in bytes.

File Checksums
==============

Each file descriptor includes a checksum field, ``data_crc``, which is
calculated over the binary data. If a checksum mismatch is detected during
loading, the bootloader will treat the file as invalid.

Boot Process
------------

During the boot process, the bootloader reads the TT Boot FS descriptor list
at ``0x0``. If no descriptors in this list are marked as executable, the bootloader
will proceed to read the failover descritor list at ``0x4000``.

The bootloader will load each binary it encounters in the descriptor list into
RAM at the specified ``copy_dest`` address. If a binary is marked as executable,
the bootloader will transfer control to that binary after loading it. If
control returns, the bootloader will continue to load binaries from the list.
