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
#define MAX_CHILDREN 8
#define DISCOVERY_TIME_WAIT 5
#define DATA_TRANSFER 20
#define DATA_GENERATION 16
#define MAX_RETRANSMISSION 5
#define MAX_BUF_SIZE 300
#define STILL_ALIVE 300
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
  struct data_message *data_msg;
};

struct data_message {
  uint8_t periodic;
  uint8_t waiting;
  uint8_t temp;
  uint8_t hum;
};

struct reachable {
  struct reachable *next;
  linkaddr_t from;
  linkaddr_t addr;
};
/*---------------------------------------------------------------------------*/
/* Variables */
static struct tree *tree_instance;
static struct broadcast_conn broadcast;
static struct unicast_conn unicast;
static struct runicast_conn runicast;
static char* buf;
/*---------------------------------------------------------------------------*/
/* Lists */
MEMB(passive_view_memb, struct node, MAX_PASSIVE_VIEW);
LIST(passive_view);

MEMB(children_list_memb, struct node, MAX_CHILDREN);
LIST(children_list);

MEMB(reachable_nodes_memb, struct reachable, MAX_CHILDREN * MAX_CHILDREN);
LIST(reachable_nodes);
/*---------------------------------------------------------------------------*/
/* Processes */
PROCESS(tree, "tree management");
PROCESS(sensor_data, "data management");
AUTOSTART_PROCESSES(&tree);
/*---------------------------------------------------------------------------*/
/* Auxiliary functions */
/* set parent attributes */

static void setParentAttrs(struct node *n){
  linkaddr_copy(&tree_instance->parent->addr, &n->addr);
  tree_instance->parent->rank = n->rank;
  tree_instance->parent->rssi = n->rssi;
}

/* choose the best parent based on the rank and then the rssi */
static void chooseParent(){
  struct node *n;

  for(n = list_head(passive_view); n != NULL; n = list_item_next(n)){
    if(linkaddr_cmp(&tree_instance->parent->addr, &linkaddr_null)){
      setParentAttrs(n);
    }
    if(n->rank < tree_instance->parent->rank || (tree_instance->parent->rank == n->rank && n->rssi > tree_instance->parent->rssi)){
      setParentAttrs(n);
    }
  }

  tree_instance->rank = tree_instance->parent->rank + 1;
}

/* initialization of tree_instance */
static void init_tree(){
  tree_instance = malloc(sizeof(struct tree));
  if(tree_instance == NULL){
    printf("error malloc tree_instance\n");
  }

  tree_instance->in_tree = 0;
  tree_instance->rank = 100;

  tree_instance->parent = malloc(sizeof(struct node));
  if(tree_instance->parent == NULL){
    printf("error malloc parent\n");
  }
  tree_instance->parent->addr = linkaddr_null;
  tree_instance->parent->rssi = -100;
  tree_instance->parent->rank = 100;

  tree_instance->data_msg = malloc(sizeof(struct data_message));
  if(tree_instance->data_msg == NULL){
    printf("error malloc data_msg\n");
  }
  tree_instance->data_msg->periodic = 1;
  tree_instance->data_msg->waiting = 1;
  tree_instance->data_msg->temp = 0;
  tree_instance->data_msg->hum = 0;

  buf = malloc(MAX_BUF_SIZE * sizeof(char));
  if (buf == NULL){
    printf("error malloc buf\n");
  }

}

static char* firstMessage(){
  int length_addr = snprintf(NULL, 0, "%d", linkaddr_node_addr.u8[0]);
  char addr_str[length_addr + 1];
  snprintf(addr_str, length_addr + 1, "%d", linkaddr_node_addr.u8[0]);

  int length_addr2 = snprintf(NULL, 0, "%d", linkaddr_node_addr.u8[1]);
  char addr2_str[length_addr2 + 1];
  snprintf(addr2_str, length_addr2 + 1, "%d", linkaddr_node_addr.u8[1]);

  int length = strlen(addr_str) + strlen(".") + strlen(addr2_str) + strlen(" not aggregate") + 1;

  char* first = malloc(length);

  /* form the message */
  strcat(first, addr_str);
  strcat(first, ".");
  strcat(first, addr2_str);
  strcat(first, " not aggregate");

  return first;
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
     to the desired children if it's not for us */

      char *period = malloc(strlen((char *)packetbuf_dataptr()));
      char *msg = (char *)packetbuf_dataptr();
      strcpy(period, msg);
      strtok(period, " ");
      if(period == NULL){
        printf("invalid message received\n");
        return;
      }
      printf("period : '%s'\n", period);
      char *target = strtok(NULL, " ");
      if(target == NULL){
        printf("invalid message received\n");
        return;
      }
      printf("target : '%s'\n", target);
      char *topic = strtok(NULL, " ");
      if(topic == NULL){
        printf("invalid message received\n");
        return;
      }
      printf("topic : '%s'\n", topic);
      if(strcmp(strtok(firstMessage(), " "), target) == 0){
        /* if we are the target of the message */

        if(strstr(topic, "temperature")){
          tree_instance->data_msg->temp = 1;
          printf("generation temperature on\n");
        }
        else if(strstr(topic, "humidity")){
          tree_instance->data_msg->hum = 1;
          printf("generation humidity on\n");
        }
        if(strcmp(period, "periodic") == 0){
          tree_instance->data_msg->periodic = 1;
          tree_instance->data_msg->waiting = 0;
          printf("Data sending set to periodic\n");
        }
        else if(strcmp(period, "data_change") == 0){
          tree_instance->data_msg->periodic = 0;
          tree_instance->data_msg->waiting = 0;
          printf("Data sending set to non periodic\n");
        }
        else if(strstr(period, "unsub")){
          /* unsubscibe for one topic */
          if(strstr(topic, "temperature")){
            tree_instance->data_msg->temp = 0;
            printf("generation temperature off\n");
          } else if (strstr(topic, "humidity")){
            tree_instance->data_msg->hum = 0;
            printf("generation humidity off\n");
          }
          if(!tree_instance->data_msg->temp && !tree_instance->data_msg->hum){
            tree_instance->data_msg->waiting = 1;
            printf("Node waiting subscriber\n");
          }
          return;
        }
        process_post(&sensor_data, PROCESS_EVENT_CONTINUE, NULL);
      }
      else {
        /* if we are not the target of the message -> forward to the child
            for which the target is reachable */
        linkaddr_t addr;
        linkaddr_copy(&addr, &linkaddr_null);
        addr.u8[0] = (uint8_t)atoi(strtok(target, "."));

        static struct reachable *r;

        for(r = list_head(reachable_nodes); r != NULL; r = list_item_next(r)){
          if(linkaddr_cmp(&r->addr, &addr)){
            runicast_send(&runicast, &r->from, MAX_RETRANSMISSION);
            break;
          }
        }
      }
      free(topic);
   }
   else {

     /* else add the node to our children_list if not yet in this list */
     static struct node *ch;

     for(ch = list_head(children_list); ch != NULL; ch = list_item_next(ch)){
       if(linkaddr_cmp(&ch->addr, from)){
         break;
       }
     }
     /* if not yet in our children_list */
     if(ch == NULL) {
       ch = memb_alloc(&children_list_memb);

       if(ch == NULL) {
         return;
       }

       linkaddr_copy(&ch->addr, from);

       list_add(children_list, ch);

       /* we add this node to the reachable_nodes */
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
       char *f = malloc(strlen((char *) packetbuf_dataptr()) + 1);
       char *rmsg = (char *)packetbuf_dataptr();
       strcpy(f, rmsg);
       f = strtok(f, ".");
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
       free(f);
     }
     char* msg = (char *)packetbuf_dataptr();
     char* ret;
     ret = strstr(msg, "not aggregate");

     if(tree_instance->data_msg->waiting || ret){
       /* then forward message from child to the gateway if not aggregate or we sleep */
       printf("msg : '%s'\n", msg);
       runicast_send(&runicast, &tree_instance->parent->addr, MAX_RETRANSMISSION);
     }
     else {
       /* if we are not sleeping and the message is (by default) aggregate, we buf the msg */
       strcat(buf, " - ");
       strcat(buf, msg);
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
  init_tree();

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
  /* sending a first runicast message to advertise our presence to the tree */

  char *first = firstMessage();
  printf("first message : '%s'\n", first);

  packetbuf_clear();
  packetbuf_copyfrom(first, strlen(first) + 1);
  /* send to our parent node */
  runicast_send(&runicast, &tree_instance->parent->addr, MAX_RETRANSMISSION);

  process_start(&sensor_data, NULL);

  static struct etimer ett;

  while(1){

    etimer_set(&ett, CLOCK_SECOND * STILL_ALIVE);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&ett));
    printf("still alive\n");
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
    if(tree_instance->data_msg->waiting){
      /* if no subscriber need our informations, we sleep */
      PROCESS_WAIT_EVENT();
    }
    static struct etimer et;
    /* wait a certain amount of time before sending data */
    if(tree_instance->data_msg->periodic){
      /* if we must send periodically, we wait a fixed amount of time */
      etimer_set(&et, CLOCK_SECOND * DATA_TRANSFER);
    }
    else {
      /* else we wait a random amount of time */
      etimer_set(&et, CLOCK_SECOND * DATA_GENERATION + random_rand() % (CLOCK_SECOND * DATA_GENERATION));
    }
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    int length_addr = snprintf(NULL, 0, "%d", linkaddr_node_addr.u8[0]);
    char addr_str[length_addr+1];
    snprintf(addr_str, length_addr + 1, "%d", linkaddr_node_addr.u8[0]);

    int length_addr2 = snprintf(NULL, 0, "%d", linkaddr_node_addr.u8[1]);
    char addr2_str[length_addr2+1];
    snprintf(addr2_str, length_addr2 + 1, "%d", linkaddr_node_addr.u8[1]);

    int length = strlen(addr_str) + strlen(".") + strlen(addr2_str) + 1;

    /* random generation of the data */
    uint8_t temp = random_rand() % 50;
    uint8_t hum = random_rand() % 100;

    int length_temp = snprintf(NULL, 0, "%d", temp);
    char temp_str[length_temp+1];
    snprintf(temp_str, length_temp + 1, "%d", temp);

    if(tree_instance->data_msg->temp){
      length = length + strlen(", 1 = ") + strlen(temp_str);
    }

    int length_hum = snprintf(NULL, 0, "%d", hum);
    char hum_str[length_hum+1];
    snprintf(hum_str, length_hum + 1, "%d", hum);

    if(tree_instance->data_msg->hum){
      length = length + strlen(", 2 = ") + strlen(hum_str);
    }

    printf("genaration of data : temp = %s, hum = %s\n", temp_str, hum_str);

    /* change uint8_t variables into string */

    char* msg = malloc(length + strlen(buf) + strlen(" - "));

    /* form the message */
    strcat(msg, addr_str);
    strcat(msg, ".");
    strcat(msg, addr2_str);
    if(tree_instance->data_msg->temp){
      strcat(msg, ", 1 = ");
      strcat(msg, temp_str);
    }
    if(tree_instance->data_msg->hum){
      strcat(msg, ", 2 = ");
      strcat(msg, hum_str);
    }
    strcat(msg, " - ");
    strcat(msg, buf);

    packetbuf_copyfrom(msg, strlen(msg) + 1);

    /* send to our parent node */
    runicast_send(&runicast, &tree_instance->parent->addr, MAX_RETRANSMISSION);
    msg = "";
    msg = NULL;
    free(msg);
    free(buf);
    buf = malloc(MAX_BUF_SIZE);
  }

  PROCESS_END();
}
