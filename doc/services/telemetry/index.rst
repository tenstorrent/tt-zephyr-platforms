Telemetry
=========

In Chip Management Firmware, telemetry gathering and reporting is done in the file `telemetry.c </tt-system-firmware/doxygen/telemetry_8c.html>`_. This telemetry is located within the ``tensix_sm`` (ARC) memory space (i.e., within ``NOC0 8-0``):

See `telemetry table </tt-system-firmware/doxygen/group__telemetry__table.html>`_ for information.

Scratch Registers
-----------------

As per:

``Scratch Registers | arc_ss.reset_unit.SCRATCH_RAM[0..63]``:

- Address of ``telemetry_data`` is stored in ``reset_unit.SCRATCH_RAM[12]`` (WH: ARC_RESET.NOC_NODEID_Y_0)
- Address of ``telemetry_table`` is stored in ``reset_unit.SCRATCH_RAM[13]`` (WH: ARC_RESET.NOC_NODEID_X_0)



The telemetry table is updated every 100ms by a Zephyr worker thread.

Procedure to Read Telemetry
---------------------------

Via MMIO Access to ARC Memory
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

(Generally, when you are connected to BH over PCIe)

1. Find ``telemetry_table`` by reading ```reset_unit.SCRATCH_RAM[13]``.
2. Check ``telemetry_table.entry_count`` to find out how many ``tag_table`` entries there are.
3. Read through all ``tag_table`` entries and keep the tag-offset mapping around. These will not change unless the firmware gets updated and the chip is reset.
4. To find specific telemetry entries:
   - Look up the offset in the tag-offset mapping.
   - Read 4 bytes starting from ``SCRATCH_RAM[12] + 4 * offset``.

Via SMBUS
~~~~~~~~~

(i.e., from DMC or Galaxy UBB CPLD)

1. Write the tag ID you wish to read to SMBUS address ``0x26``. This register is 8 bits wide—the SMBUS write should include a PEC.
2. The tag ID is persistent, so it can be programmed once, and the telemetry data can be read multiple times from address ``0x27``.
3. Read the telemetry tag data via SMBUS by issuing a block read to address ``0x27``. This register is 32 bits wide.

.. list-table::
   :header-rows: 1
   :widths: 20 20 60

   * - Register
     - Address
     - Usage
   * - TELEMETRY_TAG
     - 0x26
     - Write only. Write with telemetry tag value to select which telemetry field will be read when reading from TELEMETRY_DATA register.
   * - TELEMETRY_DATA
     - 0x27
     - Read only. Read to get the latest telemetry data for the tag programmed to the TELEMETRY_TAG register.

Via west attach
~~~~~~~~~~~~~~~

You can print ``telemetry_table.telemetry[<TAG>]`` in gdb.

.. code-block:: shell

   west attach
   (gdb) print telemetry_table.telemetry[7]
   $6 = 47

Tag IDs
-------

Tag IDs are permanent in the sense that they’ll never change meaning, but may not be present.

See `telemetry group </tt-system-firmware/doxygen//group__telemetry__tags.html>`_ for more details.
