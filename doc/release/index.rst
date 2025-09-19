.. _ttzp_release:

Releases
########

Tenstorrent firmware follows a release workflow that is similar to that of the
:ref:`Zephyr Project<zephyr_release_notes>` with some differences.

* Tenstorrent releases firmware with a more frequent cadence
* Tenstorrent firmware does not (yet) have a Long Term Support (LTS) release

.. _ttzp_release_cycle:

Release Life Cycle and Maintenance
**********************************

Tenstorrent firmware follows a documented :ref:`ttzp_release_process` in order to deliver
stability, new features, and bug fixes to customers in a timely manner. Firmwware is tested
functionally on every commit, on a nightly basis for extendd testing, and goes through a
rigourous QA process before being released.

Periodic Releases
=================

Tenstorrent firmware is released periodically, every 2 weeks on the Monday, by EOD. There is at
least one release candidate (e.g. ``v1.2.3-rc1``) before the final release (e.g. ``v1.2.3``).

Firmware versions for separate components (e.g. SMC, DMC) are numerically independent, and are
derived from the ``VERSION`` file in the application directory (e.g. ``app/smc/VERSION`` for SMC
firmware).

Collectively, all components use a bundle version that is derived from the ``VERSION`` file at
the root of the repository.

Although the version numbers of individual components may not match, they do have the same
release cadence, and are released together as a bundle for release candidates and final releases.

Version Numbering
=================

Tenstorrent firmware uses `semantic versioning`_ where tagged version numbers follow a
``MAJOR.MINOR.PATCH`` format.

1. ``MAJOR`` version when there are incompatible API changes.
2. ``MINOR`` version when new functionalities were added in a
   backward-compatible manner.
3. ``PATCH`` version when there are backward-compatible bug fixes.
4. ``EXTRAVERSION`` release-candidate suffix (e.g. ``rc1``).

We add pre-release tags using the format ``MAJOR.MINOR.PATCH-EXTRAVERSION``.

Stabilization Period
====================

A stabilization period will occur prior to each release, where tags are made for at
least one release candidate, ``a.b.c-rc1``. If needed, additional release candidates ``a.b.c-rc2``
may be made, ultimately followed by the official and final ``a.b.c`` release.

During the stabilization period between ``rc1`` and the final release, the only changes that should
be merged into the ``main`` branch are to improve testing, fix bugs, correct documentation and other
cosmetic fixes, and explicitly not to introduce other major changes or features. The stabilization
period is effectively a feature freeze.

Exceptions
==========

Features may only be merged during the stabilization period with the explicit approval of the
Change Review Board (CRB). The author of the change must accompany the Release Manager (RM) to a
CRB meeting to provide justification for merging the change and to seek CRB approval.

Release Notes and Migration Guides
**********************************

.. _ttzp_release_notes:

Release Notes
=============

Each release is accompanied by a set of release notes that document the changes
included in the release.

.. toctree::
   :maxdepth: 1
   :glob:

   release-notes-*

.. _ttzp_migration_guide:

Migration Guides
================

In order to ensure that there is a clear upgrade path for firmware, if there are specific changes,
procedures, or API modifications that require attention when moving from one release to the next,
then entries are added to the migration guide.

.. toctree::
   :maxdepth: 1
   :glob:

   migration-guide-*

.. _semantic versioning: https://en.wikipedia.org/wiki/Software_versioning

Additional Release Documentation
********************************

.. toctree::
   :maxdepth: 1

   release-process.rst
