#include "contiki.h"
#include "net/rime/rime.h"
#include "net/rime/broadcast.h"
#include "net/rime/unicast.h"
#include "random.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"

#include <stdio.h>
/*---------------------------------------------------------------------------*/
PROCESS(sensor_process, "Sensor example");
PROCESS(unicast_process, "unicast process");
AUTOSTART_PROCESSES(&sensor_process, &unicast_process);
/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  printf("broadcast message received from %d.%d: '%s'\n",
         from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
static void
recv_uc(struct unicast_conn *c, const linkaddr_t *from)
{
  printf("unicast message received from %d.%d\n",
	 from->u8[0], from->u8[1]);

  linkaddr_t addr;

  linkaddr_copy(&addr, from);

  process_post_synch(&unicast_process, PROCESS_EVENT_CONTINUE, &addr);
}
static const struct unicast_callbacks unicast_callbacks = {recv_uc};
static struct unicast_conn uc;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(sensor_process, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {

    /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * 5);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    packetbuf_clear();
    packetbuf_copyfrom("1", sizeof("1"));
    broadcast_send(&broadcast);
    printf("broadcast message sent\n");
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(unicast_process, ev, data)
{
    PROCESS_EXITHANDLER(unicast_close(&uc);)

    PROCESS_BEGIN();

    unicast_open(&uc, 146, &unicast_callbacks);
    while(1){

      PROCESS_WAIT_EVENT();

      linkaddr_t *addr = data;

      packetbuf_copyfrom("15", sizeof("15"));
      unicast_send(&uc, addr);
    }

    PROCESS_END();

}
