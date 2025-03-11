.. zephyr:code-sample:: flash_performance
   :name: Flash Performance Sample

   Tests flash device read/write performance

Overview
********

This sample is designed to test flash device read/write performance. It
erases a section of flash, writes a buffer over the erased area, and then reads
back the data. The sample measures the time taken to perform each operation.

Building and Running
********************

This application can be built and executed as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/flash/performance
   :host-os: unix
   :board: tt_blackhole@p100/tt_blackhole/smc
   :goals: debug
   :compact:

To build for another board, change "tt_blackhole@p100/tt_blackhole/smc"
above to that board's name.

Sample Output
=============

.. code-block:: console
   Erasing 8 pages at 0x2000000
   Erase took 115 ms
   Write of 32768 bytes took 222 ms
   Read of 32768 bytes took 3 ms
   Flash performance test complete
