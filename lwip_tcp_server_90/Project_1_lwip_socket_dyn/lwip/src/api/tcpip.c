/**
 * @file
 * Sequential API Main thread module
 *
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#include "lwip/opt.h"

#if !NO_SYS /* don't build if not configured for use in lwipopts.h */

#include "lwip/sys.h"
#include "lwip/memp.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/init.h"
#include "netif/etharp.h"
#include "lwip/tcpip_thread.h"
#include "lwip/sample_tracepoint.h"

/* global variables */
static tcpip_init_done_fn tcpip_init_done;
static void *tcpip_init_done_arg;

#if LWIP_TCPIP_CORE_LOCKING
/** The global semaphore to lock the stack. */
sys_mutex_t lock_tcpip_core;
#endif /* LWIP_TCPIP_CORE_LOCKING */

#define TCP_LOCAL_PORT_RANGE_START  0xc000

static void lwip_epoll_init(struct tcpip_thread *thread) {
  int num_epoll;
  for (num_epoll=0; num_epoll<NUM_EPOLL; num_epoll++) {
    thread->epoll[num_epoll].thread = thread;
    thread->epoll[num_epoll].cpu = thread->cpu;

    if(sys_lwip_mbox_new(&thread->epoll[num_epoll].listener_mbox, TCPIP_MBOX_SIZE) != ERR_OK) {
      LWIP_ASSERT("failed to create epoll listen mbox", 0);
    }
 
    if(sys_lwip_mbox_new(&thread->epoll[num_epoll].other_mbox, TCPIP_MBOX_SIZE) != ERR_OK) {
      LWIP_ASSERT("failed to create epoll listen mbox", 0);
    }
  }
}

static void 
tcpip_thread_init(struct tcpip_thread *thread)
{
  int i;
  /* init thread data */
  lwip_thread_aff(thread->cpu);
  pthread_yield();
  if(sys_lwip_mbox_new(&thread->mbox, TCPIP_MBOX_SIZE) != ERR_OK) {
    LWIP_ASSERT("failed to create tcpip_thread mbox", 0);
  }
  thread->iss = 6510;
  thread->tcpip_data.tcp_pcb_lists[0] = &thread->tcpip_data.tcp_listen_pcbs.pcbs;
  thread->tcpip_data.tcp_pcb_lists[1] = &thread->tcpip_data.tcp_bound_pcbs;
  thread->tcpip_data.tcp_pcb_lists[2] = &thread->tcpip_data.tcp_active_pcbs;
  thread->tcpip_data.tcp_pcb_lists[3] = &thread->tcpip_data.tcp_tw_pcbs;
  thread->tcpip_data.port = TCP_LOCAL_PORT_RANGE_START;
  for (i=0; i<MAX_MEM_TAB; i++) {
    pthread_mutex_init(&thread->mem_mutex[i], NULL);
  }
  pthread_mutex_init(&thread->pbuf_mutex, NULL);
  pthread_mutex_init(&thread->evt_mutex, NULL);
  pthread_mutex_init(&thread->sock_alloc, NULL);
  pthread_mutex_init(&thread->sock_free, NULL);
  pthread_mutex_init(&thread->sock_accept, NULL);
  pthread_mutex_init(&thread->sock_selscan, NULL);
  pthread_mutex_init(&thread->sock_select, NULL);
  lwip_init();
  netif_init(thread->cpu);
  etharp_init_add(thread->cpu);
  lwip_epoll_init(thread);
}

/**
 * The main lwIP thread. This thread has exclusive access to lwIP core functions
 * (unless access to them is not locked). Other threads communicate with this
 * thread using message boxes.
 *
 * It also starts all the timers to make sure they are running in the right
 * thread context.
 *
 * @param arg unused argument
 */
static void
tcpip_thread_exec(void *arg)
{
  struct tcpip_msg *msg = NULL;
  struct tcpip_thread *thread;
  uint32_t i;

  thread = (struct tcpip_thread *) arg;

  /* init thread data */
  tcpip_thread_init(thread);

  if (tcpip_init_done != NULL) {
    tcpip_init_done(tcpip_init_done_arg);
  }
  thread->init_done = 1;
  while (1) {                          /* MAIN Loop */
    LWIP_TCPIP_THREAD_ALIVE();
    for(i=0; i<PBUF_READ_DEPTH; i++) {
      /* wait for a message, timeouts are processed while waiting */
      sys_timeouts_mbox_fetch(&thread->mbox, (void **)&msg);
      if (!msg) {
        break;
      }
      switch (msg->type) {
      case TCPIP_MSG_API:
        LWIP_DEBUGF(TCPIP_DEBUG, ("tcpip_thread: API message %p\n", (void *)msg));
        msg->msg.apimsg->function(&(msg->msg.apimsg->msg));
        break;

      case TCPIP_MSG_INPKT:
        LWIP_DEBUGF(TCPIP_DEBUG, ("tcpip_thread: PACKET %p\n", (void *)msg));
        //tracepoint(sample_tracepoint, message, "tcpip_thread: received packet\n");
        if (msg->msg.inp.netif->flags & (NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET)) {
          ethernet_input(msg->msg.inp.p, msg->msg.inp.netif);
        } else {
          ip_input(msg->msg.inp.p, msg->msg.inp.netif);
        }
        memp_free(MEMP_TCPIP_MSG_INPKT, msg, thread);
        break;

      case TCPIP_MSG_CALLBACK:
        LWIP_DEBUGF(TCPIP_DEBUG, ("tcpip_thread: CALLBACK %p\n", (void *)msg));
        msg->msg.cb.function(msg->msg.cb.ctx);
        memp_free(MEMP_TCPIP_MSG_API, msg, thread);
        break;

  #if LWIP_TCPIP_TIMEOUT
      case TCPIP_MSG_TIMEOUT:
        LWIP_DEBUGF(TCPIP_DEBUG, ("tcpip_thread: TIMEOUT %p\n", (void *)msg));
        sys_timeout(msg->msg.tmo.msecs, msg->msg.tmo.h, msg->msg.tmo.arg);
        memp_free(MEMP_TCPIP_MSG_API, msg, thread);
        break;
      case TCPIP_MSG_UNTIMEOUT:
        LWIP_DEBUGF(TCPIP_DEBUG, ("tcpip_thread: UNTIMEOUT %p\n", (void *)msg));
        sys_untimeout(msg->msg.tmo.h, msg->msg.tmo.arg);
        memp_free(MEMP_TCPIP_MSG_API, msg, thread);
        break;
  #endif /* LWIP_TCPIP_TIMEOUT */

      default:
        LWIP_DEBUGF(TCPIP_DEBUG, ("tcpip_thread: invalid message: %d\n", msg->type));
        /*LWIP_ASSERT("tcpip_thread: invalid message", 0);*/
        break;
      }
      msg = NULL;
    }
    thread->netif.linkinput(&thread->netif);
  }
}

/**
 * Pass a received packet to tcpip_thread for input processing
 *
 * @param p the received packet, p->payload pointing to the Ethernet header or
 *          to an IP header (if inp doesn't have NETIF_FLAG_ETHARP or
 *          NETIF_FLAG_ETHERNET flags)
 * @param inp the network interface on which the packet was received
 */
err_t
tcpip_input(struct pbuf *p, struct netif *inp)
{
  struct tcpip_msg *msg;
  struct tcpip_thread *thread = inp->prot_thread;

  if (inp->flags & (NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET)) {
     ethernet_input(p, inp);
  }
  return ERR_OK;
}

/**
 * Call a specific function in the thread context of
 * tcpip_thread for easy access synchronization.
 * A function called in that way may access lwIP core code
 * without fearing concurrent access.
 *
 * @param f the function to call
 * @param ctx parameter passed to f
 * @param block 1 to block until the request is posted, 0 to non-blocking mode
 * @return ERR_OK if the function was called, another err_t if not
 */
err_t
tcpip_callback_with_block(tcpip_callback_fn function, void *ctx, u8_t block)
{
  struct tcpip_msg *msg;
  char cpu = sched_getcpu() % CPU_MOD;

  if (sys_mbox_valid(&lwip_tcpip_thread[cpu]->mbox)) {
    msg = (struct tcpip_msg *)memp_malloc(MEMP_TCPIP_MSG_API, lwip_tcpip_thread[cpu]);
    if (msg == NULL) {
      return ERR_MEM;
    }

    msg->type = TCPIP_MSG_CALLBACK;
    msg->msg.cb.function = function;
    msg->msg.cb.ctx = ctx;
    if (block) {
      sys_lwip_mbox_post(&lwip_tcpip_thread[cpu]->mbox, msg);
    } else {
      if (sys_mbox_trypost(&lwip_tcpip_thread[cpu]->mbox, msg) != ERR_OK) {
        memp_free(MEMP_TCPIP_MSG_API, msg, lwip_tcpip_thread[cpu]);
        return ERR_MEM;
      }
    }
    return ERR_OK;
  }
  return ERR_VAL;
}

#if LWIP_TCPIP_TIMEOUT
/**
 * call sys_timeout in tcpip_thread
 *
 * @param msec time in milliseconds for timeout
 * @param h function to be called on timeout
 * @param arg argument to pass to timeout function h
 * @return ERR_MEM on memory error, ERR_OK otherwise
 */
err_t
tcpip_timeout(u32_t msecs, sys_timeout_handler h, void *arg)
{
  struct tcpip_msg *msg;
  char cpu = sched_getcpu();

  if (sys_mbox_valid(&lwip_tcpip_thread[cpu]->mbox)) {
    msg = (struct tcpip_msg *)memp_malloc(MEMP_TCPIP_MSG_API, lwip_tcpip_thread[cpu]);
    if (msg == NULL) {
      return ERR_MEM;
    }

    msg->type = TCPIP_MSG_TIMEOUT;
    msg->msg.tmo.msecs = msecs;
    msg->msg.tmo.h = h;
    msg->msg.tmo.arg = arg;
    sys_lwip_mbox_post(&lwip_tcpip_thread[cpu]->mbox, msg);
    return ERR_OK;
  }
  return ERR_VAL;
}

/**
 * call sys_untimeout in tcpip_thread
 *
 * @param msec time in milliseconds for timeout
 * @param h function to be called on timeout
 * @param arg argument to pass to timeout function h
 * @return ERR_MEM on memory error, ERR_OK otherwise
 */
err_t
tcpip_untimeout(sys_timeout_handler h, void *arg)
{
  struct tcpip_msg *msg;
  char cpu = sched_getcpu();

  if (sys_mbox_valid(&lwip_tcpip_thread[cpu]->mbox)) {
    msg = (struct tcpip_msg *)memp_malloc(MEMP_TCPIP_MSG_API, lwip_tcpip_thread[cpu]);
    if (msg == NULL) {
      return ERR_MEM;
    }

    msg->type = TCPIP_MSG_UNTIMEOUT;
    msg->msg.tmo.h = h;
    msg->msg.tmo.arg = arg;
    sys_lwip_mbox_post(&lwip_tcpip_thread[cpu]->mbox, msg);
    return ERR_OK;
  }
  return ERR_VAL;
}
#endif /* LWIP_TCPIP_TIMEOUT */

#if LWIP_NETCONN
/**
 * Call the lower part of a netconn_* function
 * This function is then running in the thread context
 * of tcpip_thread and has exclusive access to lwIP core code.
 *
 * @param apimsg a struct containing the function to call and its parameters
 * @return ERR_OK if the function was called, another err_t if not
 */
err_t
tcpip_apimsg(struct api_msg *apimsg)
{
  struct tcpip_msg msg;
  char cpu = sched_getcpu() % CPU_MOD;
#ifdef LWIP_DEBUG
  /* catch functions that don't set err */
  apimsg->msg.err = ERR_VAL;
#endif
  
  if (sys_mbox_valid(&lwip_tcpip_thread[cpu]->mbox)) {
    msg.type = TCPIP_MSG_API;
    msg.msg.apimsg = apimsg;
    sys_lwip_mbox_post(&lwip_tcpip_thread[cpu]->mbox, &msg);
    sys_arch_sem_wait(&apimsg->msg.conn->op_completed, 0);
    return apimsg->msg.err;
  }
  return ERR_VAL;
}

/**
 * Call the lower part of a netconn_* function
 * This function is then running in the thread context
 * of tcpip_thread and has exclusive access to lwIP core code.
 *
 * @param apimsg a struct containing the function to call and its parameters
 * @return ERR_OK if the function was called, another err_t if not
 */
err_t
tcpip_apimsg_no_wait(struct api_msg *apimsg)
{
  struct tcpip_msg msg;
  char cpu = sched_getcpu() % CPU_MOD;
#ifdef LWIP_DEBUG
  /* catch functions that don't set err */
  apimsg->msg.err = ERR_VAL;
#endif

  apimsg->msg.err = ERR_OK;
  if (sys_mbox_valid(&lwip_tcpip_thread[cpu]->mbox)) {
    msg.type = TCPIP_MSG_API;
    msg.msg.apimsg = apimsg;
    sys_lwip_mbox_post(&lwip_tcpip_thread[cpu]->mbox, &msg);
    return apimsg->msg.err;
  }
  return ERR_VAL;
}

#if LWIP_TCPIP_CORE_LOCKING
/**
 * Call the lower part of a netconn_* function
 * This function has exclusive access to lwIP core code by locking it
 * before the function is called.
 *
 * @param apimsg a struct containing the function to call and its parameters
 * @return ERR_OK (only for compatibility fo tcpip_apimsg())
 */
err_t
tcpip_apimsg_lock(struct api_msg *apimsg)
{
#ifdef LWIP_DEBUG
  /* catch functions that don't set err */
  apimsg->msg.err = ERR_VAL;
#endif

  LOCK_TCPIP_CORE();
  apimsg->function(&(apimsg->msg));
  UNLOCK_TCPIP_CORE();
  return apimsg->msg.err;

}
#endif /* LWIP_TCPIP_CORE_LOCKING */
#endif /* LWIP_NETCONN */

#if LWIP_NETIF_API
#if !LWIP_TCPIP_CORE_LOCKING
/**
 * Much like tcpip_apimsg, but calls the lower part of a netifapi_*
 * function.
 *
 * @param netifapimsg a struct containing the function to call and its parameters
 * @return error code given back by the function that was called
 */
err_t
tcpip_netifapi(struct netifapi_msg* netifapimsg)
{
  struct tcpip_msg msg;
  
  if (sys_mbox_valid(&mbox)) {
    err_t err = sys_sem_new(&netifapimsg->msg.sem, 0);
    if (err != ERR_OK) {
      netifapimsg->msg.err = err;
      return err;
    }
    
    msg.type = TCPIP_MSG_NETIFAPI;
    msg.msg.netifapimsg = netifapimsg;
    sys_mbox_post(&mbox, &msg);
    sys_sem_wait(&netifapimsg->msg.sem);
    sys_sem_free(&netifapimsg->msg.sem);
    return netifapimsg->msg.err;
  }
  return ERR_VAL;
}
#else /* !LWIP_TCPIP_CORE_LOCKING */
/**
 * Call the lower part of a netifapi_* function
 * This function has exclusive access to lwIP core code by locking it
 * before the function is called.
 *
 * @param netifapimsg a struct containing the function to call and its parameters
 * @return ERR_OK (only for compatibility fo tcpip_netifapi())
 */
err_t
tcpip_netifapi_lock(struct netifapi_msg* netifapimsg)
{
  LOCK_TCPIP_CORE();  
  netifapimsg->function(&(netifapimsg->msg));
  UNLOCK_TCPIP_CORE();
  return netifapimsg->msg.err;
}
#endif /* !LWIP_TCPIP_CORE_LOCKING */
#endif /* LWIP_NETIF_API */

/**
 * Initialize this module:
 * - initialize all sub modules
 * - start the tcpip_thread
 *
 * @param initfunc a function to call when tcpip_thread is running and finished initializing
 * @param arg argument to pass to initfunc
 */
void
tcpip_init(tcpip_init_done_fn initfunc, void *arg)
{
  u16_t i;

  tcpip_init_done = initfunc;
  tcpip_init_done_arg = arg;

  for (i=0; i<NUM_CPU; i++) {
    lwip_tcpip_thread[i+6] = malloc(sizeof(struct tcpip_thread));
    memset(lwip_tcpip_thread[i+6], 0 , sizeof(struct tcpip_thread));
    lwip_tcpip_thread[i+6]->cpu = i+6;

    sys_thread_new(TCPIP_THREAD_NAME, tcpip_thread_exec, lwip_tcpip_thread[i+6],
                   TCPIP_THREAD_STACKSIZE, TCPIP_THREAD_PRIO);

    while (!lwip_tcpip_thread[i+6]->init_done) {
    }
  }
}

/**
 * Simple callback function used with tcpip_callback to free a pbuf
 * (pbuf_free has a wrong signature for tcpip_callback)
 *
 * @param p The pbuf (chain) to be dereferenced.
 */
static void
pbuf_free_int(void *p)
{
  struct pbuf *q = (struct pbuf *)p;
  pbuf_free(q, NULL);
}

/**
 * A simple wrapper function that allows you to free a pbuf from interrupt context.
 *
 * @param p The pbuf (chain) to be dereferenced.
 * @return ERR_OK if callback could be enqueued, an err_t if not
 */
err_t
pbuf_free_callback(struct pbuf *p)
{
  return tcpip_callback_with_block(pbuf_free_int, p, 0);
}

/**
 * A simple wrapper function that allows you to free heap memory from
 * interrupt context.
 *
 * @param m the heap memory to free
 * @return ERR_OK if callback could be enqueued, an err_t if not
 */
err_t
mem_free_callback(void *m)
{
  return tcpip_callback_with_block(mem_free, m, 0);
}

#endif /* !NO_SYS */
