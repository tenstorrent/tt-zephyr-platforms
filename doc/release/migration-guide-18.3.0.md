# Migration Guide: TT-Zephyr-Platforms v18.3.0

---

This document lists recommended and required changes for those migrating from the previous v18.2.0 firmware release to the new v18.3.0 firmware release.

[comment]: <> (UL by area, indented as necessary)

* Going forward, and following the more standard semantic versioning of firmware, the filename
  of the combined firmware bundles will be of the form
  * `fw_pack-<major>.<minor>.<patch>.fwbundle` for final releases, and
  * `fw_pack-<major>.<minor>.<patch>-rc<N>.fwbundle` for release candidates (pre-releases)

* The Board Management Controller (BMC) on Blackhole cards has been renamed to Device Management
  Controller (DMC) in order to reduce possible confusion around the standard industry Baseboard
  Management Controller (BMC) which is part of the [Intelligent Platform Management Interface](https://en.wikipedia.org/wiki/Intelligent_Platform_Management_Interface)
  (IPMI) specification. Any automation or scripts that refer to that "cpu cluster" (in Zephyr
  terminology) or application (`app/bmc`) should be adjusted accordingly.
