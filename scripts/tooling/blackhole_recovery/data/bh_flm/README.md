# Blackhole Flash Loader Module

This directory contains support code which enables pyocd to load data to
the SPI EEPROM present on blackhole PCIe cards via the STM32 MCU (DMC).

The files contained here are sourced from the following locations:
- `STM32G0Bx_SPI_EEPROM.FLM`: build from fork of the pyocd flash algorithm repo,
  hosted here: https://github.com/danieldegrasse/FlashAlgo/tree/c616c215faebc885e6733c730e22d1d267f6aa04
- `Keil.STM32G0xx_DFP.2.0.0.pack`: Device support pack for STM32G0xxx, downloaded
  from: https://www.keil.com/pack/Keil.STM32G0xx_DFP.2.0.0.pack
- `pyocd_config.py`: Custom user script to override flash read algorithm in pyocd
  and add the custom flash loader module for the eeprom. Hosted here:
  https://github.com/danieldegrasse/FlashAlgo/blob/c616c215faebc885e6733c730e22d1d267f6aa04/flash-algo/pyocd_config.py
