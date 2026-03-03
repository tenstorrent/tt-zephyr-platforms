This test validates that we can load and run a simple application on the
remote SMC chiplet via I3C. It tests the OCCP subsystem, as well as the
I3C driver.

The remote application boots to main, which will indicate passing
conditions by writing to a scratch register on the SMC. The testbench
host monitors this register and will report the test result.
