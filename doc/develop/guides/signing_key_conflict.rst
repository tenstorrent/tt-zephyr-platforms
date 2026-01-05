.. _ttzp_signing_key_conflict:

Firmware Signing Key Conflict (v19.0.0+)
========================================

This guide explains how to recover from a firmware signing key conflict in firmware versions v19.0.0 and later.

This issue occurs in firmware versions v19.0.0 and later when mixing officially released (production-signed) firmware with locally built (development-signed) firmware.

The Core Problem
----------------

The Production-signed MCUBoot (Device Manager/DM Firmware), once installed, enforces a strict security check. It will permanently reject and fail to boot any subsequent DMFW image that is signed with the local development key.

The Failure Chain:
******************

* **Initial Flash:** Developer uses ``recover-blackhole.py`` to flash a production-signed fwbundle.
* **Lock Installed:** The restrictive production MCUBoot is installed.
* **Subsequent Flash:** Developer attempts to flash a local, development-signed fwbundle (``west build``).
* **Result:** The new DMFW won't be installed and the board will fall back to the previous production-signed firmware because the installed MCUBoot rejects the development key.

Recovery Methods (To Accept Development Firmware)
-------------------------------------------------

Base recovery command:

.. code-block:: shell

   python ./scripts/tooling/blackhole_recovery/recover-blackhole.py {recovery-fw-bundle-name} {board type} --force

.. list-table:: Recovery Methods
   :widths: 20 30 50
   :header-rows: 1

   * - Solution
     - Strategy
     - Steps
   * - **Option 1: Overwrite MCUBoot**
     - Manually replace the production bootloader with a permissive development version. (Complicated)
     - #. Flash development MCUBoot: ``west flash -d build/mcuboot-bl2``
       #. Re-flash DMFW (the old copy of mcuboot probably erased the update image in SPI flash): ``west flash --domain dmc``
       #. Flash any desired development firmware.
   * - **Option 2: Downgrade**
     - Use a non-secure firmware version to clear the lock. (Easy)
     - #. Flash a pre-v19.0.0 recovery image (e.g., `v18.12.1 <https://github.com/tenstorrent/tt-zephyr-platforms/releases/download/v18.12.1/fw-pack-v18.12.1-recovery.tar.gz>`_).
       #. The lock is removed; flash any subsequent firmware.
   * - **Option 3: Use Local Image**
     - Avoid installing the production MCUBoot entirely. (Recommended)
     - #. Always use your local development-signed fwbundle for the initial recovery flash.
       #. The permissive development MCUBoot is installed; flash any subsequent firmware.

To build a local recovery bundle:

.. code-block:: shell

   python3 ./scripts/tooling/blackhole_recovery/prepare-recovery-bundle.py <recovery-fw-bundle-name>
