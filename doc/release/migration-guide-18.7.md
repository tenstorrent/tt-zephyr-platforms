# v18.7.0

This document lists recommended and required changes for those migrating from the previous v18.6.0 firmware release to the new v18.7.0 firmware release.

* RTT temporarily unavailable on SMC
  * due to the transition to using the virtual uart and `tt-console` by default for serial I/O,
    RTT support is temporarily unavailable on the default build until it is transitioned to be
    exclusively a logging backend
  * the up-coming 18.8 release will re-enable RTT as a log backend, mainly for debugging CI
  * the unavailability of RTT should have zero impact on end-users
  * end users will see an immediate benefit to using `tt-console` to have more visibility into FW
    operation, including the ability to self-diagnose issues
  * future plans for logging include:
    * enabling dynamic log level support (including the ability to enable or disable backends and
      log modules)
    * the primary log backend will be the virtual uart via `tt-console` as it provides an
      extremely high bandwidth channel for log data (benchmarks forthcoming). Although it captures
      data very early, console I/O is only available after PCIe enumerates
    * the secondary log backend will be RTT. It also captures log messages very early, and can be
      accessed before PCIe enumerates, via JTAG. After PCIe has enumerated, firmware (or possibly
      tooling) will disable the RTT log backend, as it is much lower-bandwidth and would likely
      overflow during regular operation if verbose logging is enabled.
