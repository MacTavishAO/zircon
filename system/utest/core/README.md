# utest/core

The "core" tests exist for one main purpose:
To test basic functionality when things like devmgr aren't working.

If the kernel is told to run core-tests instead of devmgr, these tests
will run without any userspace device manager, device drivers, io plumbing,
etc.

## Example usage

```
fx set bringup.x64 --with-base //garnet/packages/tests:zircon  # or bringup.arm64
fx build
fx qemu -z out/default.zircon/user-x64-clang/obj/system/utest/core/core-tests.zbi
```

## Notes

The tests here are for "core" functionality (channels, etc.), but
not all "core" functionality can go here.  For example, you can't
start a process in your test with launchpad because core tests are for
when that functionality isn't working.  Core tests can't use fdio and
launchpad uses fdio.

Since these tests can't use fdio core/libc-and-io-stubs.c stubs out the needed
functions from fdio.
