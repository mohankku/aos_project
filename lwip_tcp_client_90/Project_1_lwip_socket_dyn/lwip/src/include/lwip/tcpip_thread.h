/* This contains the per thread data structures needed for TCPIP
 * Thread. This will contain all the memory pools, PCB structures,
 * IP and ARP structures */

#ifndef __LWIP_TCPIP_THREAD_H__
#define __LWIP_TCPIP_THREAD_H__

#include "lwip/opt.h"
#include "lwip/sys.h"
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

struct tcpip_thread;
struct netif;
struct tcp_pcb;

/** Function prototype for tcp accept callback functions. Called when a new
 * connection can be accepted on a listening pcb.
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param newpcb The new connection pcb
 * @param err An error code if there has been an error accepting.
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 */
typedef err_t (*tcp_accept_fn)(void *arg, struct tcp_pcb *newpcb, err_t err);

#if LWIP_NETIF_HWADDRHINT
#define IP_PCB_ADDRHINT ;u8_t addr_hint
#else
#define IP_PCB_ADDRHINT
#endif /* LWIP_NETIF_HWADDRHINT */

/* This is the common part of all PCB types. It needs to be at the
 * beginning of a PCB type definition. It is located here so that
 * changes to this common part are made in one location instead of
 * having to change all PCB structs. */
#define IP_PCB \
  /* ip addresses in network byte order */ \
  ip_addr_t local_ip; \
  ip_addr_t remote_ip; \
   /* Socket options */  \
  u8_t so_options;      \
   /* Type Of Service */ \
  u8_t tos;              \
  /* Time To Live */     \
  u8_t ttl               \
  /* link layer address resolution hint */ \
  IP_PCB_ADDRHINT

struct memp {
  struct memp *next;
};

enum tcp_state {
  CLOSED      = 0,
  LISTEN      = 1,
  SYN_SENT    = 2,
  SYN_RCVD    = 3,
  ESTABLISHED = 4,
  FIN_WAIT_1  = 5,
  FIN_WAIT_2  = 6,
  CLOSE_WAIT  = 7,
  CLOSING     = 8,
  LAST_ACK    = 9,
  TIME_WAIT   = 10
};

/**
 * members common to struct tcp_pcb and struct tcp_listen_pcb
 */
#define TCP_PCB_COMMON(type) \
  type *next; /* for the linked list */ \
  enum tcp_state state; /* TCP state */ \
  u8_t prio; \
  void *callback_arg; \
  /* the accept callback for listen- and normal pcbs, if LWIP_CALLBACK_API */ \
  tcp_accept_fn accept; \
  /* ports are in host byte order */ \
  u16_t local_port

#define ETHARP_HWADDR_LEN     6
#define NETIF_MAX_HWADDR_LEN 6U

PACK_STRUCT_BEGIN
struct eth_addr {
  PACK_STRUCT_FIELD(u8_t addr[ETHARP_HWADDR_LEN]);
} PACK_STRUCT_STRUCT;
PACK_STRUCT_END

struct ip_addr {
  u32_t addr;
};

typedef struct ip_addr ip_addr_t;

struct etharp_entry {
  /** Pointer to a single pending outgoing packet on this ARP entry. */
  struct pbuf *q;
  ip_addr_t ipaddr;
  u32_t     r1;
  struct eth_addr ethaddr;
#if LWIP_SNMP
  struct netif *netif;
#endif /* LWIP_SNMP */
  u8_t state;
  u8_t ctime;
  u8_t static_entry;
  u8_t r2[5];
};

/** Function prototype for netif->input functions. This function is saved as 'input'
 * callback function in the netif struct. Call it when a packet has been received.
 *
 * @param p The received packet, copied into a pbuf
 * @param inp The netif which received the packet
 */
typedef err_t (*netif_input_fn)(struct pbuf *p, struct netif *inp);

/** Function prototype for netif->output functions. Called by lwIP when a packet
 * shall be sent. For ethernet netif, set this to 'etharp_output' and set
 * 'linkoutput'.
 *
 * @param netif The netif which shall send a packet
 * @param p The packet to send (p->payload points to IP header)
 * @param ipaddr The IP address to which the packet shall be sent
 */
typedef err_t (*netif_output_fn)(struct netif *netif, struct pbuf *p,
       ip_addr_t *ipaddr);

/** Function prototype for netif->linkoutput functions. Only used for ethernet
 * netifs. This function is called by ARP when a packet shall be sent.
 *
 * @param netif The netif which shall send a packet
 * @param p The packet to send (raw ethernet packet)
 */
typedef err_t (*netif_linkoutput_fn)(struct netif *netif, struct pbuf *p);

/** Function prototype for netif->linkinput functions.
 *
 * @param netif The netif which shall send a packet
 * @param p The packet to send (raw ethernet packet)
 */
typedef err_t (*netif_linkinput_fn)(struct netif *netif);


struct netif {
  /** pointer to next in linked list */
  struct netif *next;

  struct tcpip_thread *prot_thread;

  /** IP address configuration in network byte order */
  ip_addr_t ip_addr;
  ip_addr_t netmask;
  ip_addr_t gw;
  ip_addr_t filler;

  /** This function is called by the network device driver
   **  to pass a packet up the TCP/IP stack. */
  netif_input_fn input;
  /** This function is called by the IP module when it wants
   **  to send a packet on the interface. This function typically
   **  first resolves the hardware address, then sends the packet. */
  netif_output_fn output;
  /** This function is called by the ARP module when it wants
   **  to send a packet on the interface. This function outputs
   **  the pbuf as-is on the link medium. */
  netif_linkoutput_fn linkoutput;
  /** This function is called by the tcpip_thread when it checks
   **  for packets on the interface. */
  netif_linkinput_fn linkinput;
  /** This field can be set by the device driver and could point
   **  to state information for the device. */
  void *state;
  /** maximum transfer unit (in bytes) */
  u16_t mtu;
  /** number of bytes used in hwaddr */
  u8_t hwaddr_len;
  /** link level hardware address of this interface */
  u8_t hwaddr[NETIF_MAX_HWADDR_LEN];
  /** flags (see NETIF_FLAG_ above) */
  u8_t flags;
  /** descriptive abbreviation */
  char name[2];
  /** number of this interface */
  u8_t num;
};

PACK_STRUCT_BEGIN
struct tcp_hdr {
  PACK_STRUCT_FIELD(u16_t src);
  PACK_STRUCT_FIELD(u16_t dest);
  PACK_STRUCT_FIELD(u32_t seqno);
  PACK_STRUCT_FIELD(u32_t ackno);
  PACK_STRUCT_FIELD(u16_t _hdrlen_rsvd_flags);
  PACK_STRUCT_FIELD(u16_t wnd);
  PACK_STRUCT_FIELD(u16_t chksum);
  PACK_STRUCT_FIELD(u16_t urgp);
} PACK_STRUCT_STRUCT;
PACK_STRUCT_END

/* This structure represents a TCP segment on the unsent, unacked and ooseq queues */
struct tcp_seg {
  struct tcp_seg *next;    /* used when putting segements on a queue */
  struct pbuf *p;          /* buffer containing data + TCP header */
  u16_t len;               /* the TCP length of this segment */
  u16_t oversize_left;     /* Extra bytes available at the end of the last
                              pbuf in unsent (used for asserting vs.
                              tcp_pcb.unsent_oversized only) */
  u16_t chksum;
  u8_t  chksum_swapped;
  u8_t  flags;
#define TF_SEG_OPTS_MSS         (u8_t)0x01U /* Include MSS option. */
#define TF_SEG_OPTS_TS          (u8_t)0x02U /* Include timestamp option. */
#define TF_SEG_DATA_CHECKSUMMED (u8_t)0x04U /* ALL data (not the header) is
                                               checksummed into 'chksum' */
  struct tcp_hdr *tcphdr;  /* the TCP header */
};

/** Function prototype for tcp receive callback functions. Called when data has
 * been received.
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param tpcb The connection pcb which received data
 * @param p The received data (or NULL when the connection has been closed!)
 * @param err An error code if there has been an error receiving
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 */
typedef err_t (*tcp_recv_fn)(void *arg, struct tcp_pcb *tpcb,
                             struct pbuf *p, err_t err);

/** Function prototype for tcp sent callback functions. Called when sent data has
 * been acknowledged by the remote side. Use it to free corresponding resources.
 * This also means that the pcb has now space available to send new data.
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param tpcb The connection pcb for which data has been acknowledged
 * @param len The amount of bytes acknowledged
 * @return ERR_OK: try to send some data by calling tcp_output
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 */
typedef err_t (*tcp_sent_fn)(void *arg, struct tcp_pcb *tpcb,
                              u16_t len);

/** Function prototype for tcp poll callback functions. Called periodically as
 * specified by @see tcp_poll.
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param tpcb tcp pcb
 * @return ERR_OK: try to send some data by calling tcp_output
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 */
typedef err_t (*tcp_poll_fn)(void *arg, struct tcp_pcb *tpcb);

/** Function prototype for tcp error callback functions. Called when the pcb
 * receives a RST or is unexpectedly closed for any other reason.
 *
 * @note The corresponding pcb is already freed when this callback is called!
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param err Error code to indicate why the pcb has been closed
 *            ERR_ABRT: aborted through tcp_abort or by a TCP timer
 *            ERR_RST: the connection was reset by the remote host
 */
typedef void  (*tcp_err_fn)(void *arg, err_t err);

/** Function prototype for tcp connected callback functions. Called when a pcb
 * is connected to the remote side after initiating a connection attempt by
 * calling tcp_connect().
 *
 * @param arg Additional argument to pass to the callback function (@see tcp_arg())
 * @param tpcb The connection pcb which is connected
 * @param err An unused error code, always ERR_OK currently ;-) TODO!
 *            Only return ERR_ABRT if you have called tcp_abort from within the
 *            callback function!
 *
 * @note When a connection attempt fails, the error callback is currently called!
 */
typedef err_t (*tcp_connected_fn)(void *arg, struct tcp_pcb *tpcb, err_t err);

/* the TCP protocol control block */
struct tcp_pcb {
/** common PCB members */
  IP_PCB;
/** protocol specific PCB members */
  TCP_PCB_COMMON(struct tcp_pcb);

  /* ports are in host byte order */
  u16_t remote_port;

  u8_t flags;
  u8_t filler[5];
#define TF_ACK_DELAY   ((u8_t)0x01U)   /* Delayed ACK. */
#define TF_ACK_NOW     ((u8_t)0x02U)   /* Immediate ACK. */
#define TF_INFR        ((u8_t)0x04U)   /* In fast recovery. */
#define TF_TIMESTAMP   ((u8_t)0x08U)   /* Timestamp option enabled */
#define TF_RXCLOSED    ((u8_t)0x10U)   /* rx closed by tcp_shutdown */
#define TF_FIN         ((u8_t)0x20U)   /* Connection was closed locally (FIN segment enqueued). */
#define TF_NODELAY     ((u8_t)0x40U)   /* Disable Nagle algorithm */
#define TF_NAGLEMEMERR ((u8_t)0x80U)   /* nagle enabled, memerr, try to output to prevent delayed ACK to happen */

  /* the rest of the fields are in host byte order
     as we have to do some math with them */
  /* receiver variables */
  u32_t rcv_nxt;   /* next seqno expected */
  u16_t rcv_wnd;   /* receiver window available */
  u16_t rcv_ann_wnd; /* receiver window to announce */
  u32_t rcv_ann_right_edge; /* announced right edge of window */

  /* Timers */
  u32_t tmr;
  u8_t polltmr, pollinterval;

  /* Retransmission timer. */
  s16_t rtime;

  u16_t mss;   /* maximum segment size */

  /* RTT (round trip time) estimation variables */
  u32_t rttest; /* RTT estimate in 500ms ticks */
  u32_t rtseq;  /* sequence number being timed */
  s16_t sa, sv; /* @todo document this */

  s16_t rto;    /* retransmission time-out */
  u8_t nrtx;    /* number of retransmissions */

  /* fast retransmit/recovery */
  u32_t lastack; /* Highest acknowledged seqno. */
  u8_t dupacks;

  /* congestion avoidance/control variables */
  u16_t cwnd;
  u16_t ssthresh;

  /* sender variables */
  u32_t snd_nxt;   /* next new seqno to be sent */
  u16_t snd_wnd;   /* sender window */
  u32_t snd_wl1, snd_wl2; /* Sequence and acknowledgement numbers of last
                             window update. */
  u32_t snd_lbb;       /* Sequence number of next byte to be buffered. */

  u16_t acked;

  u16_t snd_buf;   /* Available buffer space for sending (in bytes). */
#define TCP_SNDQUEUELEN_OVERFLOW (0xffffU-3)
  u16_t snd_queuelen; /* Available buffer space for sending (in tcp_segs). */

#if TCP_OVERSIZE
  /* Extra bytes available at the end of the last pbuf in unsent. */
  u16_t unsent_oversize;
#endif /* TCP_OVERSIZE */

  /* These are ordered by sequence number: */
  struct tcp_seg *unsent;   /* Unsent (queued) segments. */
  struct tcp_seg *unacked;  /* Sent but unacknowledged segments. */
#if TCP_QUEUE_OOSEQ
  struct tcp_seg *ooseq;    /* Received out of sequence segments. */
#endif /* TCP_QUEUE_OOSEQ */

  struct pbuf *refused_data; /* Data previously received but not yet taken by upper layer */
#if LWIP_CALLBACK_API
  /* Function to be called when more send buffer space is available. */
  tcp_sent_fn sent;
  /* Function to be called when (in-sequence) data has arrived. */
  tcp_recv_fn recv;
  /* Function to be called when a connection has been set up. */
  tcp_connected_fn connected;
  /* Function which is called periodically. */
  tcp_poll_fn poll;
  /* Function to be called whenever a fatal error occurs. */
  tcp_err_fn errf;
#endif /* LWIP_CALLBACK_API */

#if LWIP_TCP_TIMESTAMPS
  u32_t ts_lastacksent;
  u32_t ts_recent;
#endif /* LWIP_TCP_TIMESTAMPS */

  /* idle time before KEEPALIVE is sent */
  u32_t keep_idle;
#if LWIP_TCP_KEEPALIVE
  u32_t keep_intvl;
  u32_t keep_cnt;
#endif /* LWIP_TCP_KEEPALIVE */

  /* Persist timer counter */
  u32_t persist_cnt;
  /* Persist timer back-off */
  u8_t persist_backoff;

  /* KEEPALIVE counter */
  u8_t keep_cnt_sent;
  int cpu;
};

struct tcp_pcb_listen {
/* Common members of all PCB types */
  IP_PCB;
/* Protocol specific PCB members */
  TCP_PCB_COMMON(struct tcp_pcb_listen);

#if TCP_LISTEN_BACKLOG
  u8_t backlog;
  u8_t accepts_pending;
#endif /* TCP_LISTEN_BACKLOG */
};

#define NUM_SOCKETS MEMP_NUM_NETCONN
#define NUM_EPOLL   1

/** Contains all internal pointers and states used for a socket */
struct lwip_sock {
  /** sockets currently are built on netconns, each socket has one netconn */
  struct netconn *conn;
  /** data that was left from the previous read */
  void *lastdata;
  /** offset in the data that was left from the previous read */
  u16_t lastoffset;
  /** number of times data was received, set by event_callback(),
 *       tested by the receive and select functions */
  s16_t rcvevent;
  /** number of times data was ACKed (free send buffer), set by event_callback(),
 *tested by select */
  u16_t sendevent;
  /** error happened for this socket, set by event_callback(), tested by select */
  u16_t errevent;
  /** last error that occurred on this socket */
  int err;
  /** counter of how many threads are waiting for this socket using select */
  int select_waiting;
  int cpu;
};

struct lwip_epoll {
  struct tcpip_thread *thread;
  int           cpu;
  lwip_mbox_t   listener_mbox;
  lwip_mbox_t   other_mbox;
};

/** Description for a task waiting in select */
struct lwip_select_cb {
  /** Pointer to the next waiting task */
  struct lwip_select_cb *next;
  /** Pointer to the previous waiting task */
  struct lwip_select_cb *prev;
  /** readset passed to select */
  fd_set *readset;
  /** writeset passed to select */
  fd_set *writeset;
  /** unimplemented: exceptset passed to select */
  fd_set *exceptset;
  /** don't signal the same semaphore twice: set to 1 when signalled */
  int sem_signalled;
  /** semaphore to wake up a task waiting for select */
  sys_sem_t sem;
};

/* The TCP PCB lists. */
union tcp_listen_pcbs_t { /* List of all TCP PCBs in LISTEN state. */
  struct tcp_pcb_listen *listen_pcbs;
  struct tcp_pcb *pcbs;
};

#define NUM_FLOWS MEMP_NUM_NETCONN

struct lwip_tcpip_data {
  /* Axioms about the above lists:
     1) Every TCP PCB that is not CLOSED is in one of the lists.
     2) A PCB is only in one of the lists.
     3) All PCBs in the tcp_listen_pcbs list is in LISTEN state.
     4) All PCBs in the tcp_tw_pcbs list is in TIME-WAIT state.
  */
  struct tcp_pcb *tcp_bound_pcbs;
  union  tcp_listen_pcbs_t tcp_listen_pcbs;
  struct tcp_pcb *tcp_pcbs[NUM_FLOWS];
  struct tcp_pcb *tcp_active_pcbs;  /* List of all TCP PCBs that are in a
                                              state in which they accept or send
                                              data. */
  struct tcp_pcb *tcp_tw_pcbs;      /* List of all TCP PCBs in TIME-WAIT. */

  struct tcp_pcb *tcp_tmp_pcb;      /* Only used for temporary storage. */
  struct tcp_pcb *tcp_input_pcb;
  struct tcp_pcb **tcp_pcb_lists[4];
  u32_t           tcp_ticks;
  int             tcpip_tcp_timer_active;
  u16_t           port;
  u16_t           reserved[3];
};

struct pbuf_handle {
  u16_t cnt;
  u16_t next_to_use;
  u16_t next_to_send;
  u32_t next_offset;

  void  *lock;
  struct pbuf *info;
  char  *buf;
};

struct tcpip_msg_handle {
  u16_t cnt;
  u16_t next_to_use;
  u16_t next_to_send;
  u32_t next_offset;

  void  *lock;
  struct tcpip_msg *info;
};

#define MAX_MEM_TAB  24

struct tcpip_thread {
  lwip_mbox_t mbox;      /* the mail box used by this thread */
  /* memory needed for all the structures */
  struct memp *memp_tab[MAX_MEM_TAB];
  /* ARP table needed for ethernet */
  struct etharp_entry arp_table[ARP_TABLE_SIZE];
  /* TCP data needed */
  struct lwip_tcpip_data tcpip_data;
  struct netif netif;
  struct lwip_sock sockets[NUM_SOCKETS];
  struct lwip_epoll epoll[NUM_EPOLL];

  int cpu;              /* The cpu core this thread is running */
  int init_done;        /* Init completed */
  int select_cb_ctr;
  int r1;
  pthread_t thread;     /* The thread identifier */
  u32_t iss;
  u8_t tcp_timer;
  u16_t ip_id;
  u8_t filler;

  /* Available IP interfaces */
  struct netif *netif_list;
  struct netif *netif_default;

  struct sys_timeo *next_timeout;

  struct lwip_select_cb *select_cb_list;
  struct pbuf_handle    pbuf_rx_handle;
  struct pbuf_handle    pbuf_tx_handle;
  struct tcpip_msg_handle msg_handle;
  ip_addr_t current_iphdr_src;    /** Source IP address of current_header */
  ip_addr_t current_iphdr_dest;   /** Destination IP address of current_header */

  /* Mutex needed for LWIP */
  pthread_mutex_t mem_mutex[MAX_MEM_TAB];
  pthread_mutex_t pbuf_mutex;
  pthread_mutex_t evt_mutex;
  pthread_mutex_t sock_alloc;
  pthread_mutex_t sock_free;
  pthread_mutex_t sock_accept;
  pthread_mutex_t sock_selscan; 
  pthread_mutex_t sock_select;
};

#endif /* __LWIP_TCP_THREAD_H__ */
