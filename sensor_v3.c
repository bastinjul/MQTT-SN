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
  struct node *parent;
  uint8_t periodic;
};
/*---------------------------------------------------------------------------*/
/* Variables */
static struct broadcast_conn broadcast;
static struct tree *tree_instance;
/*---------------------------------------------------------------------------*/
/* Lists */

/*---------------------------------------------------------------------------*/
/* Processes */
PROCESS(tree, "tree management");
AUTOSTART_PROCESSES(&tree);

/*---------------------------------------------------------------------------*/
/* Auxiliary functions */

/*---------------------------------------------------------------------------*/
/* unicast callback function */

/*---------------------------------------------------------------------------*/
/* broadcast callback function */

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from){
  printf("broadcast message received from %d.%d : '%s'\n", from->u8[0], from->u8[1],
    (char *)packetbuf_dataptr());

  /* if this node is in the tree, send an unicast message with the rank of the node */
  /*if(tree_instance->in_tree){
    int length = snprintf(NULL, 0, "%d", tree_instance->rank);
    char msg[length+1];
    snprintf(msg, length + 1, "%d", tree_instance->rank);
    packetbuf_clear();
    packetbuf_copyfrom(&msg, sizeof(msg));
    unicast_send(&unicast, from);
  }*/
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
/*---------------------------------------------------------------------------*/
/* runicast callback function */

/*---------------------------------------------------------------------------*/
/* tree process */

PROCESS_THREAD(tree, ev, data){

  static struct etimer et;
  PROCESS_EXITHANDLER(
    broadcast_close(&broadcast);
    //unicast_close(&unicast);
  )

  PROCESS_BEGIN();

  /* Variables initialization */
  tree_instance->in_tree = 0;
  //tree_instance->rank = 100;
  //tree_instance->parent = NULL;
  //tree_instance->periodic = 1;

  /* broadcast and unicast open */
  //unicast_open(&unicast, 146, &unicast_call);
  broadcast_open(&broadcast, 129, &broadcast_call);

  etimer_set(&et, CLOCK_SECOND * DISCOVERY_TIME_WAIT);
  while(1){
    printf("in while\n");

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    /* We send discovery messages as long as we are not in the tree */
    packetbuf_clear();

    packetbuf_copyfrom("1", sizeof("1"));
    broadcast_send(&broadcast);
    printf("broadcast sent\n");

    etimer_reset(&et);
  }

  //process_start(&sensor_data, NULL);

  //while(1){
    /* we wait possible broadcast and unicast messages */
    /*static struct etimer ett;
    etimer_set(&ett, CLOCK_SECOND * 7);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&ett));
    printf("print qqch\n");
  }
  */

  PROCESS_END();
}
