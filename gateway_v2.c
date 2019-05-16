#include "contiki.h"
#include "net/rime/rime.h"
#include "net/rime/broadcast.h"
#include "net/rime/unicast.h"
#include "random.h"
#include "lib/list.h"
#include "lib/memb.h"

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
/*---------------------------------------------------------------------------*/
/* Lists */

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
static const struct unicast_callbacks unicast_call = {unicast_recv};


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
/* tree process */
PROCESS_THREAD(tree, ev, data){
  PROCESS_EXITHANDLER(
    unicast_close(&unicast);
    broadcast_close(&broadcast);
  )

  PROCESS_BEGIN();

  unicast_open(&unicast, 146, &unicast_call);
  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1){
    static struct etimer et;
    etimer_set(&et, CLOCK_SECOND * 7);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    printf("print qqch\n");
  }

  PROCESS_END();
}
