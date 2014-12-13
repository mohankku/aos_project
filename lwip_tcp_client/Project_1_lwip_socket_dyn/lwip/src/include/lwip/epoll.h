#ifndef __LWIP_EPOLL_H__
#define __LWIP_EPOLL_H__

#include "lwip/opt.h"

#if LWIP_NETCONN /* don't build if not configured for use in lwipopts.h */

#include <stddef.h> /* for size_t */

#include "lwip/netbuf.h"
#include "lwip/sys.h"
#include "lwip/ip_addr.h"
#include "lwip/err.h"
#include "lwip/tcpip.h"
#include "lwip/tcpip_thread.h"

enum lwip_epoll_op
{
  EPOLL_CTL_ADD = 1, 
  EPOLL_CTL_DEL = 2,
  EPOLL_CTL_MOD = 3,
};      
/*----------------------------------------------------------------------------*/
enum lwip_event_type
{
  EPOLLNONE  = 0x000, 
  EPOLLIN    = 0x001,
  EPOLLPRI   = 0x002,
  EPOLLOUT   = 0x004,
  EPOLLRDNORM        = 0x040, 
  EPOLLRDBAND        = 0x080,
  EPOLLWRNORM        = 0x100,
  EPOLLWRBAND        = 0x200,
  EPOLLMSG           = 0x400,
  EPOLLERR           = 0x008,
  EPOLLHUP           = 0x010,
  EPOLLRDHUP         = 0x2000,
  EPOLLONESHOT       = (1 << 30),
  EPOLLET            = (1 << 31)
};
/*----------------------------------------------------------------------------*/
typedef union lwip_epoll_data
{
  void *ptr;
  int sockid;
  uint32_t u32;
  uint64_t u64;
} lwip_epoll_data_t;
/*----------------------------------------------------------------------------*/
struct lwip_epoll_event
{
  uint32_t events;
  lwip_epoll_data_t data;
};

/*----------------------------------------------------------------------------*/
int
lwip_epoll_ctl(int sockid, struct lwip_epoll_event *event);
/*----------------------------------------------------------------------------*/
int
lwip_epoll_wait(int epid, struct lwip_epoll_event *events);
/*----------------------------------------------------------------------------*/

#endif /* LWIP_NETCONN */

#endif /* __LWIP_EPOLL_H__ */
