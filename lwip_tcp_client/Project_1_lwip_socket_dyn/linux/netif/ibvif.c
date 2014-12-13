/*
 * Author: Mohan 
 *
 */
#include "lwip/stats.h"
#include "netif/ibvif.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include "lwip/debug.h"
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/ip.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/tcpip.h"

#include "netif/etharp.h"
#include <infiniband/verbs.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <byteswap.h>
#include <time.h>
#include <sys/time.h>
#include "lwip/sample_tracepoint.h"

#if defined(LWIP_DEBUG) && defined(LWIP_TCPDUMP)
#include "netif/tcpdump.h"
#endif /* LWIP_DEBUG && LWIP_TCPDUMP */

#define IFNAME0 'i'
#define IFNAME1 'b'
#define IP_ETHER_TYPE (0x800)
#define LINK_FAILURE (4)

struct ibvif *ibvif[NUM_CPU];

/* Rules needed for flow steering */
typedef struct flow_rules {
  struct ibv_exp_flow_attr attr_info;
  struct ibv_exp_flow_spec_eth spec_info;
  struct ibv_exp_flow_spec_ipv4 ip_spec_info;
  struct ibv_exp_flow_spec_tcp_udp tcp_spec_info;
} __attribute__((packed)) fr_attr;

void ibv_attach_device (struct netif *netif);

static uint8_t set_link_layer(struct ibv_context *context, uint8_t ib_port) {
  struct ibv_port_attr port_attr;

  if (ibv_query_port(context,ib_port,&port_attr)) {
    perror("Unable to query port attributes\n");
    return LINK_FAILURE;
  }
  if (port_attr.state != IBV_PORT_ACTIVE) {
    perror("Port number invalid state \n");
    return LINK_FAILURE;
  }
  if (port_attr.link_layer != IBV_LINK_LAYER_ETHERNET) {
    perror("Unable to determine link layer \n"); return LINK_FAILURE;
  }
  return port_attr.link_layer;
}

#define HUGE_PAGE_SIZE (2 * 1024 * 1024)
#define ALIGN_TO_PAGE_SIZE(x) \
(((x) + HUGE_PAGE_SIZE - 1) / HUGE_PAGE_SIZE * HUGE_PAGE_SIZE)

int ibv_buffer(struct ibvif *m) {

  size_t sz_bytes = ALIGN_TO_PAGE_SIZE(m->buf_size + HUGE_PAGE_SIZE);

  m->recv_shmid = shmget(IPC_PRIVATE, sz_bytes,
                     SHM_HUGETLB | IPC_CREAT | SHM_R | SHM_W);

  if (m->recv_shmid < 0) {
    perror("shmget failed !!!");
    return 0;
  }

  m->send_buf = (char *)shmat(m->recv_shmid, NULL, 0);
  if (m->send_buf == (void*)-1) {
    shmctl(m->recv_shmid, IPC_RMID, NULL);
    m->recv_shmid = -1;
    m->send_buf = NULL;
    perror("shmat failed !!!");
    return 0;
  }

  if (shmctl(m->recv_shmid, IPC_RMID, NULL)) {
    perror("shared memory marking to be destroyed - FAILED");
  }

  m->buf_size = sz_bytes;
  return 1;
}

static void infini_post_recv(struct ibvif *m)
{
  struct ibv_sge list;
  struct ibv_recv_wr wr;
  int i;
  struct ibv_recv_wr *bad_wr_recv;

  memset(&list, 0, sizeof(struct ibv_sge));
  list.addr   = (uintptr_t) m->recv_buf;
  list.length = m->send_size;
  list.lkey   = m->recv_mr->lkey;

  memset(&wr, 0, sizeof(struct ibv_recv_wr));
  wr.wr_id   = list.addr;
  wr.sg_list = &list;
  wr.num_sge = 1;
  wr.next    = NULL;

  for (i = 0; i < m->rx_depth ; ++i) {
    bad_wr_recv = NULL;
    wr.wr_id = list.addr;
    ibv_post_recv(m->qp, &wr, &bad_wr_recv);
    if (i+1 < m->rx_depth)
      list.addr += m->send_size;
  }
}

static void infini_post_recv_buf(struct tcpip_thread *t, struct pbuf *p)
{
  struct ibv_sge list;
  struct ibv_recv_wr wr;
  struct ibv_recv_wr *bad_wr_recv;
  struct ibvif *m;

  m = (struct ibvif *)t->netif.state;

  memset(&list, 0, sizeof(struct ibv_sge));
  list.addr   = (uintptr_t) p->actual_payload;
  list.length = m->send_size;
  list.lkey   = m->recv_mr->lkey;

  memset(&wr, 0, sizeof(struct ibv_recv_wr));
  wr.wr_id   = list.addr;
  wr.sg_list = &list;
  wr.num_sge = 1;
  wr.next    = NULL;

  ibv_post_recv(m->qp, &wr, &bad_wr_recv);
}

static int infini_post_send(struct ibvif *m, void *payload, int length)
{
  struct ibv_sge list;
  struct ibv_exp_send_wr wr;
  struct ibv_exp_send_wr *bad_wr = NULL;

  memset(&list, 0, sizeof(struct ibv_sge));
  list.addr   = (uintptr_t) payload;
  list.length = length;
  list.lkey   = m->send_mr->lkey;

  memset(&wr, 0, sizeof(struct ibv_send_wr));
  wr.wr_id      = list.addr;
  wr.sg_list    = &list;
  wr.num_sge    = 1;
  wr.exp_opcode = IBV_EXP_WR_SEND;
  wr.exp_send_flags = IBV_EXP_SEND_SIGNALED | IBV_EXP_SEND_IP_CSUM;
  wr.next       = NULL;

  return ibv_exp_post_send(m->qp, &wr, &bad_wr);
}

static err_t ibvif_thread(struct netif *netif);

/*-----------------------------------------------------------------------------------*/
static void
low_level_init(struct netif *netif)
{
  struct ibvif *ibvif;
  int num_of_device, flags = IBV_ACCESS_LOCAL_WRITE;
  struct ibv_qp_init_attr attr;
  struct ibv_qp_attr qp_attr;
  uint8_t port_num = 1;
  int    qp_flags;
  struct ibv_device **ib_dev_list;
  struct tcpip_thread *thread;
  struct ibv_exp_cq_init_attr cq_attr;

  ibvif = (struct ibvif *)netif->state;

  /* Obtain MAC address from network interface. */
  ibvif->ethaddr->addr[0] = 0x00;
  ibvif->ethaddr->addr[1] = 0x02;
  ibvif->ethaddr->addr[2] = 0xc9;
  ibvif->ethaddr->addr[3] = 0xa4;
  ibvif->ethaddr->addr[4] = 0x58;
  ibvif->ethaddr->addr[5] = 0xc1;

  ibvif->buf_size = PBUF_POOL_SIZE * TCP_MAX_PACKET_SIZE;

  /* Do things needed for using Raw Packet Verbs */

  ib_dev_list = ibv_get_device_list(&num_of_device);
  if (num_of_device <= 0 || !ib_dev_list || !ib_dev_list[0]) {
    perror("IBV no device found\n");
    exit(1);
  }

  ibvif->context = ibv_open_device(ib_dev_list[1]);
  if (!ibvif->context) {
    perror("IBV can't open device\n");
    exit(1);
  }

  ibv_free_device_list(ib_dev_list);

  if (set_link_layer(ibvif->context, 1) == LINK_FAILURE) {
    perror("IBV can't allocate PD\n");
    exit(1); 
  }

  ibvif->pd = ibv_alloc_pd(ibvif->context);
  if (!ibvif->pd) {
    perror("IBV can't allocate PD\n");
    exit(1);
  }

  /*if (!ibv_buffer(ibvif)) {
    LWIP_DEBUGF(NETIF_DEBUG, ("Buffer allocation failed\n"));
    exit(1);
  }*/

  ibvif->recv_buf     = netif->prot_thread->pbuf_rx_handle.buf;
  ibvif->send_buf     = netif->prot_thread->pbuf_tx_handle.buf;
  ibvif->send_size    = TCP_MAX_PACKET_SIZE;
  ibvif->rx_depth     = PBUF_POOL_SIZE;
  ibvif->tx_depth     = PBUF_POOL_SIZE;

  ibvif->send_mr = ibv_reg_mr(ibvif->pd, ibvif->send_buf, ibvif->buf_size, flags);
  if (!ibvif->send_mr) {
    perror("IBV error reg send mr\n");
    exit(1);
  }

  ibvif->recv_mr = ibv_reg_mr(ibvif->pd, ibvif->recv_buf, ibvif->buf_size, flags);
  if (!ibvif->recv_mr) {
    perror("IBV error reg recv mr\n");
    exit(1);
  }

  ibvif->send_cq = ibv_create_cq(ibvif->context, ibvif->tx_depth, NULL, NULL, 0);
  if (!ibvif->send_cq) {
    perror("IBV can't create send cq\n");
    exit(1);
  }

  cq_attr.flags = IBV_EXP_CQ_TIMESTAMP;
  cq_attr.comp_mask = IBV_EXP_CQ_INIT_ATTR_FLAGS;
  ibvif->recv_cq = ibv_exp_create_cq(ibvif->context, ibvif->rx_depth, NULL, NULL, 0, &cq_attr);
  if (!ibvif->recv_cq) {
    perror("IBV can't create recv cq\n");
    exit(1);
  }

  memset(&attr, 0, sizeof(struct ibv_qp_init_attr));
  attr.send_cq = ibvif->send_cq;
  attr.recv_cq = ibvif->recv_cq;
  attr.cap.max_send_wr = ibvif->tx_depth;
  attr.cap.max_send_sge = 1;
  attr.cap.max_recv_wr = ibvif->rx_depth;
  attr.cap.max_recv_sge = 1;
  attr.qp_type = IBV_QPT_RAW_PACKET;

  ibvif->qp = ibv_create_qp(ibvif->pd, &attr);
  if (!ibvif->qp) {
    perror("IBV can't create QP\n");
    exit(1);
  }

  qp_flags = IBV_QP_STATE | IBV_QP_PORT;
  memset(&qp_attr, 0, sizeof(struct ibv_qp_attr));
  qp_attr.qp_state = IBV_QPS_INIT;
  qp_attr.pkey_index = 0;
  qp_attr.port_num = port_num;
  qp_attr.qp_access_flags = 0;

  if (ibv_modify_qp(ibvif->qp, &qp_attr, qp_flags)) {
    perror("IBV can't set qp to init\n");
    exit(1);
  }
  ibv_attach_device(netif);
}

void ibv_attach_device (struct netif *netif)
{
  struct ibvif *ibvif;
  uint8_t mac[6] = {0x00, 0x02, 0xc9, 0xa4, 0x58, 0xc1};
  struct ibv_qp_attr qp_attr;
  int    qp_flags;
  uint8_t port_num = 1;
  fr_attr fr;
  int start, end, index, i; 

  ibvif = (struct ibvif *)netif->state;
  
  /* Attaching the qp to the spec */
  memset(&fr.attr_info, 0 , sizeof(struct ibv_exp_flow_attr));
  fr.attr_info.type = IBV_EXP_FLOW_ATTR_NORMAL;
  fr.attr_info.size = sizeof(struct flow_rules);
  fr.attr_info.priority = 0;
  fr.attr_info.num_of_specs = 3;
  fr.attr_info.port = port_num;
  fr.attr_info.flags = 0;

  memset(&fr.spec_info, 0 , sizeof(struct ibv_exp_flow_spec_eth));
  fr.spec_info.type = IBV_EXP_FLOW_SPEC_ETH;
  fr.spec_info.size = sizeof(struct ibv_exp_flow_spec_eth);
  fr.spec_info.val.ether_type = IP_ETHER_TYPE;
  fr.spec_info.mask.ether_type = 0xffff;
  memcpy(fr.spec_info.val.dst_mac, mac, sizeof(fr.spec_info.mask.dst_mac));
  memset(fr.spec_info.mask.dst_mac, 0xff, sizeof(fr.spec_info.mask.dst_mac));

  memset(&fr.ip_spec_info, 0 , sizeof(struct ibv_exp_flow_spec_ipv4));
  fr.ip_spec_info.type = IBV_EXP_FLOW_SPEC_IPV4;
  fr.ip_spec_info.size = sizeof(struct ibv_exp_flow_spec_ipv4);
  if (netif->prot_thread->cpu == 6) {
    fr.ip_spec_info.val.dst_ip = inet_addr("10.0.0.3");
  } else if (netif->prot_thread->cpu == 7) {
    fr.ip_spec_info.val.dst_ip = inet_addr("10.0.0.5");
  } else if (netif->prot_thread->cpu == 8) {
    fr.ip_spec_info.val.dst_ip = inet_addr("10.0.0.6");
  } else if (netif->prot_thread->cpu == 9) {
    fr.ip_spec_info.val.dst_ip = inet_addr("10.0.0.7");
  } else if (netif->prot_thread->cpu == 10) {
    fr.ip_spec_info.val.dst_ip = inet_addr("10.0.0.8");
  } else if (netif->prot_thread->cpu == 11) {
    fr.ip_spec_info.val.dst_ip = inet_addr("10.0.0.9");
  }

  fr.ip_spec_info.mask.dst_ip = 0xffffffff;

  memset(&fr.tcp_spec_info, 0 , sizeof(struct ibv_exp_flow_spec_tcp_udp));
  fr.tcp_spec_info.type = IBV_EXP_FLOW_SPEC_TCP;
  fr.tcp_spec_info.size = sizeof(struct ibv_exp_flow_spec_tcp_udp);
  fr.tcp_spec_info.val.src_port = bswap_16(80);
  fr.tcp_spec_info.mask.src_port = 0xffff;

  ibvif->flow = ibv_exp_create_flow(ibvif->qp, &fr.attr_info);
  if (!ibvif->flow) {
    perror("IBV can't create flow\n");
    exit(1);
  }

  /*start =  netif->prot_thread->cpu * 100;
  end = start + 100;
  index = 0;

  for (i=start; i<end; i++) { 
    fr.tcp_spec_info.val.src_port = htons(i);
    fr.tcp_spec_info.mask.src_port = 0xffff;

    ibvif->flow[index] = ibv_exp_create_flow(ibvif->qp, &fr.attr_info);
    if (!ibvif->flow[index]) {
      perror("IBV can't create flow\n");
      exit(1);
    }
    index++;
  }*/

  /* modify QP to send and receive */

  qp_flags = IBV_QP_STATE | IBV_QP_AV;
  //qp_flags = IBV_QP_STATE;
  memset(&qp_attr, 0, sizeof(struct ibv_qp_attr));
  qp_attr.qp_state = IBV_QPS_RTR;
  qp_attr.ah_attr.src_path_bits = 0;
  qp_attr.ah_attr.port_num = 1;
  qp_attr.ah_attr.is_global  = 0;
  qp_attr.ah_attr.sl = 1;
  if (ibv_modify_qp(ibvif->qp, &qp_attr, qp_flags)) {
    perror("IBV can't set state to RTR\n");
    exit(1);
  }

  qp_flags = IBV_QP_STATE;
  memset(&qp_attr, 0, sizeof(struct ibv_qp_attr));
  qp_attr.qp_state = IBV_QPS_RTS;
  qp_attr.ah_attr.src_path_bits = 0;
  qp_attr.ah_attr.port_num = 1;
  if (ibv_modify_qp(ibvif->qp, &qp_attr, qp_flags)) {
    perror("IBV can't set state to RTS\n");
    exit(1);
  }

  infini_post_recv(ibvif);

  netif->flags =  NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;

}
/*-----------------------------------------------------------------------------------*/
/*
 * low_level_output():
 *
 * Should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 */
/*-----------------------------------------------------------------------------------*/

static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
  struct ibvif *ibvif;
  struct pbuf *q;
  char *bufptr;

  ibvif = netif->state;

  /* signal that packet should be sent(); */
  if (infini_post_send(ibvif, p->payload, p->tot_len)) {
    perror("tapif: write 1");
  }

  //tracepoint(sample_tracepoint, message, "IBVIF sent message\n");

  return ERR_OK;
}
/*-----------------------------------------------------------------------------------*/
/*
 * low_level_input():
 *
 * Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 * This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface.
 *
 *
 */
/*-----------------------------------------------------------------------------------*/
static void 
low_level_input(struct netif *netif)
{
  struct pbuf *p;
  u16_t len;
  char *bufptr;
  struct ibv_exp_wc wc[PBUF_READ_DEPTH];
  u16_t ne, i;
  struct ibvif *ibvif;
  struct timespec start;

  ibvif = (struct ibvif *)netif->state;

  /* Obtain the size of the packet and put it into the "len"
   * variable. */
  ne = ibv_exp_poll_cq(ibvif->recv_cq, PBUF_READ_DEPTH, wc, sizeof(struct ibv_exp_wc));

  if (ne == 0)
    return;

  //tracepoint(sample_tracepoint, message, "IBVIF received message\n");

  for (i=0; i<ne; i++) {
    len = wc[i].byte_len;

    /* We allocate a pbuf chain of pbufs from the pool. */
    p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL_RX, netif->prot_thread);

    if (p != NULL) {
      /* We iterate over the pbuf chain until we have read the entire
       * packet into the pbuf. */
      bufptr = (char *) wc[i].wr_id;
      p->actual_payload = p->payload = bufptr;
      p->custom_free_function = infini_post_recv_buf;

      //tracepoint(sample_tracepoint, message, "IBVIF sent to tcp\n");

      if (netif->input(p, netif) != ERR_OK) {
         LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
         pbuf_free(p, netif->prot_thread);
         p = NULL;
      }
    } else {
      perror("Could not allocate pbufs\n");
    }
  }
}
/*-----------------------------------------------------------------------------------*/
static err_t
ibvif_thread(struct netif *netif)
{
  struct ibvif *ibvif;
  int ne, i;
  struct ibv_wc wc[PBUF_READ_DEPTH];

  ibvif = (struct ibvif *)netif->state;

  netif->prot_thread->init_done = 1;

  ne = ibv_poll_cq(ibvif->send_cq, PBUF_READ_DEPTH, wc);

  for (i=0; i<ne; i++) {
    if (wc[i].status != IBV_WC_SUCCESS) {
      perror("tapif: write 2");
    }
  }

  /* Wait for a packet to arrive. */
  low_level_input(netif);
}
/*-----------------------------------------------------------------------------------*/
/*
 * ibvif_init():
 *
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 */
/*-----------------------------------------------------------------------------------*/
err_t
ibvif_init(struct netif *netif)
{
  struct ibvif *ibvif_temp;

  ibvif_temp = (struct ibvif *)malloc(sizeof(struct ibvif));
  if (!ibvif_temp) {
    return ERR_MEM;
  }
  memset(ibvif_temp, 0, sizeof(struct ibvif));

  ibvif_temp->thread = netif->prot_thread;
  netif->state = ibvif_temp;
  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
  netif->output = etharp_output;
  netif->linkoutput = low_level_output;
  netif->linkinput = ibvif_thread;
  netif->mtu = 1500;
  /* hardware address length */
  netif->hwaddr_len = 6;

  ibvif_temp->ethaddr = (struct eth_addr *)&(netif->hwaddr[0]);

  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;

  low_level_init(netif);

  return ERR_OK;
}
/*-----------------------------------------------------------------------------------*/
