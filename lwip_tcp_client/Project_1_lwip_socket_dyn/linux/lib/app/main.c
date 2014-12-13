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

struct thread_struct {
   int    connfd;
   int    cpu;
   int    port;
   struct sockaddr_in cliaddr;
};

long long send[2];
long long conn[2];

static int count6, count7, count8, count9, count10, count11, count12, count13;
static int connection[NUM_CPU];

/* The time difference in microseconds */
time_t delta_time_in_microseconds (struct timeval * t2, struct timeval * t1)
{
  /* Compute delta in second, 1/10's and 1/1000's second units */
  time_t delta_seconds      = t2 -> tv_sec - t1 -> tv_sec;
  time_t delta_microseconds = t2 -> tv_usec - t1 -> tv_usec;

  if (delta_microseconds < 0)
    { /* manually carry a one from the seconds field */
      delta_microseconds += 1000000;                            /* 1e6 */
      -- delta_seconds;
    }
  return (delta_seconds * 1000000) + delta_microseconds;
}

void *
handle_connection (void *arg) {
  int cpu = *(int *) arg;
  int listenfd[100001], connfd;
  struct sockaddr_in servaddr, cliaddr;
  socklen_t clilen;
  u64_t     n,i;
  struct timespec start, stop;
  int code;
  struct sockaddr_in sa_loc;
  int idx, inc = 0;
  int j =0;
  signed int x;
  char mesg[1500];

  lwip_thread_aff(cpu);
  pthread_yield();
  for (i=1; i<11; i++) {
        listenfd[i] = lwip_socket(AF_INET,SOCK_STREAM,0);
        memset(&servaddr,0,sizeof(struct sockaddr_in));
        cliaddr.sin_family = AF_INET;
        cliaddr.sin_port = htons(i);
	if (cpu == 19) {
        	cliaddr.sin_addr.s_addr = inet_addr("10.0.0.5");
	} else if (cpu == 20) {
		cliaddr.sin_addr.s_addr = inet_addr("10.0.0.6");
	} else if (cpu == 21) {
		cliaddr.sin_addr.s_addr = inet_addr("10.0.0.7");
	} else if (cpu == 22) {
                cliaddr.sin_addr.s_addr = inet_addr("10.0.0.8");
        } else if (cpu == 23) {
                cliaddr.sin_addr.s_addr = inet_addr("10.0.0.9");
        }

        lwip_bind(listenfd[i], (struct sockaddr *)&cliaddr, sizeof(cliaddr));
        memset(&servaddr,0,sizeof(struct sockaddr_in));
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr=inet_addr("10.0.0.1");
        servaddr.sin_port=bswap_16(80);
        x = lwip_connect(listenfd[i], (struct sockaddr *)&servaddr, sizeof(servaddr));
        if (x<0) {
          printf("connect error \n");
	  continue;
        }
        conn[1]++;
 }
 //strncpy(mesg, "hellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohello", 100);
 strncpy(mesg, "hellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohello", 1450);

 while (1){
   for (i=1; i<11; i++) {
     lwip_sendto(listenfd[i],mesg,1450,0,NULL,0);
     //lwip_sendto(listenfd[i],mesg,100,0,NULL,0);
     send[1]++;
   }
 }
}

void print_stats (int sig)
{
   printf ("\n%lld \n %lld\n", send[0], send[1]);
   printf ("\n%lld \n %lld\n", conn[0], conn[1]);
   exit(1);
}

int main(int argc, char**argv) {
 int listenfd[100001], connfd;
 struct sockaddr_in servaddr, cliaddr;
 socklen_t clilen;
 u64_t     n,i;
 struct timespec start, stop;
 int code;
 struct sockaddr_in sa_loc;
 int idx, inc = 0;
 int j =0;
 signed int x;
 int cpus[NUM_CPU];
 pthread_attr_t attr;
 struct sched_param param = {
     .sched_priority = 9
 };
 char mesg[1500];
 pthread_t temp;
 
 lwip_thread_aff(18);
 pthread_yield();
 tcpip_init(NULL, NULL);
 signal(SIGINT, print_stats);
 send[0] = 0;
 send[1] = 0;

 for (i=1; i<11; i++) {
        listenfd[i] = lwip_socket(AF_INET,SOCK_STREAM,0);
        memset(&servaddr,0,sizeof(struct sockaddr_in));
        cliaddr.sin_family = AF_INET;
        cliaddr.sin_port = htons(i);
        cliaddr.sin_addr.s_addr = inet_addr("10.0.0.3");
        lwip_bind(listenfd[i], (struct sockaddr *)&cliaddr, sizeof(cliaddr));
        memset(&servaddr,0,sizeof(struct sockaddr_in));
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr=inet_addr(argv[1]);
        servaddr.sin_port=bswap_16(80);
        x = lwip_connect(listenfd[i], (struct sockaddr *)&servaddr, sizeof(servaddr));
        if (x<0) {
          printf("connect error \n");
          continue;
        }
        conn[0]++;
 }

 /*for (i=19; i<24; i++) {
   cpus[i%18] = i;
   pthread_create(&temp, NULL, (void *(*)(void *)) handle_connection, (void *) &cpus[i%18]);
 }*/

 clilen = sizeof(servaddr);
 //strncpy(mesg, "hellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohello", 100);
 strncpy(mesg, "hellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohello", 1450);

 sleep(5);

 while (1){
   //lwip_sendto(listenfd,mesg,100,0,(struct sockaddr *)&servaddr,clilen);
   for (i=1; i<11; i++) {
     x=lwip_sendto(listenfd[i],mesg,1450,0,NULL,0);
     //x = lwip_sendto(listenfd[i],mesg,100,0,NULL,0);
     if (x<0) {
       printf("send error %d \n", i);
       continue;
     }
     send[0]++;
   }
 }
 while (1) {
 }
}
