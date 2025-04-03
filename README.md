# TT-ZEPHYR-PLATFORMS

[![Build](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/build-fw.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/build-fw.yml)
[![Run Unit Tests](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/run-unit-tests.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/run-unit-tests.yml)
[![HW Smoke](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/hardware-smoke.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/hardware-smoke.yml)
[![HW Soak](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/hardware-long.yml/badge.svg?branch=main)](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/hardware-long.yml)

Welcome to TT-Zephyr-Platforms!

This is the Zephyr firmware repository for [Tenstorrent](https://tenstorrent.com) AI ULC.

## Getting Started

For those completely new to Zephyr, please refer to the
[Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

The remainder of these instructions assume that system requirements, a Python virtual environment,
all Python dependencies, and the Zephyr SDK are already installed and activated.

### Check-out Sources

```shell
# Create a west workspace
MODULE=tt-zephyr-platforms
west init -m https://github.com/tenstorrent/tt-zephyr-platforms.git ~/$MODULE-work
cd ~/$MODULE-work

# Fetch Zephyr modules
west update

# Verify binary blobs
west blobs fetch

# Apply local patches
west patch apply

# Set up Zephyr environment
source zephyr/zephyr-env.sh

# Enter the module
cd $MODULE.git
```

### Build & Flash

**Build and flash targets with `west`**

```shell
# Build tt-console
gcc -Iinclude -std=gnu11 -o tt-console scripts/tt-console/console.c

# Build and flash firmware
west build --sysbuild -p -b tt_blackhole@p100/tt_blackhole/smc app/smc
west flash -r tt_flash --force

# Reset the board and rescan the PCIe bus
tt-smi -r
./scripts/rescan-pcie.sh

# Interact via tt-console
./scripts/tt-console/tt-console
```

Output should appear as shown below
```shell
Press Ctrl-a,x to quit
         .:.                 .:
      .:-----:..             :+++-.
   .:------------:.          :++++++=:
 :------------------:..      :+++++++++
 :----------------------:.   :+++++++++
 :-------------------------:.:+++++++++
 :--------:  .:-----------:. :+++++++++
 :--------:     .:----:.     :+++++++++
 .:-------:         .        :++++++++-
    .:----:                  :++++=:.
        .::                  :+=:
          .:.               ::
          .===-:        .-===-
          .=======:. :-======-
          .==================-
          .==================-
           ==================:
            :-==========-:.
                .:====-.

*** Booting tt_blackhole with Zephyr OS v4.1.0 ***
Tenstorrent Blackhole CMFW 0.9.1
```

### Build, Flash, Debug & Test BMC FW

**Build, flash, and view output from the target with `west`**
```shell
# Set up a convenience variable for BMC FW
BOARD=tt_blackhole/tt_blackhole/bmc
BOARD_SANITIZED=tt_blackhole_tt_blackhole_bmc

# Build BMC firmware
west build --sysbuild -p -S rtt-console -b $BOARD app/bmc

# Flash mcuboot and the app
west flash

# Open RTT viewer
west rtt
```

Console output should appear as shown below.
```shell
*** Booting MCUboot v2.1.0-rc1-233-g346f7374ff44 ***
*** Using Zephyr OS build v4.1.0 ***
I: Starting bootloader
I: Primary image: magic=good, swap_type=0x2, copy_done=0x1, image_ok=0x1
I: Secondary image: magic=good, swap_type=0x2, copy_done=0x3, image_ok=0x3
I: Boot source: none
I: Image index: 0, Swap type: test
I: Starting swap using move algorithm.
I: Bootloader chainload address offset: 0xc000
I: Image version: v0.3.2
I: Jumping to the first image slot
*** Booting Zephyr OS build 7823374e8721 ***
```

**Build and run tests on hardware with `twister`**

```shell
twister -i -p $BOARD --device-testing --west-flash \
  --device-serial-pty rtt --west-runner openocd \
  -s samples/hello_world/sample.basic.helloworld.rtt \
  -s tests/boot/test_mcuboot/bootloader.mcuboot.rtt
```

**Re-flash stable firmware**

Note: `fw.hex` is mostly available on CI machines at this time although nothing prevents any
developer from creating a similar directory structure and firmware hex file on developer machines.
The file `fw.hex` is a concatenation of the mcuboot `zephyr.bin` and the `app/smc`
`zephyr.signed.bin` concatenated with the tool `srec_cat`.

```shell
./scripts/bmc-reset.py /opt/tenstorrent/fw/stable/$BOARD_SANITIZED/fw.hex
./scripts/rescan-pcie.sh
```

## Enable Git Hooks for Development

To add git hooks to check your commits and branch prior to pushing to insure
they do not have any formatting or compliance issues, you can run

```shell
tt-zephyr-platforms/scripts/add-git-hooks.sh
```

## Further Reading

Learn more about `west`
[here](https://docs.zephyrproject.org/latest/develop/west/index.html).

Learn more about `twister`
[here](https://docs.zephyrproject.org/latest/develop/test/twister.html).

For more information on creating Zephyr Testsuites, visit
[this](https://docs.zephyrproject.org/latest/develop/test/ztest.html) page.
