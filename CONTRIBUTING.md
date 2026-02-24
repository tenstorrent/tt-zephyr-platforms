# Contributing to TT-System-Firmware

Thank you for your interest in contributing to TT-System-Firmware! We welcome bug reports, bug fixes, and enhancements from the community.

## How to Contribute

### Reporting Bugs

If you find a bug in the firmware, please open an issue on GitHub:

1. Navigate to the [Issues](https://github.com/tenstorrent/tt-system-firmware/issues) page
2. Click **New Issue**
3. Provide a clear description of the bug, including:
   - Steps to reproduce
   - Expected behavior
   - Actual behavior
   - Environment details (hardware, OS, build configuration)
   - Any relevant logs or error messages

### Submitting Pull Requests

We follow a development flow similar to the upstream Zephyr project. Pull requests are reviewed weekly.

#### Before You Start

1. Review our [Coding Guidelines](doc/contribute/coding_guidelines/index.rst)
2. Ensure you understand our commit and PR requirements

#### Pull Request Guidelines

Following [Zephyr's contributor expectations](https://docs.zephyrproject.org/latest/contribute/contributor_expectations.html), we ask that you:

- **Make PRs with the smallest logical change possible** - Aim to add one feature per PR. This makes code review easier and leads to faster merges.
- **Split PRs into multiple commits where appropriate** - The firmware should build with each commit, though commits may only partially enable the new feature.
- **Ensure all commits are bisectable** - The firmware for the SMC and BMC should build without warnings or errors on each commit.
- **Pass all code style checks** - Avoid commits that only fix formatting issues; resolve them in the original commit instead.

#### Commit Message Guidelines

We follow [Zephyr's Commit Message Guidelines](https://docs.zephyrproject.org/latest/contribute/guidelines.html#commit-message-guidelines):

- **Include a Signed-off-by line** - Add this with `git commit -s` or `git commit --amend -s`
  
  Example:
  ```
  Signed-off-by: Your Name <your.email@example.com>
  ```

- **Write clear, descriptive commit messages** - Use the imperative mood ("Add feature" not "Added feature")
- **Reference related issues** - If your commit addresses a GitHub issue, reference it in the commit message

#### Responding to Change Requests

The best way to address review feedback is using `git rebase`. See the [Zephyr Contribution Workflow](https://docs.zephyrproject.org/latest/contribute/guidelines.html#contribution-workflow) for detailed instructions.

Quick summary:
1. `git rebase -i <commit-to-fix>^`
2. Change `pick` to `edit` for the commit you want to modify
3. Make your changes and run `git add <files>`
4. Continue with `git rebase --continue`
5. Force push your updated branch

### Review Process

- Pull requests are reviewed on a **weekly basis**
- Maintainers will provide feedback and may request changes
- Once approved, your PR will be merged to the main branch

## Code of Conduct

This project adheres to the [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code. Please report unacceptable behavior to ospo@tenstorrent.com.

## Development Setup

For information on setting up your development environment and building the firmware, see our [Getting Started Guide](https://docs.tenstorrent.com/tt-system-firmware/develop/getting_started/index.html).

## Questions?

If you have questions about contributing, feel free to:
- Open a discussion in [GitHub Discussions](https://github.com/tenstorrent/tt-system-firmware/discussions)
- Ask in a GitHub Issue

We appreciate your contributions to making TT-System-Firmware better!
