# MQTT-SN -- Group Q
MQTT-SN implementation in contiki for the course LINGI2146 - Mobile and Embedded Computing

## Repository description
- __/gateway__ : contains all files relative to the gateway
  - __gateway_v2.c__ : file  containing the code of the sensor root node
  - __gateway.py__ : file running the gateway
- __/sensor_node__ : contains all files relative to the sensor nodes
  - __sensor_v2.c__ : file containing the code of a sensor node
- __/subscriber__ : contains all files relative to the subscribers of the MQTT network

## Requirements
- *contiki* and *cooja*
- the cooja plugin *serial2pty*.
- *Mosquitto*
- *Python*
- *paho-mqtt*

## How to test
1. Inside a new simulation in cooja, add a first z1 mote with *gateway_v2.c*.
2. On this node perform a right click and choose *serial2pty*.
3. Add as many z1 motes as you want with *sensor_v2.c*.
4. Go to the __/gateway__ directory and run inside a command prompt `sudo python gateway.py -m <mote_nomber>` where `<mote_number>` is the number of the serial device of *serial2pty*. You can get this number unside cooja in the small window of the related plugin : `/dev/pts/<mote_number>.`
5. Got to __/broker__ and run `mosquitto -c broker.conf`. If mosquitto is already running, you need to kill its process.
6. Inside a new command prompt, in the __/subscriber__ directory, enter `python subscriber.py -f <topics_file>` where `<topics_file>` is a *.txt* file located in the same directory. You can perform this action as many times as you want.
7. Finally run the simulation on cooja

### Remark
The `<topics_file>` contains the different subscriptions of a subscriber. These subscriptions have the form : `<node_id>/<topic>` where `<node_id>`is the rime id of a node inside contiki and `<topic>` is *temperature* or *humidity*. An example is `2.0/temperature`.
