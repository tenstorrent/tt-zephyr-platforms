.. _ttzp_binary_descriptors:

Binary Descriptors
==================

Binary descriptors embed firmware version and git revision directly into firmware images using Zephyr's
`bindesc <https://docs.zephyrproject.org/latest/services/binary_descriptors/>`_
subsystem. This allows the version and source revision of a firmware binary to
be extracted without running it.

Enabled Descriptors
-------------------

Both the SMC and DMC firmware images embed the following descriptors:

.. list-table::
   :header-rows: 1
   :widths: 30 15 55

   * - Descriptor
     - Type
     - Description
   * - ``APP_VERSION_STRING``
     - STR
     - Semantic version from the app's ``VERSION`` file (e.g. ``"0.28.0-rc1"``).
   * - ``APP_BUILD_VERSION``
     - STR
     - Git revision from ``git describe`` (e.g. ``"v19.6.0-rc1-3-gb7209fb40886"``).

The version string is sourced from each application's ``VERSION`` file
(``app/smc/VERSION``, ``app/dmc/VERSION``). The git revision is generated
automatically by the build system using ``git describe``.

Reading Descriptors
-------------------

Zephyr's ``west bindesc`` extension can list or search for descriptors:

.. code-block:: bash

   # List all descriptors in an image
   west bindesc list build/smc/zephyr/zephyr.bin

   # Search for a specific descriptor
   west bindesc search APP_VERSION_STRING build/smc/zephyr/zephyr.bin
