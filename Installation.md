# Installation #
1. Build kernel module
> cd ntzc/zc

> make

> insmod ntzc.ko

> If you need any NIC driver, please modify from kernel source by your own.
> Intel 82575 driver is included as a demo, which is modified from original download from e1000.sf.net

2. Build user demo application
> cd ntzc/nta

> make

> sudo ./sniff -i ifname