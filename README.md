# TT-ZEPHYR-PLATFORMS 

[![CI](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/build.yml/badge.svg)](https://github.com/tenstorrent/tt-zephyr-platforms/actions/workflows/build.yml)

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

# Enable optional modules
west config manifest.group-filter -- +optional

# Fetch Zephyr modules
west update

# Apply local patches
west patch apply

# Go to the main Zephyr directory
cd zephyr
source zephyr-env.sh
```

### Build, Flash, Debug & Test BMC FW

Note: Currently, [JLink software](https://www.segger.com/downloads/jlink/) is used to flash BMC FW.
Please download and install it.

**Build, flash, and attach to the target with `west`**
```shell
# Set up a convenience variable for BMC FW
BOARD=tt_blackhole/tt_blackhole/bmc
BOARD_SANITIZED=tt_blackhole_tt_blackhole_bmc

west build --sysbuild -p -S rtt-console -b $BOARD ../$MODULE.git/app/bmc
# In a separate terminal run 'JLinkRTTClient'
# Flash mcuboot and the app
west flash -r jlink
# Attach a debugger
west attach -r jlink -d build/bmc
# at the GDB prompt, enter the following
# (gdb) monitor reset
```

Console output should appear as shown below.
```shell
*** Booting MCUboot v2.1.0-rc1-163-geb9420679895 ***
*** Using Zephyr OS build v4.0.0-1487-g595d81a941c5 ***
I: Starting bootloader
I: Primary image: magic=good, swap_type=0x2, copy_done=0x1, image_ok=0x1
I: Secondary image: magic=unset, swap_type=0x1, copy_done=0x3, image_ok=0x3
I: Boot source: none
I: Image index: 0, Swap type: none
I: Bootloader chainload address offset: 0xc000
I: Image version: v0.2.1
I: Jumping to the first image slot
*** Booting Zephyr OS build v4.0.0-1487-g595d81a941c5 ***
```

TODO: enable app debug logs

**Build and run tests on hardware with `twister`**
```shell
twister -i -p $BOARD --device-testing --west-flash \
  --device-serial-pty rtt --west-runner jlink \
  -s samples/hello_world/sample.basic.helloworld.rtt \
  -s tests/boot/test_mcuboot/bootloader.mcuboot.rtt
```

**Re-flash stable firmware**
```shell
west flash --skip-rebuild -d /opt/tenstorrent/fw/stable/$BOARD_SANITIZED
```

### Build, Flash, and Debug SMC FW

**Build, flash, and attach to the target with `west`**
```shell
# Set up a convenience variable for SMC FW
BOARD=tt_blackhole/tt_blackhole/smc

# Get the serial number of the jtag adapter
# lsusb -v ...
# SERIAL="..."

west build -p -S rtt-console -b $BOARD ../$MODULE.git/app/smc
# Flash mcuboot and the app
west flash -r openocd --cmd-pre-init "adapter serial $SERIAL"
# Attach a debugger
west attach -r openocd --cmd-pre-init "adapter serial $SERIAL"
```

TODO: enable app debug logs

TODO: run HW-in-the-loop-tests with `twister`

## Further Reading

Learn more about `west`
[here](https://docs.zephyrproject.org/latest/develop/west/index.html).

Learn more about `twister`
[here](https://docs.zephyrproject.org/latest/develop/test/twister.html).

For more information on creating Zephyr Testsuites, visit
[this](https://docs.zephyrproject.org/latest/develop/test/ztest.html) page.
