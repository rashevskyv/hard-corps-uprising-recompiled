/* libnx has no netinet/ip.h; xsocket.cpp and xam_net.cpp include it for the IP option names. */
#ifndef HCU_SWITCH_NETINET_IP_H
#define HCU_SWITCH_NETINET_IP_H

#include <netinet/in.h>

#ifndef IPTOS_LOWDELAY
#define IPTOS_LOWDELAY 0x10
#define IPTOS_THROUGHPUT 0x08
#define IPTOS_RELIABILITY 0x04
#endif

#endif
