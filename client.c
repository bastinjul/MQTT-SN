#include "contiki.h"
#include "net/rime/rime.h"
#include "net/rime/broadcast.h"
#include "net/rime/runicast.h"
#include "random.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "dev/button-sensor.h"

#include "dev/leds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIO_TRANSMITION_SECONDS 5

/*---------------------------------------------------------------------------*/
/* Structures */
struct dodag {
  uint8_t in;
  uint8_t id;
  linkaddr_t parent;
  uint8_t rank;
};

struct children{
  struct children *next;
  linkaddr_t addr;
};

struct possible_parents {
  struct possible_parents *next;
  linkaddr_t addr;
  uint16_t rssi;
  uint16_t rank;
};

#define MAX_POSSIBLE_PARENTS 16
MEMB(possible_parents_memb, struct possible_parents, MAX_POSSIBLE_PARENTS);
LIST(possible_parents_list);

#define MAX_CHILDREN 16
MEMB(children_memb, struct children, MAX_CHILDREN);
LIST(children_list);

static struct dodag *dodag_instance;
/*---------------------------------------------------------------------------*/
/* Processes */
PROCESS(broadcast_dio_process, "Broadcasting dio messages");
PROCESS(enter_dodag, "Search and enter in dodag");
PROCESS(dao_unicast, "Dao unicast exchange");
AUTOSTART_PROCESSES(&enter_dodag, &dao_unicast, &broadcast_dio_process);
/*---------------------------------------------------------------------------*/
/* Function called when broadcast message is received*/
static void dio_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  printf("broadcast message received from %d.%d : '%s'\n", from->u8[0], from->u8[1],
    (char *)packetbuf_dataptr());

  struct possible_parents *pp;

  for(pp = list_head(possible_parents_list); pp != NULL; pp = list_item_next(pp)){
    if(linkaddr_cmp(&pp->addr, from)){
      break;
    }
  }

  if(pp == NULL) {
    pp = memb_alloc(&possible_parents_memb);

    if(pp == NULL) {
      return;
    }

    linkaddr_copy(&pp->addr, from);
    pp->rank = (uint16_t)atoi((char *)packetbuf_dataptr());

    list_add(possible_parents_list, pp);
  }

  pp->rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);

  struct possible_parents *test = list_head(possible_parents_list);
  printf("test : %d.%d\n", test->addr.u8[0], test->addr.u8[1]);

  process_post(&enter_dodag, PROCESS_EVENT_CONTINUE, NULL);

}

static const struct broadcast_callbacks broadcast_dio_call = {dio_recv};
static struct broadcast_conn broadcast_dio;
/*---------------------------------------------------------------------------*/
/* Function called when unicast */
static void
recv_uc_dao(struct unicast_conn *c, const linkaddr_t *from)
{
  printf("unicast message received from %d.%d : '%s'\n", from->u8[0], from->u8[1],
    (char *)packetbuf_dataptr());

  dodag_instance->id = (uint8_t) atoi((char *)packetbuf_dataptr());
  dodag_instance->in = 1;

  process_post(&dao_unicast, PROCESS_EVENT_CONTINUE, NULL);
}
static void
sent_uc_dao(struct unicast_conn *c, int status, int num_tx)
{
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  if(linkaddr_cmp(dest, &linkaddr_null)) {
    printf("addr null\n");
    return;
  }
  printf("unicast message sent to %d.%d: status %d num_tx %d\n",
    dest->u8[0], dest->u8[1], status, num_tx);
}
static const struct unicast_callbacks dao_callbacks = {recv_uc_dao, sent_uc_dao};
static struct unicast_conn dao_uc;
/*---------------------------------------------------------------------------*/
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from) {
  printf("broadcast message received from %d.%d: '%s'\n",
         from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
}

static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
/* Broadcast dio messages process thread */
PROCESS_THREAD(broadcast_dio_process, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 130, &broadcast_call);

  PROCESS_WAIT_EVENT();

  while(1) {

    /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * DIO_TRANSMITION_SECONDS * 2);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    packetbuf_copyfrom(&dodag_instance->id, sizeof(dodag_instance->id));
    broadcast_send(&broadcast);
    printf("broadcast message sent\n");
  }

  PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* Enter in dodag process thread */
PROCESS_THREAD(enter_dodag, ev, data)
{
  struct possible_parents *pp;
  struct possible_parents *cp;
  cp = NULL;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast_dio);)

  PROCESS_BEGIN();

  static struct etimer et;

  dodag_instance->id = 0;
  dodag_instance->rank = 100;
  dodag_instance->in = 0;
  dodag_instance->parent = linkaddr_null;

  broadcast_open(&broadcast_dio, 129, &broadcast_dio_call);

  /* We wait for a first broadcast message */

  //pp = list_head(possible_parents_list);
  //PROCESS_YIELD_UNTIL(pp != NULL);
  PROCESS_WAIT_EVENT();

  /* When a broadcast message is received, we wait for others messages from others nodes */
  etimer_set(&et, CLOCK_SECOND * DIO_TRANSMITION_SECONDS * 2);

  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

  /* Once all possible messages reveived,
   * we choose the best parent depends on the rank
   * and the Received Signal Strength Indicator (RSSI)
   */

  for(pp = list_head(possible_parents_list); pp != NULL; pp = list_item_next(pp)){
    if(cp == NULL){
      cp = list_head(possible_parents_list);
    }
    if(pp->rank < cp->rank || (pp->rank == cp->rank && pp->rssi < cp->rssi)){
      cp = list_head(possible_parents_list);
    }
  }

  printf("parent chosen : %d.%d\n", cp->addr.u8[0], cp->addr.u8[1]);

  /* We don't need broadcast here anymore */
  broadcast_close(&broadcast_dio);

  linkaddr_copy(&dodag_instance->parent, &cp->addr);
  dodag_instance->rank = cp->rank+1;

  printf("dodag_instance parent : %d.%d\n", dodag_instance->parent.u8[0], dodag_instance->parent.u8[1]);

  /* Then we ask to this choosen parent to enter in the dodag */
  process_post(&dao_unicast, PROCESS_EVENT_CONTINUE, NULL);


  PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* Process launch to send dao message to chosen parent */
PROCESS_THREAD(dao_unicast, ev, data)
{

  PROCESS_EXITHANDLER(unicast_close(&dao_uc);)
  PROCESS_BEGIN();

  unicast_open(&dao_uc, 146, &dao_callbacks);

  PROCESS_WAIT_EVENT();

  dodag_instance->parent.u8[1] = 0;

  printf("sending unicast to %d.%d\n",
    dodag_instance->parent.u8[0], dodag_instance->parent.u8[1]);

  packetbuf_copyfrom("dao", sizeof("dao"));
  unicast_send(&dao_uc, &dodag_instance->parent);
  printf("print qqch\n");

  PROCESS_WAIT_EVENT();

  process_post(&broadcast_dio_process, PROCESS_EVENT_CONTINUE, NULL);

  PROCESS_END();

}
