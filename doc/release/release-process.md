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

A stabilization period will occur prior to each release, where tags are made (see below) for at
least one release candidate, `a.b.c-rc1`. If needed, additional release candidates `a.b.c-rc2`
may be made, ultimately followed by the official and final `a.b.c` release.

During the stabilization period between `rc1` and the final release, the only changes that should
be merged into the `main` branch are to improve testing, fix bugs, correct documentation and other
cosmetic fixes, and explicitly not to introduce other major changes or features. The stabilization
period is effectively a feature freeze.

### Exceptions

Features may only be merged during the stabilization period with the explicit approval of the
Change Review Board (CRB). The author of the change must accompany the Release Manager (RM) to a
CRB meeting to provide justification for merging the change and to seek CRB approval.

## Release Procedure

The following steps are required to be followed by firmware release engineers when creating a new
`tt-zephyr-platforms` release.

### Release Checklist

Create an issue with the title `Release Checklist va.b.c`. If the issue does not already exist,
then create it using the previous release checklist as a template. The checklist will list the
steps required before tagging a final release.

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
    notes, and publish the release. It is recommended to copy the contents of
    `doc/release/release-notes-1.2.3.md` into the release-notes area.

7. Announce the release via official Tenstorrent channels and provide a link to the
    GitHub release page.

### Publishing a Combined Firmware Bundle to `tt-firmware`

Make a pull request to [tt-firmware](https://github.com/tenstorrent/tt-firmware) with the new
combined firmware bundle and accompanying required changes.

As part of the pull request:

1. Clone the `tt-firmware` repository (if it has not already been cloned).

```shell
if [ ! -d ~/tt-firmware ]; then
  git clone git@github.com:tenstorrent/tt-firmware.git ~/tt-firmware
  cd ~/tt-firmware
fi
```

2. Create a branch to make modifications for the release.

```shell
git checkout -b release-v1.2.3
```

3. Download the `fw_pack-<version>.fwbundle` file in the associated `tt-zephyr-platforms`
    [release](https://github.com/tenstorrent/tt-zephyr-platforms/releases).

```shell
wget https://github.com/tenstorrent/tt-zephyr-platforms/releases/download/v1.2.3/fw_pack-1.2.3.fwbundle
```

4. Change the `latest.fwbundle` symbolic link to point to the new firmware bundle and remove the
    older version, staging the files for commit.

```shell
git rm -f $(readlink latest.fwbundle) latest.fwbundle
ln -sf fw_pack-1.2.3.fwbundle latest.fwbundle
git add *.fwbundle
```

5. Edit the `README.md` file following the existing structure. Update the "Available Firmware"
    and "Release Notes" sections, staging the file for commit.

```shell
$EDITOR README.md
git add README.md
```

6. Make a signed commit (`git commit -s`) for the changes using the commit subject and body below.

```
release: fw bundle v1.2.3

Release FW Bundle v1.2.3

Signed-off-by: Your Name <your@email.com>
```

7. Create a pull request for the changes. After the PR has been merged, refresh the `main` branch,
    and tag the release with a signed commit.

```shell
git checkout main
git pull
git tag -s -m "tt-firmware 1.2.3" v1.2.3
git push --tags
```

8. Create a new `tt-firmware`
    [release from the new tag](https://github.com/tenstorrent/tt-firmware/releases/new) by
    copying the contents of `doc/release/release-notes-1.2.3.md` into the release notes for
    the new `tt-firmware` release.
