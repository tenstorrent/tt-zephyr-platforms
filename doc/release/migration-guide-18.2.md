# Migration Guide: TT-Zephyr-Platforms v18.2.0

---

This document lists recommended and required changes for those migrating from the previous v80.18.1 firmware release to the new v18.2.0 firmware release.

> [!IMPORTANT]
> TT-KMD Users are required to update v1.33 to ensure MPS limit is properly saved and restored.

[comment]: <> (UL by area, indented as necessary)

* Firmware Versioning: TT Zephyr Platforms has dropped the previous legacy `v80.major.minor.rc` numbering to more traditional [Semantic Versioning](https://semver.org). E.g.
  * `v18.2.0-rc1` for release candidates
  * `v18.3.0` when the next minor version increases
  * `v19.0.0` when the next major version increases
