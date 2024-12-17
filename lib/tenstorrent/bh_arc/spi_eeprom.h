#ifndef SPI_EEPROM_H
#define SPI_EEPROM_H

#include <stdint.h>

void SpiBufferSetup();
void EepromSetup();
void ReinitSpiclk();
void SpiBlockRead(uint32_t spi_address, uint32_t num_bytes, uint8_t *dest);
void SpiSmartWrite(uint32_t address, const uint8_t *data, uint32_t num_bytes);

#endif
