#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/rime/rime.h"
#include "net/rime/broadcast.h"
#include "net/rime/unicast.h"
#include "random.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "dev/serial-line.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*---------------------------------------------------------------------------*/
/* Define */

/*---------------------------------------------------------------------------*/
/* Structures */

/*---------------------------------------------------------------------------*/
/* Variables */
static struct broadcast_conn broadcast;
static struct unicast_conn unicast;
static struct runicast_conn runicast;
/*---------------------------------------------------------------------------*/
/* Lists */

/*---------------------------------------------------------------------------*/
/* Processes */
PROCESS(tree, "tree construction");
PROCESS(serial, "get serial messages");
AUTOSTART_PROCESSES(&tree, &serial);
/*---------------------------------------------------------------------------*/
/* Auxiliary functions */
/*---------------------------------------------------------------------------*/
/* unicast callback function */
static void
unicast_recv(struct unicast_conn *c, const linkaddr_t *from)
{
  printf("unicast message received from %d.%d\n",
	 from->u8[0], from->u8[1]);
}
static const struct unicast_callbacks unicast_call = {unicast_recv};


/*---------------------------------------------------------------------------*/
/* broadcast callback function */
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from){
  printf("broadcast message received from %d.%d : '%s'\n", from->u8[0], from->u8[1],
    (char *)packetbuf_dataptr());

    /* reply with the rank of the root */
    packetbuf_copyfrom("1", sizeof("1"));
    unicast_send(&unicast, from);

    //TODO: add node to children_list
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};

/*---------------------------------------------------------------------------*/
/* runicast callback function */

static void runicast_recv(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno){
  printf("%s\n", (char *)packetbuf_dataptr());
}
static void runicast_sent(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){
  printf("runicast message sent to %d.%d, retransmissions %d\n",
	 to->u8[0], to->u8[1], retransmissions);
}
static void runicast_timedout(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){
  printf("runicast message timed out when sending to %d.%d, retransmissions %d\n",
	 to->u8[0], to->u8[1], retransmissions);
}
static const struct runicast_callbacks runicast_call = {runicast_recv, runicast_sent, runicast_timedout};

/*---------------------------------------------------------------------------*/
/* tree process */
PROCESS_THREAD(tree, ev, data){
  PROCESS_EXITHANDLER(
    unicast_close(&unicast);
    broadcast_close(&broadcast);
    runicast_close(&runicast);
  )

  PROCESS_BEGIN();

  unicast_open(&unicast, 146, &unicast_call);
  broadcast_open(&broadcast, 129, &broadcast_call);
  runicast_open(&runicast, 144, &runicast_call);

  while(1){
    static struct etimer et;
    etimer_set(&et, CLOCK_SECOND * 7);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    printf("print qqch\n");
  }

  PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* Process to get input from gateway */
PROCESS_THREAD(serial, ev, data){
  PROCESS_BEGIN();

  while(1){
    PROCESS_WAIT_EVENT();
    if(ev == serial_line_event_message && data != NULL){
      printf("input string : %s\n", (const char *) data);
      //TODO: transfer msg to all children
    }
  }

  PROCESS_END();
}