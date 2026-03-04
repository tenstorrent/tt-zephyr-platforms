.. _tt_grendel_mk_bu_dmc:

Tenstorrent MK Bring up Board (DMC)
###################################

Overview
********

The Tenstorrent Grendel MK BU Device Management Controller (DMC) is a custom board
based on the STM32U385RG microcontroller. It provides device management
capabilities for Tenstorrent AI chips.

Hardware
********

The board is built around the STM32U385RG microcontroller.

Communication Interfaces
========================

The following interfaces are configured in the device tree:

- UART4: PA0 (TX), PA1 (RX) - Console/Shell
- I2C1: PB8 (SCL), PB7 (SDA)
- I2C2: PB10 (SCL), PB14 (SDA)
- I2C3: PA7 (SCL), PB4 (SDA)
- SPI2: PA9 (SCK), PC2 (MISO), PC3 (MOSI)
- USB: PA11 (D-), PA12 (D+)

Shell Commands
**************

The board provides shell commands for testing and interacting with various peripherals.

GPIO Commands
=============

Test GPIO functionality using the shell:

.. code-block:: console

   # Configure a GPIO pin as output, active high
   dm_test_app:~$ gpio conf gpioa 5 oh

   # Set GPIO pin high
   dm_test_app:~$ gpio set gpioa 5 1

   # Set GPIO pin low
   dm_test_app:~$ gpio set gpioa 5 0

   # Configure as input and read value
   dm_test_app:~$ gpio conf gpioa 5 ih
   dm_test_app:~$ gpio get gpioa 5

I2C Commands
============

Scan and communicate with I2C devices:

.. code-block:: console

   # Scan I2C bus for devices (e.g. for the i2c2 bus)
   dm_test_app:~$ i2c scan i2c2

   # Read from I2C device (device address 0x50, register 0x00, 1 byte)
   dm_test_app:~$ i2c read_byte i2c2 0x50 0x00

   # Write to I2C device (device address 0x50, register 0x00, value 0xFF)
   dm_test_app:~$ i2c write_byte i2c2 0x50 0x00 0xFF

SPI Commands
============

Test SPI communication:

.. code-block:: console

   # Configure SPI frequency first (e.g., 1MHz)
   dm_test_app:~$ spi conf spi2 1000000

   # Configure SPI transceive (send 0x01 0x02, print response)
   dm_test_app:~$ spi transceive spi2 1 01 02

Flash Commands
==============

Test internal flash functionality:

.. code-block:: console

   # Read data from internal flash (address 0x0, 16 bytes)
   dm_test_app:~$ flash read flash 0x0 16

   # Erase internal flash page (address 0x10000, 1 page)
   dm_test_app:~$ flash erase flash 0x10000 1

   # Write data to internal flash (address 0x10000, data "deadbeef")
   dm_test_app:~$ flash write flash 0x10000 deadbeef

Programming and Debugging
*************************

The board supports flashing via SWD interface using various tools.

Example Usage
=============

Build and flash an application:

.. code-block:: console

   $ west build -b tt_grendel_mk_bu_dmc samples/hello_world
   $ west flash --runner pyocd --frequency 100000
