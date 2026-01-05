.. _ttzp_repo_management:

Repository Management and Structure
===================================

This guide explains how to manage repositories within the tt-zephyr-platforms project.

Repository Structure
--------------------

The tt-zephyr-platforms project utilizes a `"T2 star topology"
<https://docs.zephyrproject.org/latest/develop/west/workspaces.html#t2-star-topology-application-is-the-manifest-repository>`_
with west, Zephyr's repo management tool, to manage multiple repositories. The
application repository serves as the manifest repo, while other repositories are
organized as manifest projects.

Most repositories are pulled directly from Zephyr's manifest, while some are
tracked as separate projects for easier management and updates.

Zephyr Fork Management
----------------------

Tenstorrent maintains a fork of the Zephyr repository to incorporate custom
changes and features specific to our hardware platforms. This fork is regularly
synchronized with the upstream Zephyr repository to ensure compatibility and
access to the latest features.

Updating Zephyr Fork
********************

To update the Tenstorrent Zephyr fork with the latest changes from the upstream
Zephyr repository, follow these steps:

1. Navigate to the local clone of the Tenstorrent Zephyr fork.

2. Add the upstream Zephyr repository as a remote if it hasn't been added already:

   .. code-block:: shell

      git remote add upstream https://github.com/zephyrproject-rtos/zephyr.git

3. Fetch the latest changes from the upstream repository:

   .. code-block:: shell

      git fetch upstream

4. Check out the revision you want to apply Tenstorrent changes to (e.g., ``v4.4.0-rc1``):

   .. code-block:: shell

      git checkout v4.4.0-rc1
      # Save branch description for later
      export BRANCH_NAME=$(git describe)

5. Check out a new branch for the rebase:

   .. code-block:: shell

      # Jump to current tenstorrent branch
      west update zephyr
      # Make new branch for rebase
      git switch -c tt-zephyr-$BRANCH_NAME

6. Rebase the Tenstorrent changes onto the selected upstream revision. Make
   sure to drop any commits that have already been merged upstream to avoid
   conflicts. If a commit has a conflict while rebasing, resolve the conflict
   within that commit and then continue the rebase.

   .. code-block:: shell

      git rebase ${BRANCH_NAME}

7. After the rebase is complete, test the changes to ensure everything works as
   expected, and push the updated fork to the remote repository:

   .. code-block:: shell

      git remote add tt-zephyr-push git@github.com:tenstorrent/zephyr-fork.git
      git push tt-zephyr-push tt-zephyr-$BRANCH_NAME

8. Update the application repository to point to the new revision of the
   Tenstorrent Zephyr fork by modifying the manifest file accordingly.

9. Commit and push the changes to the application repository.

Proposing Changes to Zephyr Fork
********************************

When proposing changes to the Tenstorrent Zephyr fork, you should submit a pull
request (PR) to the Tenstorrent Zephyr repository on GitHub, and reference this
PR from the application repository PR. This ensures that all changes are tracked and
reviewed appropriately.

For more details on this process, see the example for contributing module
changes within Zephyr itself: `Zephyr Module Contribution Guide
<https://docs.zephyrproject.org/4.3.0/develop/modules.html#process-for-submitting-changes-to-existing-modules>`_.
