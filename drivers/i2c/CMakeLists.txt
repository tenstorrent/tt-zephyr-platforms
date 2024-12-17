# Copyright (c) 2019, PHYTEC Messtechnik GmbH
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_TT_I2C_DRIVER)

zephyr_library()

zephyr_library_include_directories(
	${ZEPHYR_BASE}/drivers/i2c
)
zephyr_library_sources(
	tt_stm32_i2c.c
)

zephyr_library_sources_ifdef(CONFIG_TT_I2C_STM32_INTERRUPT
	tt_stm32_i2c_int_api.c
)
zephyr_library_sources_ifdef(CONFIG_TT_I2C_STM32_POLL
	tt_stm32_i2c_poll_api.c
)
zephyr_library_sources_ifdef(CONFIG_TT_I2C_STM32_BYTE_POLL
	tt_stm32_i2c_byte_poll_api.c
)

endif()
