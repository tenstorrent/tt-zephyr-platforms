# TT-ZEPHYR-PLATFORMS

[![Build](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/build-fw.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/build-fw.yml)
[![Run Unit Tests](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/run-unit-tests.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/run-unit-tests.yml)
[![HW Smoke](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/hardware-smoke.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/hardware-smoke.yml)
[![HW Soak](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/hardware-long.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/hardware-long.yml)
[![Metal](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/hardware-long.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/metal.yml)

[P100A CI History](https://docs.tenstorrent.com/tt-zephyr-platforms/p100a_ci_stats.html)
[P150A CI History](https://docs.tenstorrent.com/tt-zephyr-platforms/p150a_ci_stats.html)
[P300A CI History](https://docs.tenstorrent.com/tt-zephyr-platforms/p300a_ci_stats.html)

Welcome to TT-Zephyr-Platforms!

This is the Zephyr firmware repository for [Tenstorrent](https://tenstorrent.com) AI ULC.

![Zephyr Shell on Blackhole](./doc/img/shell.gif)

## Getting Started

To get started with the development environment, building and flashing to a board, please refer to our [Getting Started docs](https://docs.tenstorrent.com/tt-zephyr-platforms/develop/getting_started/index.html).

## Further Reading

Learn more about `west`
[here](https://docs.zephyrproject.org/latest/develop/west/index.html).

Learn more about `twister`
[here](https://docs.zephyrproject.org/latest/develop/test/twister.html).

For more information on creating Zephyr Testsuites, visit
[this](https://docs.zephyrproject.org/latest/develop/test/ztest.html) page.

## Software License

This source code in this repository is made available under the terms of the
[Apache-2.0 software license](https://www.apache.org/licenses/LICENSE-2.0), as described in the
accompanying [LICENSE](LICENSE) file.

Additional binary artifacts are separately licensed with terms be found in
[zephyr/blobs/license.txt](zephyr/blobs/license.txt).

For the avoidance of doubt, this software assists in programming
[Tenstorrent](https://tenstorrent.com) products. However, making, using, or selling hardware,
models, or IP may require the license of rights (such as patent rights) from Tenstorrent or
others.
