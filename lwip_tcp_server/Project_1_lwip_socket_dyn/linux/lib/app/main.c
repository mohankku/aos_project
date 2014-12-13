#include "lwip/init.h"
#include "lwip/sys.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/stats.h"
#include "lwip/sockets.h"
#include <byteswap.h>
#include "netif/ibvif.h"
#include <time.h>
#include <sys/time.h>
#include "lwip/stats.h"
#include <signal.h>
#include "lwip/sample_tracepoint.h"
#include "lwip/epoll.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <infiniband/verbs.h>

long long recvx[2];
long long conn[2];
//long long recv_sock[10240];

struct thread_struct {
   int cpu;
   int    connfd;
   struct lwip_sockaddr_in cliaddr;
};

#define MAC {0x00, 0x02, 0xc9, 0xa4, 0x58, 0xc1}
#define IB_PORT 2
#define IP_ETHER_TYPE (0x800)

#define SUCCESS (0)
#define FAILURE (1)
#define LINK_FAILURE (4)

#define MAX_SEND_SGE (1)
#define MAX_RECV_SGE (1)

static int die(const char *reason){
fprintf(stderr, "Err: %s - %s\n ", strerror(errno), reason);
exit(EXIT_FAILURE);
return -1;
}

struct ibv_comp_channel *channel;
struct ibv_context *context;
struct ibv_pd *pd;
struct ibv_exp_flow   *flow_create_result;
struct ibv_mr *send_mr;
struct ibv_mr *recv_mr;
struct ibv_cq *send_cq;
struct ibv_cq *recv_cq;
struct ibv_qp *qp;
struct ibv_send_wr swr;
char *send_buf;
char *recv_buf;


struct parameters {
    uint8_t     ib_port;
    int tx_depth;
    int rx_depth;
    int inline_size;
    uint64_t    size;
    uint64_t    buff_size;
    uint8_t     mac[6];
} user_param = {IB_PORT,0,0,0,0,0,MAC};

  struct flow_rules {
    struct ibv_exp_flow_attr attr_info;
    struct ibv_exp_flow_spec_eth spec_info;
    struct ibv_exp_flow_spec_ipv4 ip_spec_info;
    struct ibv_exp_flow_spec_tcp_udp udp_spec_info;
  } __attribute__((packed)) fr;

  struct packet {
    struct ETH_header {uint8_t dst_mac[6]; uint8_t src_mac[6]; uint16_t eth_type;}__attribute__((packed)) ethernet_header;
struct IP_V4_header{uint8_t ihl:4;uint8_t version:4;uint8_t tos;uint16_t tot_len;uint16_t id;uint16_t frag_off; uint8_t ttl; uint8_t protocol; uint16_t check;  uint32_t saddr; uint32_t daddr; }__attribute__((packed)) ip_header;
    char buff[1450];
  } eth_data;

int setup_connection_to_server () {

  eth_data.ethernet_header.dst_mac[0] = 0x00;
  eth_data.ethernet_header.dst_mac[1] = 0x02;
  eth_data.ethernet_header.dst_mac[2] = 0xc9;
  eth_data.ethernet_header.dst_mac[3] = 0xa4;
  eth_data.ethernet_header.dst_mac[4] = 0x59;
  eth_data.ethernet_header.dst_mac[5] = 0x41;

  eth_data.ethernet_header.src_mac[0] = 0x00;
  eth_data.ethernet_header.src_mac[1] = 0x02;
  eth_data.ethernet_header.src_mac[2] = 0xc9;
  eth_data.ethernet_header.src_mac[3] = 0xa4;
  eth_data.ethernet_header.src_mac[4] = 0x58;
  eth_data.ethernet_header.src_mac[5] = 0xc1;

  eth_data.ethernet_header.eth_type = htons(IP_ETHER_TYPE);

  eth_data.ip_header.ihl = 0x5;
  eth_data.ip_header.version = 0x4;
  eth_data.ip_header.tos  = 0x0;
  eth_data.ip_header.tot_len  = 0x3200;
  eth_data.ip_header.id   = 0x0;
  eth_data.ip_header.frag_off = 0x40;
  eth_data.ip_header.protocol = 0x11;
  eth_data.ip_header.check = 0xc8c6;
  eth_data.ip_header.saddr = 0x300000a;
  eth_data.ip_header.daddr = 0x100000a;

 strncpy(eth_data.buff, "hellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohello", 1450);

  long long count = 0;
  char print_buf[150];
  int total_length;
  char temp_buf[1600];
  int rc;

  /* init default values to user's parameters */
  user_param.tx_depth = 5000;
  user_param.rx_depth = 5000;
  user_param.inline_size = 0;
  //user_param.size = 1518; // NOTE - try 64 byte aligned // FIXME???
  user_param.size = 1536;       // NOTE - try 64 byte aligned
  user_param.buff_size = (user_param.rx_depth + 1) * user_param.size;

  int i, j, ne, num_of_device = 0;
  struct ibv_device **ib_dev_list = ibv_get_device_list(&num_of_device);
  if (num_of_device <= 0 || !ib_dev_list || !ib_dev_list[1]) {die("Unable to find the Infiniband/RoCE device\n"); return FAILURE;}
  context = ibv_open_device(ib_dev_list[1]);
  if (!context) {die("Couldn't get context for the device\n"); return FAILURE;}
  ibv_free_device_list(ib_dev_list);

  //channel = ibv_create_comp_channel(context);

  // Creating all the basic IB resources (data buffer, PD, MR, CQ and QP)
  //
  int flags = IBV_ACCESS_LOCAL_WRITE;
  send_buf = malloc(user_param.buff_size);
  recv_buf = malloc(user_param.buff_size);
  pd = ibv_alloc_pd(context);
  if (!pd) {die("Couldn't allocate PD\n"); return FAILURE;}

  // Allocating Memory region and assigning our buffer to it.
  send_mr = ibv_reg_mr(pd, send_buf, user_param.buff_size, flags);       // buff_size = 8192, flags = 33
  recv_mr = ibv_reg_mr(pd, recv_buf, user_param.buff_size, flags);       // buff_size = 8192, flags = 33
  if (!send_mr || !recv_mr) {die("Couldn't allocate MR\n"); return FAILURE;}

  // Creating CQs.
  send_cq = ibv_create_cq(context, user_param.tx_depth, NULL, NULL , 0); // tx_de
  if (!send_cq) {die("Couldn't create CQ\n"); return FAILURE;}
  recv_cq = ibv_create_cq(context, user_param.rx_depth, NULL, NULL, 0);
  if (!recv_cq) {die("Couldn't create a receiver CQ\n");return FAILURE;}

  ibv_req_notify_cq(recv_cq, 0);
  struct ibv_qp_init_attr attr;
  memset(&attr, 0, sizeof(struct ibv_qp_init_attr));
  attr.send_cq = send_cq;
  attr.recv_cq = recv_cq;
  attr.cap.max_send_wr = user_param.tx_depth;   // 128
  attr.cap.max_send_sge = MAX_SEND_SGE;
  attr.cap.max_inline_data = user_param.inline_size;
  attr.srq = NULL;
  attr.cap.max_recv_wr = user_param.rx_depth;
  attr.cap.max_recv_sge = MAX_RECV_SGE;
  attr.qp_type = IBV_QPT_RAW_PACKET;

  qp = ibv_create_qp(pd, &attr);;
  if (qp == NULL) {die("Unable to create QP.\n"); return FAILURE;}

  // Modyfying QP to init
  {
  int flags = IBV_QP_STATE | IBV_QP_PORT;
  struct ibv_qp_attr attr;
  memset(&attr, 0, sizeof(struct ibv_qp_attr));
  attr.qp_state = IBV_QPS_INIT;
  attr.pkey_index = 0;
  attr.port_num = user_param.ib_port;
  if (ibv_modify_qp(qp, &attr, flags)) {die("Failed to modify QP to INIT\n"); return FAILURE;}
  }

  // Attaching the qp to the spec

  memset(&fr.attr_info, 0 , sizeof(struct ibv_exp_flow_attr));
  fr.attr_info.type = IBV_EXP_FLOW_ATTR_NORMAL;
  fr.attr_info.size = sizeof(struct flow_rules);        // 60
  fr.attr_info.priority = 0;
  fr.attr_info.num_of_specs = 1;        // + is_ip + is_port
  fr.attr_info.port = user_param.ib_port;
  fr.attr_info.flags = 0;

  memset(&fr.spec_info, 0 , sizeof(struct ibv_exp_flow_spec_eth));
  fr.spec_info.type = IBV_EXP_FLOW_SPEC_ETH;
  fr.spec_info.size = sizeof(struct ibv_exp_flow_spec_eth);
  fr.spec_info.val.ether_type = IP_ETHER_TYPE;
  fr.spec_info.mask.ether_type = 0xffff;
  memcpy(fr.spec_info.val.dst_mac, user_param.mac, sizeof(fr.spec_info.mask.dst_mac));
  memset(fr.spec_info.mask.dst_mac, 0xff, sizeof(fr.spec_info.mask.dst_mac));
  flow_create_result = NULL;
  flow_create_result = ibv_exp_create_flow(qp, &fr.attr_info);

  if (!flow_create_result) {die("Couldn't attach QP\n");return FAILURE;}


  // Modyfying QP to rtr
  {
  struct ibv_qp_attr attr;
  memset(&attr, 0, sizeof(struct ibv_qp_attr));
  int flags = IBV_QP_STATE;
  attr.qp_state = IBV_QPS_RTR;
  attr.ah_attr.src_path_bits = 0;
  attr.ah_attr.port_num = user_param.ib_port;
  ibv_modify_qp(qp, &attr, flags);
  }

  {
  struct ibv_qp_attr attr;
  memset(&attr, 0, sizeof(struct ibv_qp_attr));
  int flags = IBV_QP_STATE;
  attr.qp_state = IBV_QPS_RTS;
  attr.ah_attr.src_path_bits = 0;
  attr.ah_attr.port_num = user_param.ib_port;
  ibv_modify_qp(qp, &attr, flags);
  }

  // Set receive WQES
  struct ibv_sge recv_sge;
  memset(&recv_sge, 0, sizeof(struct ibv_sge));
  recv_sge.addr = (uintptr_t)recv_buf;  // (uintptr_t)ctx->buf + (num_of_qps + k)*BUFF_SIZE(ctx->size);
  recv_sge.length = user_param.size;
  recv_sge.lkey = recv_mr->lkey;

  struct ibv_recv_wr rwr;
  memset(&rwr, 0, sizeof(struct ibv_recv_wr));
  rwr.sg_list = &recv_sge;
  rwr.wr_id = 0;        // user id - 64 bit, TODO ad PTR
  rwr.next = NULL;
  rwr.num_sge = 1;

  // set send WQES
  struct ibv_sge send_sge;
  memset(&send_sge, 0, sizeof(struct ibv_sge));
  send_sge.addr = (uintptr_t)send_buf;  // (uintptr_t)ctx->buf + (num_of_qps + k)*BUFF_SIZE(ctx->size);
  send_sge.length = user_param.size;
  send_sge.lkey = send_mr->lkey;

  memset(&swr, 0, sizeof(struct ibv_send_wr));
  swr.sg_list = &send_sge;
  swr.wr_id = send_sge.addr;    // user id - 64 bit, TODO ad PTR
  swr.next = NULL;
  swr.num_sge = 1;
  swr.opcode  = IBV_WR_SEND;
  swr.send_flags = IBV_SEND_SIGNALED;
  /*for (i = 0; i < user_param.rx_depth ; ++i) {
    struct ibv_recv_wr *bad_wr_recv = NULL;
    rwr.wr_id = recv_sge.addr;
    if (ibv_post_recv(qp, &rwr, &bad_wr_recv)) {die("Couldn't post recv Qp.\n");return FAILURE;}
    recv_sge.addr += user_param.size;
  }*/

  memcpy(send_buf, &eth_data, sizeof(eth_data));
  send_sge.length = sizeof(eth_data);
  return 1;
}

void *
handle_listener (void *arg) {
  int cpu = *(int *) arg;
  int listenfd, connfd;
  struct lwip_sockaddr_in servaddr, cliaddr;
  socklen_t clilen;
  int index;
  u32_t     opt, ret,n;
 struct lwip_epoll_event event;
 char mesg[1600];

  lwip_thread_aff(cpu);
  pthread_yield();

  listenfd = lwip_socket(LWIP_AF_INET,LWIP_SOCK_STREAM,0);
  opt = lwip_fcntl(listenfd, F_GETFL, 0);
  opt |= O_NONBLOCK;
  ret = lwip_fcntl(listenfd, F_SETFL, opt);
  memset(&servaddr,0,sizeof(struct lwip_sockaddr_in));
  servaddr.sin_family = LWIP_AF_INET;
  servaddr.sin_addr.s_addr=inet_addr("10.0.0.1");
  servaddr.sin_port=bswap_16(80);
  lwip_bind(listenfd, (struct lwip_sockaddr *)&servaddr, sizeof(servaddr));
  lwip_listen(listenfd, 1024);
  event.events |= EPOLLIN;
  lwip_epoll_ctl(listenfd, &event);
  event.events = 0;

  while(1) {
    lwip_epoll_wait(0, &event);
    if (event.events & EPOLLIN) {
      /* Handle incoming packet here */
      n = lwip_recvfrom(event.data.sockid,mesg,1450,0,NULL,NULL);
      if ((n==0) || (n == -1)) {
        printf("recv error %d\n", n);
        break;
      }
      recvx[1]++;
      //lwip_sendto(event.data.sockid,mesg,n,0,NULL,0);
    } else {
      /* Handle new connections here */
      connfd = lwip_accept(listenfd,(struct lwip_sockaddr *)&cliaddr,&clilen);
      if (connfd == -1) {
        printf("recv error %d\n", n);
        break;
      }
      opt = lwip_fcntl(connfd, F_GETFL, 0);
      opt |= O_NONBLOCK;
      ret = lwip_fcntl(connfd, F_SETFL, opt);
      event.events |= EPOLLIN;
      lwip_epoll_ctl(connfd, &event);
      conn[1]++;
    }
    event.events = 0;
  }
  return 1;
}

void print_stats (int sig)
{
   int i;
   printf ("\n%lld \n %lld\n", recvx[0], recvx[1]);
   printf ("\n%lld \n %lld\n", conn[0], conn[1]);
   /*for (i=0; i<10001; i++) {
     printf ("\n%lld", recv_sock[i]);
   }*/
   exit(1);
}

int main(int argc, char **argv) {
 int listenfd, connfd;
 struct lwip_sockaddr_in servaddr, cliaddr;
 socklen_t clilen;
 u32_t     opt, ret;
 signed int n;
 struct lwip_epoll_event event;
 char mesg[1600];
 int cpu[NUM_CPU], i;
 pthread_t tmp;
 int sockfd;
 struct sockaddr_in saddr;
 int flags;

 struct ibv_wc wc[5000];
 struct ibv_send_wr *bad_wr_send = NULL;
 struct timespec start, stop;
 int sne = 0;

 /*sockfd = socket(AF_INET,SOCK_DGRAM,0);
 saddr.sin_family = AF_INET;
 saddr.sin_addr.s_addr=inet_addr("10.0.0.4");
 saddr.sin_port=htons(32000);
 fcntl(sockfd, F_SETFL, O_NONBLOCK);*/

 lwip_thread_aff(18);
 tcpip_init(NULL, NULL);
 signal(SIGINT, print_stats);
 recvx[0] = 0;
 recvx[1] = 0;

 setup_connection_to_server();

 /*for (i=19; i<24; i++) {
   cpu[i%18] = i;
   pthread_create(&tmp, NULL, (void *(*)(void *)) handle_listener, (void *) &cpu[i%18]);
 }*/

 listenfd = lwip_socket(LWIP_AF_INET,LWIP_SOCK_STREAM,0);
 opt = lwip_fcntl(listenfd, F_GETFL, 0);
 opt |= O_NONBLOCK;
 ret = lwip_fcntl(listenfd, F_SETFL, opt);

 memset(&servaddr,0,sizeof(struct lwip_sockaddr_in));
 servaddr.sin_family = LWIP_AF_INET;
 servaddr.sin_addr.s_addr=inet_addr(argv[1]);
 servaddr.sin_port=bswap_16(80);
 lwip_bind(listenfd, (struct lwip_sockaddr *)&servaddr, sizeof(servaddr));
 lwip_listen(listenfd, 1024);
 event.events |= EPOLLIN;
 lwip_epoll_ctl(listenfd, &event);
 event.events = 0;

 while(1) {
   lwip_epoll_wait(0, &event);
   if (event.events & EPOLLIN) {
     /* Handle incoming packet here */
     n = lwip_recvfrom(event.data.sockid,mesg,1450,0,NULL,NULL);
     if ((n==0) || (n == -1)) {
       printf("recv error %d\n", n);
       break;
     }
     if (ibv_post_send(qp, &swr, &bad_wr_send)) {die("couldn't post send QP\n"); return FAILURE;}
     sne = ibv_poll_cq(send_cq, user_param.tx_depth, wc);

    for (i=0; i < sne; i++) {
      if (wc[i].status != IBV_WC_SUCCESS) {die("Couldn't send, status is unsuccessful.\n");return FAILURE;}
    }
    /*if ((recvx[0] % 10) == 0) {
      sendto(sockfd,mesg,n,0, (struct sockaddr *)&saddr,sizeof(saddr));
    }*/
     recvx[0]++;
     //recv_sock[event.data.sockid]++;
     //lwip_sendto(event.data.sockid,mesg,n,0,NULL,0);
   } else {
     /* Handle new connections here */
     connfd = lwip_accept(listenfd,(struct lwip_sockaddr *)&cliaddr,&clilen);
     if (connfd == -1) {
       printf("recv error %d\n", n);
       break;
     }
     opt = lwip_fcntl(connfd, F_GETFL, 0);
     opt |= O_NONBLOCK;
     ret = lwip_fcntl(connfd, F_SETFL, opt);
     event.events |= EPOLLIN;
     lwip_epoll_ctl(connfd, &event);
     conn[0]++;
   }
   event.events = 0;
 }
 return 1;
}
