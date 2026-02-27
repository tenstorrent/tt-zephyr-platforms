
# Wormhole Flash Loader Module

## N300 SPI pins on STM32

LEFT_M3_SPI_INTERFACE (SPI2)
- NSS/CS: PB12
- MISO: PB14
- MOSI: PB15
- SCK/CLK: PB13
- SPI MUX: PA8

RIGHT_M3_SPI_INTERFACE (SPI1)
- NSS/CS: PA15
- MISO: PB4
- MOSI: PA12[PA10]
- SCK/CLK: PB3
- SPI MUX: PA11[PA9]

## How to debug

1. Add a loop in the FLM code where you want gdb to halt
2. Create a dummy .bin file to be the payload, this won't actually be flashed if you have a loop in your code
3. Run pyocd commander:

pyocd commander -vvv --target stm32g031c6ux --script data/wh_flm/pyocd_config_spi1.py -c load data/wh_flm/something.bin 0x0

It should throw a timeout error and in the logs will be a line like the following:

0000442 D flash algo: [stack=0x20000550; 0x550 b] [b2=0x20000650,+0x650] [b1=0x20000550,+0x550] [code=0x2000075c,+0x75c,0x18a4 b] (ram=0x20000000, 0x2000 b) [flash_algo]

Grab the address to debug at from here (code: 0x2000075c)

4. pyocd gdbserver --target stm32g031c6ux
5. gdb-multiarch data/wh_flm/build/spi1.flm

then add the following to gdb:
- file data/wh_flm/build/spi1.flm 
- add-symbol-file data/wh_flm/build/spi1.flm 0x0 -s PrgCode 0x2000075c
- target remote localhost:3333