This application is a minimal Zephyr application that runs on the remote
SMC chiplet. Keeping this image small is critical to limit test time,
as simulation requires a long time to send data over I3C.

The application's sole responsibility is to report a passing
condition by writing to a scratch register on the SMC. The testbench host
monitors this register and will report the test result.
