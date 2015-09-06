I've released ntzc v2 GPLed version, please access http://redmine.appdeliverynetworks.com/projects/zc

About NTZC:

When receiving or sending network packet from user space on Linux, PF\_PACKET or similar interfaces are used to accomplish communication between user space application and NIC drivers, which consumes CPU load heavily. This project, Network Tapping Zero Copy (NTZC)  provides a generic method to do the communication with true zero copy capability on all the packet traversal path from NIC driver to application on Linux. A support kernel module, user space API routines and several demo NIC drivers are provided in the package. New NIC drivers are very easy to modify from original standard NIC driver in Linux kernel.


用于捕获报文的零拷贝技术有很多讨论，但迄今还没有一个可用的、开源的实现。最接近的两个实现，一个是PF\_RING，一个是NTA，前者仍然存在一次拷贝，后者则年代较为久远且问题多多。借鉴NTA的很多思路，NTZC这个项目基本原理如下

1、将连续的若干页mmap映射到用户空间；
2、内核模块自行管理这些页的内存，并且作为DMA地址交给网卡驱动收发包使用；
3、网卡驱动接收到的报文，如果需要交给用户空间时，该报文的描述符，即报文内存指针的页起始地址和相对偏移位置会被内核模块放到特定的缓冲区中，等待用户空间读取；相应的，报文内存引用计数会增加；
4、用户空间通过文件方式读取报文描述符，并计算出对应的用户空间地址，即可访问报文；在 报文生命周期结束后，通过写文件方式告诉内核空间减少引用计数，如果引用计数为1，内核模块则释放之。
5、用户空间发报文也采取类似的方式，只不过报文内存由用户空间发起命令，内核分配，再交由用户空间使用。

NTZC的设计中，认为既然用户空间可以以足够低的成本收发报文了，因此取消了网卡驱动和Linux 协议桟之间的交互，当然，从NTZC管理的报文内存中拷贝一份出来交给标准协议栈处理，也是很容易的事情。

由于优先考虑到不对内核打补丁，因此没有修改sk\_buff的内存管理机制，相反的，实现了和sk\_buff接口语义几乎相同，但内存管理机制发生变化的报文数据结构（m\_buf，只是借用了BSD里面的数据结构名称，骨子里就是对sk\_buff的复制）。使用这个m\_buf数据结构及API，需要对网卡驱动进行修改。

正因为是个通用的零拷贝支持模块，网卡驱动的修改很容易，全局替换sk\_buff的操作到m\_buf的操作即可，因此，理论上任何在Linux中已经有源代码支持的NIC Driver，都很容易被改造成NTZC的NIC驱动，只是，这块网卡将不能在被Linux协议栈使用。NTZC的代码中也给出了Intel 82575改造后的驱动作为例子。

最后，提供一个配套的用户空间API代码，以帮助应用程序方便的访问零拷贝的编程接口。对应的收包和发包示例程序也包含在内。

目前，NTZC已经进行过一些基本测试，开始趋于稳定，简单测试数据表明，收包可以做到单核普通CPU上 >1M pps报文（无论大小包）不丢包且CPU占用率极低；而发包的性能则可以做到libpcap 发包 4倍左右（特别是大包）。

现在用户空间API的定义还是一个非标准的私有接口，未来可能会考虑发展成和libpcap接口一致。

NTZC的内核模块、用户空间API和示例程序均以GPL V3授权式发布，并同时提供商业授权。

NTZC零拷贝支持多种网卡类型，请根据自己的需要灵活选择硬件网卡类型。所有未获得软件使用商业授权许可的代码，将尽力，但无法确保可以在特定网卡硬件或操作系统环境中稳定正常运行。

NTZC V2版本已经推出，该版本为二进制目标代码发布，可以提供多核运行环境下的零拷贝支持，可以联系作者获取商业授权。

欢迎感兴趣的同好访问、使用和一起完善代码。


Contact: roccen\_at\_gmail\_dot\_com

Copyright (C) 2010 roccen\_at\_gmail\_dot\_com