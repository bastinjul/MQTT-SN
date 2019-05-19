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
#define MAX_CHILDREN 16
#define MAX_RETRANSMISSION 10
/*---------------------------------------------------------------------------*/
/* Structures */
struct node {
  struct node *next;
  linkaddr_t addr;
  uint16_t rssi;
  uint16_t rank;
};
/*---------------------------------------------------------------------------*/
/* Variables */
static struct broadcast_conn broadcast;
static struct unicast_conn unicast;
static struct runicast_conn runicast;
/*---------------------------------------------------------------------------*/
/* Lists */
MEMB(children_list_memb, struct node, MAX_CHILDREN);
LIST(children_list);
/*---------------------------------------------------------------------------*/
/* Processes */
PROCESS(tree, "tree construction");
AUTOSTART_PROCESSES(&tree);
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
static void sent_uc(struct unicast_conn *c, int status, int num_tx){
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  if(linkaddr_cmp(dest, &linkaddr_null)) {
    printf("addr null\n");
    return;
  }
  printf("unicast message sent to %d.%d: status %d num_tx %d\n",
    dest->u8[0], dest->u8[1], status, num_tx);
}
static const struct unicast_callbacks unicast_call = {unicast_recv, sent_uc};


/*---------------------------------------------------------------------------*/
/* broadcast callback function */
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from){
  printf("broadcast message received from %d.%d : '%s'\n", from->u8[0], from->u8[1],
    (char *)packetbuf_dataptr());

    /* reply with the rank of the root */
    packetbuf_copyfrom("1", sizeof("1"));
    unicast_send(&unicast, from);


}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};

/*---------------------------------------------------------------------------*/
/* runicast callback function */

static void runicast_recv(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno){
  /* Printing the message so that the gateway can read the informations */
  printf("%s\n", (char *)packetbuf_dataptr());

  /* Adding the node to our children_list */
  static struct node *ch;

  for(ch = list_head(children_list); ch != NULL; ch = list_item_next(ch)){
    if(linkaddr_cmp(&ch->addr, from)){
      break;
    }
  }

  if(ch == NULL) {
    ch = memb_alloc(&children_list_memb);

    if(ch == NULL) {
      return;
    }

    linkaddr_copy(&ch->addr, from);

    list_add(children_list, ch);
  }
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
    PROCESS_WAIT_EVENT();
    if(ev == serial_line_event_message && data != NULL){
      printf("input string : %s\n", (const char *) data);
      /* Transfer the messages from the gateway to the nodes*/
      static char* msg;
      if(strcmp((const char*) data, "data_change") == 0){
        msg = "data_change";
      }
      else {
        /* by default set periodic */
        msg = "periodic";
      }
      printf("msg : '%s'\n", msg);

      static struct node *ch;
      static struct etimer et;

      for(ch = list_head(children_list); ch != NULL; ch = list_item_next(ch)){
        etimer_set(&et, CLOCK_SECOND * 3);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
        printf("msg in for : '%s'\n", msg);
        packetbuf_copyfrom(msg, strlen(msg) + 1);
        runicast_send(&runicast, &ch->addr, MAX_RETRANSMISSION);
      }
    }
  }

  PROCESS_END();
}
