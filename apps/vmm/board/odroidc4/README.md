# Odroid-C4 images

LOOK at wiki for new instructions.
https://cellulosdocs.readthedocs.io/en/cellulos/development/virtual_machine_monitor.html


## Linux kernel

### Details
* Image name: `linux`
* Config name: `linux_config`
* Git remote: https://github.com/torvalds/linux.git
* Tag: v6.1 (commit hash: `830b3c68c1fb1e9176028d02ef86f3cf76aa2476`)
* Toolchain: `aarch64-none-elf`
    * Version: GNU Toolchain for the A-profile Architecture 10.2-2020.11 (arm-10.16)) 10.2.1 20201103

You can also get the Linux config used after booting by running the following
command in userspace: `zcat /proc/config.gz`.

### Instructions for reproducing

```bash
git clone --depth 1 --branch v6.1 https://github.com/torvalds/linux.git linux-clone-osmosis
make -C linux-clone-osmosis defconfig
cp linux_config linux-clone-osmosis/.config
make -C linux-clone-osmosis ARCH=arm64 CROSS_COMPILE=aarch64-none-elf- all -j$(nproc)
```

The path to the image will be: `linux/arm64/boot/Image`.

## Buildroot RootFS image

Note that buildoot currently does not list a configuration for OdroidC4 so we just
use the OdroidC2 configuration.
