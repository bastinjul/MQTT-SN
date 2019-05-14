#include "contiki.h"
#include "net/rime/rime.h"
#include "net/rime/broadcast.h"
#include "net/rime/unicast.h"
#include "net/rime/runicast.h"
#include "random.h"
#include "lib/list.h"
#include "lib/memb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*---------------------------------------------------------------------------*/
/* Define */
#define MAX_ACTIVE_VIEW 1
#define MAX_PASSIVE_VIEW 16
#define DISCOVERY_TIME_WAIT 5
#define DATA_TRANSFER 20
#define DATA_GENERATION 16
#define MAX_RETRANSMISSION 5
/*---------------------------------------------------------------------------*/
/* Structures */
struct node {
  struct node *next;
  linkaddr_t addr;
  uint16_t rssi;
  uint8_t rank;
};

struct tree {
  uint8_t in_tree;
  uint8_t rank;
  struct node parent;
  uint8_t periodic;
};
/*---------------------------------------------------------------------------*/
/* Variables */
static struct tree *tree;
/*---------------------------------------------------------------------------*/
/* Lists */
MEMB(passive_view_memb, struct node, MAX_PASSIVE_VIEW);
LIST(passive_view);

MEMB(children_list_memb, struct node, MAX_PASSIVE_VIEW);
LIST(children_list);
/*---------------------------------------------------------------------------*/
/* Processes */
PROCESS(tree, "tree management");
PROCESS(sensor_data, "data management");
AUTOSTART_PROCESSES(&tree, &sensor_data);
/*---------------------------------------------------------------------------*/
/* Auxiliary functions */
static char* intToString(uint8_t i){
  int length = snprintf(NULL, 0, "%d", i);
  char str[length+1];
  snprintf(str, length + 1, "%d", i);
  return str;
}

static void chooseParent(){
  static struct node *n;

  for(n = list_head(passive_view); n != NULL; n = list_item_next(n)){
    if(tree->parent == NULL){
      tree->parent = n;
    }
    if(n->rank < tree->parent.rank || (tree->parent.rank == n->rank && n->rssi > tree->parent.rssi)){
      tree->parent = n;
    }
  }

  tree->rank = tree->parent.rank + 1;
}

/*---------------------------------------------------------------------------*/
/* unicast callback function */
static void unicast_recv(struct unicast_conn *c, const linkaddr_t *from){
  printf("unicast message received from %d.%d : '%s'\n", from->u8[0], from->u8[1],
    (char *)packetbuf_dataptr());

  /* we've receive a message froml a potential parent, we add this node in the passive_view */
  struct node *n;

  for(n = list_head(passive_view); n != NULL; n = list_item_next(n)){
    if(linkaddr_cmp(&n->addr, from)){
      break;
    }
  }

  if(n == NULL){
    n = memb_alloc(&passive_view_memb);

    if(n == NULL){
      return;
    }
    linkaddr_copy(&n->addr, from);
    n->rank = (uint8_t)atoi((char *) packetbuf_dataptr());

    list_add(passive_view, n);
  }

  n->rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);

  if(!tree->in_tree) {
    tree->in_tree = 1;
  }

  chooseParent();

}
static void unicast_sent(struct unicast_conn *c, int status, int num_tx){
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  if(linkaddr_cmp(dest, &linkaddr_null)) {
    printf("addr null\n");
    return;
  }
  printf("unicast message sent to %d.%d: status %d num_tx %d\n",
    dest->u8[0], dest->u8[1], status, num_tx);
}
static const struct unicast_callbacks unicast_call = {unicast_recv, unicast_sent};
static struct unicast_conn unicast;

/*---------------------------------------------------------------------------*/
/* broadcast callback function */
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from){
  printf("broadcast message received from %d.%d : '%s'\n", from->u8[0], from->u8[1],
    (char *)packetbuf_dataptr());

  /* if this node is in the tree, send an unicast message with the rank of the node */
  if(tree->in_tree){
    char* msg = intToString(tree->rank);
    packetbuf_clear();
    packetbuf_copyfrom(&msg, sizeof(msg));
    unicast_send(&unicast, from);
  }
}
static const struct broadcast_callbacks broadcast_calll = {broadcast_recv};
static struct broadcast_conn broadcast;

/*---------------------------------------------------------------------------*/
/* runicast callback function */
static void runicast_recv(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno){
  printf("runicast message received from %d.%d, seqno %d\n",
	 from->u8[0], from->u8[1], seqno);

   struct children *ch;

   for(ch = list_head(children_list); ch != NULL; ch = list_item_next(ch)){
     if(linkaddr_cmp(&ch->addr, from)){
       break;
     }
   }

   if(ch == NULL) {
     ch = memb_alloc(&children_memb);

     if(ch == NULL) {
       return;
     }

     linkaddr_copy(&ch->addr, from);

     list_add(children_list, ch);
   }

   /* then forward message from child to the gateway */

   packetbuf_clear();
   packetbuf_copyfrom((char *)packetbuf_dataptr, sizeof((char *)packetbuf_dataptr));

   runicast_send(&runicast, &tree->parent, MAX_RETRANSMISSION);
}
static void runicast_sent(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){
  printf("runicast message sent to %d.%d, retransmissions %d\n",
	 to->u8[0], to->u8[1], retransmissions);
}
static void runicast_timedout(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions){
  printf("runicast message timed out when sending to %d.%d, retransmissions %d\n",
	 to->u8[0], to->u8[1], retransmissions);

  chooseParent();
}
static const struct runicast_callbacks runicast_call = {runicast_recv, runicast_sent, runicast_timedout};
static struct runicast_conn runicast;

/*---------------------------------------------------------------------------*/
/* tree process */
PROCESS_THREAD(tree, ev, data){

  PROCESS_EXITHANDLER(
    broadcast_close(&broadcast);
    unicast_close(&unicast);
  )

  PROCESS_BEGIN();

  /* Variables initialization */
  tree->in_tree = false;
  tree->rank = 100;
  tree->parent = NULL;
  tree->periodic = 1;

  /* broadcast and unicast open */
  broadcast_open(&broadcast, 129, &broadcast_call);
  unicast_open(&unicast, 146, &unicast_call);

  while(!tree->in_tree){
    static struct etimer et;
    etimer_set(&et, CLOCK_SECOND * DISCOVERY_TIME_WAIT);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    /* We send discovery messages as long as we are not in the tree */
    packetbuf_clear();
    packetbuf_copyfrom("Hi", sizeof("Hi"));
    broadcast_send(&broadcast);
  }

  process_post(&sensor_data, PROCESS_EVENT_CONTINUE, NULL);

  while(1){
    /* we wait possible broadcast and unicast messages */
  }

  PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* data process */
PROCESS_THREAD(sensor_data, ev, data){

  PROCESS_EXITHANDLER(runicast_close(&runicast);)

  PROCESS_BEGIN();

  runicast_open(&runicast, 144, &runicast_call);

  PROCESS_WAIT_EVENT();

  while(1) {
    static struct etimer et;
    if(tree->periodic){
      etimer_set(&et, CLOCK_SECOND * DATA_TRANSFER);
    }
    else {
      etimer_set(&et, CLOCK_SECOND * DATA_GENERATION + random_rand() % (CLOCK_SECOND * DATA_GENERATION));
    }
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    uint8_t temp = random_rand() % 50;
    uint8_t hum = random_rand() % 100;

    char* msg_temp = intToString(temp);
    char* msg_hum = intToString(hum);
    char* addr0 = intToString(linkaddr_node_addr.u8[0]);
    char* addr1 = intToString(linkaddr_node_addr.u8[1]);

    char* msg = "";
    strcat(msg, addr0);
    strcat(msg, ".");
    strcat(msg, addr1);
    strcat(msg, ", 1 = ");
    strcat(msg, msg_temp);
    strcat(msg, ", 2 = ");
    strcat(msg, msg_hum);

    packetbuf_copyfrom(msg, sizeof(msg));

    runicast_send(&runicast, &tree->parent.addr, MAX_RETRANSMISSION);
  }

  PROCESS_END();
}
