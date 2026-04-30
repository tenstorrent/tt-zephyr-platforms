## Branch Status

Production firmware builds and runs from this branch, but there are a few
outstanding issues that need to be cleaned up:

### Build failures in tests

The DMA tests need to be cleaned up, as they are setting Kconfigs to control
data alignment. These Kconfigs do not exist in upstream with 4.4 (they are set
with devicetree properties), so we need to remove them from the overlays

The flash tests are using deprecated macros, which need to be replaced. Check
the 4.4 migration guide (or the headers themselves) for more information
on what to switch to

### HW Stack protection fault

There is an `EV_ProtV` hardware exception being raised when running
the `dma/chan_link_transfer` testcase- this appears to be an issue with
the newer GCC compiler, but it might be an issue with blackhole itself.

The problematic instruction sequence is here, in `dw_apb_i2c.S`:

```
enter_s [r13-r19,fp,blink]
	sub_s   sp,sp,0x4
#ifdef CONFIG_NOP_HACK
	nop
#endif
	sth     r1,[fp,-4]
```

If `CONFIG_NOP_HACK` is set, the test passes. Otherwise it fails. It seems
that we can't store to the top of the stack directly after modifying the stack
pointer without an exception.

As a workaround to this, disabling `CONFIG_TEST_HW_STACK_PROTECTION` should
allow tests to run- but it would be *best* to determine why this issue is
occurring, since supporting HW stack protection is desirable.
