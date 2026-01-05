## Building for Grendel

To build the UART elementary test for grendel sim, use the following command:
```
west build -p always -b tt_grendel_smc \
	../zephyr/tests/drivers/uart/uart_elementary/ \
	-DDTC_OVERLAY_FILE=$PWD/test-conf/tests/drivers/uart/uart_elementary/tt_grendel_smc.overlay\
	-DEXTRA_CONF_FILE=$PWD/test-conf/tests/drivers/uart/uart_elementary/tt_grendel_smc.conf
