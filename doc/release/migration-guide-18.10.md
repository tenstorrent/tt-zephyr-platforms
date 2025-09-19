# v18.10.0

## Migration Guide

This document lists recommended and required changes for those migrating from the previous v18.9.0 firmware release to the new v18.10.0 firmware release.

* PVT driver has been migrated to a [Zephyr sensor driver](https://docs.zephyrproject.org/latest/hardware/peripherals/sensor/index.html), using the new [read/decode API](https://docs.zephyrproject.org/latest/hardware/peripherals/sensor/read_and_decode.html#sensor-read-and-decode).
