# Release Process

This page describes the release process used by Tenstorrent firmware built upon the Zephyr
Real-Time Operating System (a.k.a. `tt-zephyr-platforms`).

## Version numbering

`tt-zephyr-platforms` firmware uses
[semantic versioning](https://en.wikipedia.org/wiki/Software_versioning) where version numbers
follow a `MAJOR.MINOR.PATCH` format.

1. `MAJOR` version when there are incompatible API changes.
2. `MINOR` version when new functionalities were added in a
   backward-compatible manner.
3. `PATCH` version when there are backward-compatible bug fixes.
4. `EXTRAVERSION` release-candidate suffix (e.g. `rc1`).

We add pre-release tags using the format `MAJOR.MINOR.PATCH-EXTRAVERSION`.

> [!NOTE]
> Current firmware bundle files include a fourth version field for experimental builds. For
> official releases, the fourth version field is always zero.

## Release candidates

Before each release, tags are made (see below) for at least one release candidate (`a.b.c-rc1`,
followed by `a.b.c-rc2` and the subsequent release candidates, followed by the official `a.b.c`
release). The intent is to freeze the code and allow testing.

During the time between the `rc1` and the final release, the only changes that should be merged into
the `main` branch are those to fix bugs found in the release candidate, to correct documentation,
or to make other cosmetic fixes.

## Release Procedure

The following steps are required to be followed by firmware release engineers when creating a new
`tt-zephyr-platforms` release.

### Release Checklist

Refer to the previous release checklist for steps required before tagging release candidates and
final releases.

### Tagging

> [!NOTE]
> This section uses a fake release version, `v1.2.3`, or `v1.2.3-rc1` as an example. Replace with
> the appropriate release candidate or final release version.

> [!IMPORTANT]
> Any changes to `app/*/VERSION` files is required to take place prior to the release process.

1. Update the version variables in the bundle version file `VERSION` file located in the root of
    the Git repository to match the version for this release. The `EXTRAVERSION` variable is used
    to identify release candidates. It is left empty for final releases.

    ```
    EXTRAVERSION = rc1
    ```

2. Post a PR with the updated `VERSION` file using `release: 1.2.3-rc1` or `release: 1.2.3` as
    the commit subject. Merge the PR after CI is successful.

3. Tag and push the version, using an annotated tag:

    ```shell
    git tag -s -m "tt-zephyr-platforms 1.2.3-rc1" v1.2.3-rc1
    ```

4. Verify that the tag has been
    [signed correctly](https://docs.github.com/en/authentication/managing-commit-signature-verification/telling-git-about-your-signing-key),
    `git show` for the tag must contain a signature (look for the `BEGIN PGP SIGNATURE` or
    `BEGIN SSH SIGNATURE` marker in the output):

    ```shell
    git show v1.2.3-rc1
    ```

5. Push the tag:

    ```shell
    git push git@github.com:tenstorrent/tt-zephyr-platforms.git v1.2.3-rc1
    ```

Lastly, for final releases,

6. Find the generated
    [draft release](https://github.com/tenstorrent/tt-zephyr-platforms/releases), edit the release
    notes, and publish the release.

7. Announce the release via official Tenstorrent channels and provide a link to the
    GitHub release page.

### Publishing a Combined Firmware Bundle to `tt-firmware`

Find the `.fwbundle` file and combine it with closed-source firmware versions for older Tenstorrent
products.

```shell
scripts/tt_boot_fs.py fwbundle \
  -o fw_pack-MAJOR.MINOR.PATCH.0.fwbundle \
  -v "MAJOR.MINOR.PATCH.0"
  -c tt-zephyr-platforms-MAJOR.MINOR.PATCH.0.fwbundle \
  -c closed-frmware-pack-MAJOR.MINOR.PATCH.0.fwbundle
```

Make a pull request to [tt-firmware](https://github.com/tenstorrent/tt-firmware) with the new
combined firmware bundle.
