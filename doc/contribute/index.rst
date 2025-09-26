.. _contribute:

=====================================
Contributing to TT Zephyr Platforms
=====================================

.. contents::
   :local:
   :depth: 2

.. toctree::
   :maxdepth: 2
   :hidden:

   coding_guidelines/index

Rationale
=========

With the tt-zephyr-platforms repo, we are aiming to follow a similar development flow to the process upstream Zephyr uses for submitting changes. Zephyr's requirements on the content of PRs and commits makes code review easier, and simplifies tracing changes in the codebase.

Furthermore, having a defined set of contribution guidelines for internal developers will allow us to accept external contributions to firmware, if this is something we choose to allow in the future.

PR Guidelines
=============

The guidelines we wish to follow are summarized in `Zephyr's contributor expectations <https://docs.zephyrproject.org/latest/contribute/contributor_expectations.html#defining-smaller-prs>`_.

Key principles:

- **Make PRs with the smallest logical change possible.** For example, aim to only add one feature per PR. This eases code review, and leads to faster merges.
- **Split PRs into multiple commits where possible.** The firmware should build with each commit, but the commit may only partially enable the new feature the PR targets.

Commit Guidelines
=================

Commit message guidelines can be found in the `Zephyr Commit Message Guidelines <https://docs.zephyrproject.org/latest/contribute/guidelines.html#commit-message-guidelines>`_.

In particular:

- **Commits must contain a Signed-off-by line.** This can easily be added to a commit with ``git commit -s``, or ``git commit --amend -s`` to sign off the previous commit.
- **Commits should maintain bisectability** - the firmware for the SMC and BMC should build without warnings or errors on each commit in a PR.
- **Each commit should pass code style checks.** This means that we should avoid commits specifically fixing formatting issues - rather, resolve the issues in the original commit.

Coding Guidelines
=================

For detailed information about coding style and guidelines, please see the :doc:`coding_guidelines/index`.

Responding to Change Requests
=============================

Generally, the best way to make fixes to code in a PR is using ``git rebase``. The process is described within the `Zephyr Contribution Workflow <https://docs.zephyrproject.org/latest/contribute/guidelines.html#contribution-workflow>`_ (step 14).

To summarize:

1. Run ``git rebase -i <commit-to-fix>^``
2. Replace ``pick`` with ``edit`` in the interactive rebase editor, then exit the editor
3. Make the changes requested to the commit, then add them to the current commit with ``git add <files>`` and ``git rebase --continue``
