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

struct sensor_data {
  uint8_t topic; //MQTT-SN one byte topic ID
  uint8_t value;
};

struct sending {
  uint8_t periodical_data_sent;
  uint8_t period;
};

#define MAX_POSSIBLE_PARENTS 16
MEMB(possible_parents_memb, struct possible_parents, MAX_POSSIBLE_PARENTS);
LIST(possible_parents_list);

#define MAX_CHILDREN 16
MEMB(children_memb, struct children, MAX_CHILDREN);
LIST(children_list);

static struct sending *period;
static struct sensor_data *temp_data;
static struct sensor_data *hum_data;
static struct dodag *dodag_instance;
/*---------------------------------------------------------------------------*/
/* Processes */
PROCESS(broadcast_dio_process, "Broadcasting dio messages");
PROCESS(enter_dodag, "Search and enter in dodag");
PROCESS(send_temp_data, "Sending temperature sensor data");
PROCESS(send_hum_data, "Sending humidity sensor data");
PROCESS(generate_temp_data, "Generation of random temperatures");
PROCESS(generate_hum_data, "Generation of random humidity data");
AUTOSTART_PROCESSES(&enter_dodag, &broadcast_dio_process, &send_temp_data, &send_hum_data, &generate_temp_data, &generate_hum_data);
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

  process_post_synch(&enter_dodag, PROCESS_EVENT_CONTINUE, NULL);

}

static const struct broadcast_callbacks broadcast_dio_call = {dio_recv};
static struct broadcast_conn broadcast_dio;
/*---------------------------------------------------------------------------*/
static struct unicast_conn sensor_data_uc;
/* Function called when unicast */
static void
recv_uc_data(struct unicast_conn *c, const linkaddr_t *from)
{
  printf("unicast message received from %d.%d : '%s'\n", from->u8[0], from->u8[1],
    (char *)packetbuf_dataptr());

  /* first add new child in our list of children */
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

  unicast_send(&sensor_data_uc, &dodag_instance->parent);
}
static void
sent_uc_data(struct unicast_conn *c, int status, int num_tx)
{
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  if(linkaddr_cmp(dest, &linkaddr_null)) {
    printf("addr null\n");
    return;
  }
  printf("unicast message sent to %d.%d: status %d num_tx %d\n",
    dest->u8[0], dest->u8[1], status, num_tx);
}
static const struct unicast_callbacks sensor_data_callbacks = {recv_uc_data, sent_uc_data};
/*---------------------------------------------------------------------------*/
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from) {
  printf("broadcast message received from %d.%d: '%s'\n",
         from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
}

static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
/*---------------------------------------------------------------------------*/
/* Broadcast dio messages process thread */
PROCESS_THREAD(broadcast_dio_process, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast_dio);)

  PROCESS_BEGIN();

  PROCESS_WAIT_EVENT();

  broadcast_open(&broadcast_dio, 129, &broadcast_call);

  while(1) {
    etimer_set(&et, CLOCK_SECOND * DIO_TRANSMITION_SECONDS);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    printf("rank : %i, parent : %d.%d\n", dodag_instance->rank, dodag_instance->parent.u8[0],
            dodag_instance->parent.u8[1]);
    uint8_t rk = dodag_instance->rank;
    int length = snprintf(NULL, 0, "%d", rk);
    char msg[length+1];
    snprintf(msg, length + 1, "%d", rk);
    packetbuf_clear();
    packetbuf_copyfrom(&msg, sizeof(msg));
    broadcast_send(&broadcast_dio);
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

  dodag_instance->rank = 100;
  dodag_instance->parent = linkaddr_null;

  broadcast_open(&broadcast_dio, 129, &broadcast_dio_call);

  /* We wait for a first broadcast message */

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
    if(pp->rank < cp->rank || (pp->rank == cp->rank && pp->rssi > cp->rssi)){
      cp = list_head(possible_parents_list);
    }
  }

  printf("parent chosen : %d.%d\n", cp->addr.u8[0], cp->addr.u8[1]);

  /* We don't need broadcast here anymore */
  broadcast_close(&broadcast_dio);

  linkaddr_copy(&dodag_instance->parent, &cp->addr);
  dodag_instance->rank = cp->rank+1;
  printf("rank_dodag : %i, rank cp : %i\n", dodag_instance->rank, cp->rank);

  etimer_reset(&et);

  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

  /* then messages are broadcast so that new nodes become aware of the existence of the dodag */
  process_post(&broadcast_dio_process, PROCESS_EVENT_CONTINUE, NULL);

  /* Then we can send data to the gateway */
  period->periodical_data_sent = 1;
  period->period = 20;
  process_post(&send_temp_data, PROCESS_EVENT_CONTINUE, NULL);
  process_post(&send_hum_data, PROCESS_EVENT_CONTINUE, NULL);
  process_post(&generate_temp_data, PROCESS_EVENT_CONTINUE, NULL);
  process_post(&generate_hum_data, PROCESS_EVENT_CONTINUE, NULL);

  PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* Process to send temperature data to the gateway */
PROCESS_THREAD(send_temp_data, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(unicast_close(&sensor_data_uc);)

  PROCESS_BEGIN();

  unicast_open(&sensor_data_uc, 146, &sensor_data_callbacks);

  PROCESS_WAIT_EVENT();

  while(1){
    if(period->periodical_data_sent){
      /* We sent data periodically */
      etimer_set(&et, CLOCK_SECOND * period->period);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    }
    else {
      /* We sent data when the value data change */
      PROCESS_WAIT_EVENT();
    }

    uint8_t v = temp_data->value;
    int length = snprintf(NULL, 0, "%d", v);
    char p2[length+1];
    snprintf(p2, length + 1, "%d", v);
    uint8_t t = temp_data->topic;
    int length2 = snprintf(NULL, 0, "%d", t);
    char p1[length2+1];
    snprintf(p1, length2 + 1, "%d", t);

    strcat(p1, "-");
    strcat(p1, p2);

    packetbuf_clear();
    packetbuf_copyfrom(&p1, sizeof(p1));

    unicast_send(&sensor_data_uc ,&dodag_instance->parent);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
/* Process to send temperature data to the gateway */
PROCESS_THREAD(send_hum_data, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(unicast_close(&sensor_data_uc);)

  PROCESS_BEGIN();

  unicast_open(&sensor_data_uc, 146, &sensor_data_callbacks);

  PROCESS_WAIT_EVENT();

  while(1){
    if(period->periodical_data_sent){
      /* We sent data periodically */
      etimer_set(&et, CLOCK_SECOND * period->period);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    }
    else {
      /* We sent data when the value data change */
      PROCESS_WAIT_EVENT();
    }

    uint8_t v = hum_data->value;
    int length = snprintf(NULL, 0, "%d", v);
    char p2[length+1];
    snprintf(p2, length + 1, "%d", v);
    uint8_t t = hum_data->topic;
    int length2 = snprintf(NULL, 0, "%d", t);
    char p1[length2+1];
    snprintf(p1, length2 + 1, "%d", t);

    strcat(p1, "-");
    strcat(p1, p2);

    packetbuf_clear();
    packetbuf_copyfrom(&p1, sizeof(p1));

    unicast_send(&sensor_data_uc ,&dodag_instance->parent);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
/* Generation of random temperature after a random amount of time */
PROCESS_THREAD(generate_temp_data, ev, data)
{
  static struct etimer et;

  PROCESS_BEGIN();

  temp_data->topic = 1;
  temp_data->value = 18;

  PROCESS_WAIT_EVENT();
  /* Send a broadcast every 16 - 32 seconds */
  etimer_set(&et, CLOCK_SECOND * 16 + random_rand() % (CLOCK_SECOND * 16));

  while (1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    temp_data->value = random_rand() % 50; //maximum 50°C

    if(!period->periodical_data_sent){
      process_post(&send_temp_data, PROCESS_EVENT_CONTINUE, NULL);
    }

    etimer_reset(&et);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
/* Generation of random humidity after a random amount of time */
PROCESS_THREAD(generate_hum_data, ev, data)
{
  static struct etimer et;

  PROCESS_BEGIN();

  hum_data->topic = 2;
  hum_data->value = 20;

  PROCESS_WAIT_EVENT();

  /* Send a broadcast every 16 - 32 seconds */
  etimer_set(&et, CLOCK_SECOND * 16 + random_rand() % (CLOCK_SECOND * 16));

  while (1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    hum_data->value = random_rand() % 100; //maximum 100% humidity

    if(!period->periodical_data_sent){
      process_post(&send_hum_data, PROCESS_EVENT_CONTINUE, NULL);
    }

    etimer_reset(&et);
  }

  PROCESS_END();
}
