#include "lwip/opt.h"
#include <stddef.h> /* for size_t */

#include "lwip/netbuf.h"
#include "lwip/sys.h"
#include "lwip/ip_addr.h"
#include "lwip/err.h"
#include "lwip/epoll.h"
#include "lwip/sockets.h"
#include "lwip/api.h"

int
lwip_epoll_ctl(int sockid, struct lwip_epoll_event *event)
{
  struct lwip_sock *sock;

  sock = get_socket(sockid);
  if ((!sock) && (!sock->conn)) {
    return -1;
  }

  sock->conn->use_epoll = 1;
  sock->conn->epoll_id = 0;
  event->events |= (EPOLLERR | EPOLLHUP);
  sock->conn->epoll = event->events;
  return (sock->conn->epoll_id);
}

int
lwip_epoll_wait(int epid, struct lwip_epoll_event *events)
{
  struct netconn *newconn = NULL;
  void *buff = NULL;
  struct lwip_epoll  *epoll_ptr = &lwip_tcpip_thread[sched_getcpu() % CPU_MOD]->epoll[epid];

  while (1) {
    sys_lwip_arch_mbox_check(&epoll_ptr->listener_mbox, (void **)&newconn);
    if (newconn != NULL) {
       return 0;
    }

    sys_lwip_arch_mbox_check(&epoll_ptr->other_mbox, (void **)&buff);
    if (buff != NULL) {
      events->events |= EPOLLIN;
      events->data.sockid = ((struct pbuf *)buff)->sock_id;
      return 1;
    }
  }
}
