# kernel/phys -- Physical memory mode code for Zircon

This subdirectory contains code to support the "phys" environments for Zircon.
The [`lib/arch`] overview provides a good description of what the various
environments are and what constraints each has.

The main uses for this code are physboot, boot shims, and tests for those.
In the future it will also cover a variety of EFI cases.

[`lib/arch`]: ../lib/arch
