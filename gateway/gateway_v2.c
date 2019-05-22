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

struct reachable {
  struct reachable *next;
  linkaddr_t from;
  linkaddr_t addr;
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

MEMB(reachable_nodes_memb, struct reachable, MAX_CHILDREN * MAX_CHILDREN);
LIST(reachable_nodes);
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

    static struct reachable *r;

    r = memb_alloc(&reachable_nodes_memb);
    if(r == NULL){
      return;
    }
    linkaddr_copy(&r->addr, from);
    linkaddr_copy(&r->from, from);
    list_add(reachable_nodes, r);
  }
  else {
    /* if already in our children, we check the message and get the id of the sender node */
    linkaddr_t addr;
    linkaddr_copy(&addr, &linkaddr_null);
    char *f;
    char *rmsg = (char *)packetbuf_dataptr();
    f = strtok(rmsg, ".");
    uint8_t id = (uint8_t)atoi(f);
    addr.u8[0] = id;
    if(!linkaddr_cmp(&addr, from)){
      /* we add this node to the reachable_nodes if not yet */
      static struct reachable *rs;

      for(rs = list_head(reachable_nodes); rs != NULL; rs = list_item_next(rs)){
        if(linkaddr_cmp(&rs->addr, &addr)){
          break;
        }
      }

      if(rs == NULL){
        rs = memb_alloc(&reachable_nodes_memb);
        if(rs == NULL){
          return;
        }
        linkaddr_copy(&rs->addr, &addr);
        linkaddr_copy(&rs->from, from);
        list_add(reachable_nodes, rs);
      }
    }
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
    /* we wait for message from the gateway */
    PROCESS_WAIT_EVENT();
    if(ev == serial_line_event_message && data != NULL){
      printf("msg recv from gateway : %s\n", (char *) data);

      /* we get the data */
      static struct reachable *r;
      linkaddr_t addr;
      linkaddr_copy(&addr, &linkaddr_null);
      char *msg = (char *) data;
      char *s = malloc(strlen((char *) data));
      strcpy(s, (char *) data);
      s = strtok(s, " ");
      if(s != NULL){
        s = strtok(NULL, " ");
        if(s != NULL){
          addr.u8[0] = (uint8_t)atoi(strtok(s, "."));
          /* we send the message to the good node */
          for(r = list_head(reachable_nodes); r != NULL; r = list_item_next(r)){
            if(linkaddr_cmp(&r->addr, &addr)){
              packetbuf_clear();
              printf("msg send: %s\n", msg);
              packetbuf_copyfrom(msg, strlen(msg));
              runicast_send(&runicast, &r->from, MAX_RETRANSMISSION);
              break;
            }
          }
        }else {
            printf("error msg : '%s'\n", s);
        }
      } else {
        printf("error msg : '%s'\n", s);
      }
      free(s);
    }
  }

  PROCESS_END();
}
