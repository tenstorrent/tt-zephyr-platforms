# TT-System-Firmware

[![Build](https://github.com/tenstorrent/tt-system-firmware/actions/workflows/build-fw.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-system-firmware/actions/workflows/build-fw.yml)
[![Run Unit Tests](https://github.com/tenstorrent/tt-system-firmware/actions/workflows/run-unit-tests.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-system-firmware/actions/workflows/run-unit-tests.yml)
[![HW Smoke](https://github.com/tenstorrent/tt-system-firmware/actions/workflows/hardware-smoke.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-system-firmware/actions/workflows/hardware-smoke.yml)
[![HW Soak](https://github.com/tenstorrent/tt-system-firmware/actions/workflows/hardware-long.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-system-firmware/actions/workflows/hardware-long.yml)
[![Metal](https://github.com/tenstorrent/tt-system-firmware/actions/workflows/metal.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-system-firmware/actions/workflows/metal.yml)

[P100A CI History](https://docs.tenstorrent.com/tt-system-firmware/p100a_ci_stats.html)
[P150A CI History](https://docs.tenstorrent.com/tt-system-firmware/p150a_ci_stats.html)
[P300A CI History](https://docs.tenstorrent.com/tt-system-firmware/p300a_ci_stats.html)

Welcome to TT-System-Firmware!

This is the Zephyr firmware repository for [Tenstorrent](https://tenstorrent.com) AI ULC.

![Zephyr Shell on Blackhole](./doc/img/shell.gif)

## Getting Started

To get started with the development environment, building and flashing to a board, please refer to our [Getting Started docs](https://docs.tenstorrent.com/tt-system-firmware/develop/getting_started/index.html).

## Further Reading

Learn more about `west`
[here](https://docs.zephyrproject.org/latest/develop/west/index.html).

Learn more about `twister`
[here](https://docs.zephyrproject.org/latest/develop/test/twister.html).

For more information on creating Zephyr Testsuites, visit
[this](https://docs.zephyrproject.org/latest/develop/test/ztest.html) page.

## Contributing

We welcome contributions from the community! Please see our [Contributing Guidelines](CONTRIBUTING.md) for information on how to report bugs, submit pull requests, and participate in the development process.

This project follows the [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code.

## License

This repository contains multiple licenses for different components:

- **Overall license for this project, except where specified**: [Apache-2.0](LICENSE) - [Understanding the License](LICENSE_understanding.txt)
- **Documentation and images**: [Creative Commons Attribution 4.0 International (CC-BY)](LICENSE-DOCS)
- **Binary artifacts**: [TT Blob Software License Agreement](zephyr/blobs/license.txt) (Proprietary)

For the full license text and copyright information, please see the [NOTICE](NOTICE) file.
