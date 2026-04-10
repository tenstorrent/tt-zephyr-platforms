.. _tt_mimir:

TT Mimir Board
##############

Overview
********

The ``tt_mimir`` "board" is a simulation target intended to be run within
simulation/emulation environments. It represents a 4 core instance of the
Berkeley RISC-V Rocket core, with tenstorrent specific peripherals. This board
targets the Mimir Chiplet, which is the memory controller chiplet in the Grendel
Family. For instructions on running within specific simulation environments, see
the :ref:`executing_applications` section.

Hardware
********

- Single instance of the RISC-V Rocket Chip. This is a simple RISC-V
  core with a 5-stage pipeline.
- 64 KB of SRAM.
- SiFive CLINT interrupt controller
- SiFive E300 watchdog timer
- SiFive PLIC interrupt controller
- 4x Designware APB UART
- 6x Cadence I3C controllers

System Clock
************

The system clock is configured to run at 1.6GHz. The CLINT timer ticks at
800KHz, yielding a rate of 800,000 ticks per second.

.. _executing_applications:

Executing Applications
**********************

Applications can be built as usual (see :ref:`build_an_application`
for more details). See below for instructions

.. note::
    If planning to use VSCode for development, ensure it is `installed on the login servers <https://tenstorrent.atlassian.net/wiki/spaces/SoC/pages/1714880532/First+Week+at+TT+SoC+-+AI#Move-Visual-Studio-Code>`__.

Running Within Mimir Emulation Environment
==========================================

First, ssh into an SOC lab host and setup the environment:

.. code-block:: console

    # Create your own workspace
    mkdir -p /proj_soc/user_dev/$USER/
    cd /proj_soc/user_dev/$USER/

    # Clone the Grendel Emulation Repository
    git clone <grendel_emulation_url>
    cd grendelemulation/models/mimir

    # Setup the environment, these have to be done at every login
    source /tools_soc/tt/bin/bashrc
    source ./bin/setup_env.sh

    # Run the default SMC test
    zrun tests/test_smc.py -- -svk test_smc_plusargs

To run a Zephyr binary in the Mimir enumlation environment, upload the built binary stored in ``<build-dir>/zephyr/zephyr.bin``,
to the emulation system using ``scp`` or a similar tool before running.

For example, you could run the hello world application as follows:

.. code-block:: console

    # Build a binary and upload it to the emulation host
    west build -p always -b tt_mimir/tt_mimir/smc ../zephyr/samples/hello_world
    scp build/zephyr/zephyr.bin <user@emu-host>:zephyr.bin

    # Source the environment every login
    ssh <user@emu-host>
    cd /proj_soc/user_dev/$USER/grendelemulation/models/mimir
    source /tools_soc/tt/bin/bashrc
    source ./setup_env.sh

    # Run the emulation
    zrun tests/test_smc.py -- -svk test_smc_plusargs +bin_file=<zephyr.bin>

The zrun process should include output like the following:

.. code-block:: console

    TEST                         STATUS  SIM TIME (ns)  REAL TIME (s)  RATIO (ns/s)
    -------------------------------------------------------------------------------
    test_smc::test_smc_plusargs  PASSED      72,500.62           0.67    108,449.29
    -------------------------------------------------------------------------------
    TOTAL                                    72,500.62           0.67    108,449.29

Console output for the run can be found within
``zrun/test_smc-test_smc_plusargs/console-smc.log``

Running Within SMC Simulation Environment
=========================================

To run a built application within the SMC simulation environment, first build
the application as usual (see :ref:`build_an_application` for more details).

Then, you can clone and setup SMC simulation environment repository:

.. code-block:: console

    mkdir -p /proj_soc/user_dev/$USER/
    cd /proj_soc/user_dev/$USER/

    git clone git@yyz-gitlab.local.tenstorrent.com:tensix/tensix-hw/tt_smc.git
    cd tt_smc
    source /tools_soc/tt/bin/bashrc
    source bin/setup_env.sh

Copy the built Zephyr bin file to the ``tt_smc`` directory.

.. code-block:: shell

    cp <build-dir>/zephyr/zephyr.bin \
        tt_smc/firmware/zephyr/grendel_smc_hello_world_smp_zephyr/grendel_smc_hello_world_smp_zephyr.bin

Finally, run the simulation using the following command:

.. code-block:: shell

    cd tt_smc
    ttem tb_uvm/yaml/regression_smc_chiplet.yaml smc_zephyr_hello_world_smp_test \
	    --stack flist,cgen,compile_smc_chiplet,sim --wave --no-lsf --seed 1 --c compile_smc_chiplet


Running Within Renode Co-Simulation
===================================

The renode co-simulation environment should be considered experimental for
development on the ``tt_mimir`` board. The SMC simulation environment
should be treated as the "ground truth" for simulation of this board.

To run within the Renode co-simulation environment, a prebuilt co-simulation
package is available. The package is stored at the following location
on internal SOC systems. Execution should be performed on ``soc-l*`` hosts.

``/proj_syseng/user_dev/ddegrasse/public/grendel-renode-cosim-2025-12-11``

To run co-simulation, the following scripts are provided in the package:

* ``start-renode.sh``: Script to start renode with a single argument, the path to
  the ELF binary to load.

* ``start-renode-sip.sh``: Script to start renode in SIP mode. Takes two arguments,
   the path to the ELF to load on the primary SMC core, and the path to the ELF
   to load on the remote core.

The Co-Simulation environment supports both single core and SIP mode execution.
In single core execution, a single SMC is simulated. In SIP mode, two SMCs are
simulated, with their peripherals interconnected.

For details on the specific peripherals present in the simulation, see the
``release-notes.md`` file included in the package.

Co-Simulation can be run as follows:

.. code-block:: console

    west build -p always -b tt_mimir/tt_mimir/smc app/hello_world
    /proj_syseng/user_dev/ddegrasse/public/grendel-renode-cosim-2025-12-11/start-renode.sh <build-dir>/zephyr/zephyr.elf
    [INFO] Including script(s): /tmp/renode_cmds.S9DMFL
    [INFO] sysbus: Loading block of 85880 bytes length at 0xC0060000.
    [INFO] sysbus: Loading block of 1408 bytes length at 0xC0074F78.
    [INFO] hart0: Setting PC value to 0xC0060000.
    [INFO] hart1: Setting PC value to 0xC0060000.
    [INFO] hart2: Setting PC value to 0xC0060000.
    [INFO] hart3: Setting PC value to 0xC0060000.
    (tt-smc) start
    [INFO] tt-smc: Machine started.
    [INFO] [no-name]: CONSOLE: *** Booting Zephyr OS build v4.1.0-rc2 ***
    [INFO] [no-name]: CONSOLE: Hello World!
    (tt-smc) quit
    Renode is quitting
    [INFO] tt-smc: Machine paused.
    Renode connection closed
    Simulation PASSED
    $finish called from file "/proj_syseng/user_dev/ddegrasse/grendel/tt_smc/tb_uvm/sv/smc_renode_top.sv", line 445.
    $finish at simulation time 0.876000800s
               V C S   S i m u l a t i o n   R e p o r t
    Time: 876000800000000 fs
    CPU Time:     65.130 seconds;       Data structure size:  25.9Mb
    Fri Dec 12 14:12:03 2025
    [INFO] tt-smc: Disposed.
