#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
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
#define MAX_CHILDREN 16
#define DISCOVERY_TIME_WAIT 5
#define DATA_TRANSFER 20
#define DATA_GENERATION 16
#define MAX_RETRANSMISSION 10
/*---------------------------------------------------------------------------*/
/* Structures */
struct node {
  struct node *next;
  linkaddr_t addr;
  uint16_t rssi;
  uint16_t rank;
};

struct tree {
  uint8_t in_tree;
  uint16_t rank;
  struct node *parent;
  uint8_t periodic;
};
/*---------------------------------------------------------------------------*/
/* Variables */
static struct tree *tree_instance;
static struct broadcast_conn broadcast;
static struct unicast_conn unicast;
static struct runicast_conn runicast;
/*---------------------------------------------------------------------------*/
/* Lists */
MEMB(passive_view_memb, struct node, MAX_PASSIVE_VIEW);
LIST(passive_view);

MEMB(children_list_memb, struct node, MAX_CHILDREN);
LIST(children_list);
/*---------------------------------------------------------------------------*/
/* Processes */
PROCESS(tree, "tree management");
PROCESS(sensor_data, "data management");
AUTOSTART_PROCESSES(&tree);
/*---------------------------------------------------------------------------*/
/* Auxiliary functions */

/* choose the best parent based on the rank and then the rssi */
static void chooseParent(){
  struct node *n;

  for(n = list_head(passive_view); n != NULL; n = list_item_next(n)){
    if(tree_instance->parent == NULL){
      tree_instance->parent = n;
    }
    if(n->rank < tree_instance->parent->rank || (tree_instance->parent->rank == n->rank && n->rssi > tree_instance->parent->rssi)){
      tree_instance->parent = n;
    }
  }

  tree_instance->rank = tree_instance->parent->rank + 1;
}

/*---------------------------------------------------------------------------*/
/* unicast callback function */
static void unicast_recv(struct unicast_conn *c, const linkaddr_t *from){
  printf("unicast message received from %d.%d : '%s'\n", from->u8[0], from->u8[1],
    (char *)packetbuf_dataptr());

  /* we've receive a message from a potential parent, we add this node in the passive_view */
  static struct node *n;
  /* check if the node is already in the passive_view*/
  for(n = list_head(passive_view); n != NULL; n = list_item_next(n)){
    if(linkaddr_cmp(&n->addr, from)){
      break;
    }
  }

  /* if not we add the node to the passive view */
  if(n == NULL){
    n = memb_alloc(&passive_view_memb);

    if(n == NULL){
      return;
    }
    linkaddr_copy(&n->addr, from);
    n->rank = (uint16_t)atoi((char *) packetbuf_dataptr());

    list_add(passive_view, n);

    n->rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
    /* if we were not yet in the tree, now we are */
    if(!tree_instance->in_tree) {
      tree_instance->in_tree = 1;
    }

    /* decision process to choose the best parent node*/
    chooseParent();
  }

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

  /* if this node is in the tree, send an unicast message with the rank of the node */
  if(tree_instance->in_tree){
    int length = snprintf(NULL, 0, "%d", tree_instance->rank);
    char msg[length+1];
    snprintf(msg, length + 1, "%d", tree_instance->rank);
    packetbuf_clear();
    packetbuf_copyfrom(msg, strlen(msg) + 1);
    unicast_send(&unicast, from);
  }
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};

/*---------------------------------------------------------------------------*/
/* runicast callback function */

static void runicast_recv(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno){
  printf("runicast message received from %d.%d, '%s'\n",
	 from->u8[0], from->u8[1], (char *)packetbuf_dataptr());

   if(linkaddr_cmp(&tree_instance->parent->addr, from)){
     /* if we receive a runicast message from our parent node
     it's a message from the gateway, so we forward the runicast message
     to all our children and we read the message */

     /* if msg recv from parent and msg == 'periodic' -> set tree_instance->periodic = 1,
      else if msg == 'data_change' -> tree_instance->periodic = 0
      else it's not normal and we run the parentChoose process */

      char *msg = (char *)packetbuf_dataptr();
      char *periodic = "periodic";
      char *data_change = "data_change";
      if(strcmp(msg, periodic) == 0){
        tree_instance->periodic = 1;
        printf("Data sending set to periodic\n");
      }
      else if(strcmp(msg, data_change) == 0){
        tree_instance->periodic = 0;
        printf("Data sending set to non periodic\n");
      }
      else {
        chooseParent();
        return;
      }

      process_post(&tree, PROCESS_EVENT_CONTINUE, NULL);
      return;
   }
   else {

     /* else add the node to our children_list if not yet in this list */
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

     /* then forward message from child to the gateway */

     runicast_send(&runicast, &tree_instance->parent->addr, MAX_RETRANSMISSION);
   }
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

/*---------------------------------------------------------------------------*/
/* tree process */
PROCESS_THREAD(tree, ev, data){

  static struct etimer et;
  PROCESS_EXITHANDLER(
    broadcast_close(&broadcast);
    unicast_close(&unicast);
  )

  PROCESS_BEGIN();

  /* Variables initialization */
  tree_instance->in_tree = 0;
  tree_instance->rank = 100;
  tree_instance->parent = NULL;
  tree_instance->periodic = 1;

  /* broadcast and unicast open */
  unicast_open(&unicast, 146, &unicast_call);
  broadcast_open(&broadcast, 129, &broadcast_call);

  while(tree_instance->in_tree != 1){

    printf("in while\n");
    etimer_set(&et, CLOCK_SECOND * DISCOVERY_TIME_WAIT);
    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER);
    /* We send discovery messages as long as we are not in the tree */
    packetbuf_clear();

    packetbuf_copyfrom("Hi", sizeof("Hi"));
    broadcast_send(&broadcast);
    printf("broadcast sent\n");
  }

  process_start(&sensor_data, NULL);

  while(1){
    PROCESS_WAIT_EVENT();
    /* we wait possible broadcast and (r)unicast messages */
    static struct etimer ett;
    static struct node *ch;
    static char* msg;
    if(tree_instance->periodic){
      msg = "periodic";
    }
    else {
      msg = "data_change";
    }

    for(ch = list_head(children_list); ch != NULL; ch = list_item_next(ch)){
      etimer_set(&ett, CLOCK_SECOND * 3);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&ett));

      packetbuf_copyfrom(msg, strlen(msg)+1);
      runicast_send(&runicast, &ch->addr, MAX_RETRANSMISSION);
    }
  }

  PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* data process */
PROCESS_THREAD(sensor_data, ev, data){

  PROCESS_EXITHANDLER(runicast_close(&runicast);)

  PROCESS_BEGIN();

  runicast_open(&runicast, 144, &runicast_call);

  while(1) {
    static struct etimer et;
    /* wait a certain amount of time before sending data */
    if(tree_instance->periodic){
      /* if we must send periodically, we wait a fixed amount of time */
      etimer_set(&et, CLOCK_SECOND * DATA_TRANSFER);
    }
    else {
      /* else we wait a random amount of time */
      etimer_set(&et, CLOCK_SECOND * DATA_GENERATION + random_rand() % (CLOCK_SECOND * DATA_GENERATION));
    }
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    /* random generation of the data */
    uint8_t temp = random_rand() % 50;
    uint8_t hum = random_rand() % 100;

    /* change uint8_t variables into string */

    int length_addr = snprintf(NULL, 0, "%d", linkaddr_node_addr.u8[0]);
    char addr_str[length_addr+1];
    snprintf(addr_str, length_addr + 1, "%d", linkaddr_node_addr.u8[0]);

    int length_addr2 = snprintf(NULL, 0, "%d", linkaddr_node_addr.u8[1]);
    char addr2_str[length_addr2+1];
    snprintf(addr2_str, length_addr2 + 1, "%d", linkaddr_node_addr.u8[1]);

    int length_temp = snprintf(NULL, 0, "%d", temp);
    char temp_str[length_temp+1];
    snprintf(temp_str, length_temp + 1, "%d", temp);

    int length_hum = snprintf(NULL, 0, "%d", hum);
    char hum_str[length_hum+1];
    snprintf(hum_str, length_hum + 1, "%d", hum);

    printf("temp: %s, hum : %s\n", temp_str, hum_str);

    char* msg = malloc(strlen(addr_str) + strlen(".") + strlen(addr2_str)
      + strlen(", 1 = ") + strlen(temp_str) + strlen(", 2 = ") + strlen(hum_str) + 1);

    /* form the message */
    strcat(msg, addr_str);
    strcat(msg, ".");
    strcat(msg, addr2_str);
    strcat(msg, ", 1 = ");
    strcat(msg, temp_str);
    strcat(msg, ", 2 = ");
    strcat(msg, hum_str);

    packetbuf_copyfrom(msg, strlen(msg) + 1);

    /* send to our parent node */
    runicast_send(&runicast, &tree_instance->parent->addr, MAX_RETRANSMISSION);
    msg = "";
    msg = NULL;
    free(msg);
  }

  PROCESS_END();
}
