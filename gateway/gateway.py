#!/usr/bin/python
import subprocess, sys, getopt, paho.mqtt.publish as publish, paho.mqtt.client as mqtt, random
from threading import Thread

class MQTT_Publishing(Thread):

    def __init__(self, mote_nbr):
        Thread.__init__(self)
        self.mote_nbr = mote_nbr

    def run(self):
        cmd = "sudo make login TARGET=z1 MOTES=/dev/pts/" + self.mote_nbr
        print "run command :", cmd
        p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)

        while True:
            out = p.stdout.readline()
            if out == '' and p.poll != None:
                break
            elif len(out.split()) > 1 and '.' in out.split()[0] and ("1 =" or "2 =") in out:
                treat_data(out)

    def treat_data(data):
        for x in data.split(" - "):
            if len(x) > 0:
                mqtt_publish(x.split())

    def mqtt_publish(data_tab):
        if len(data_tab) == 4:
            topic = data_tab[0][:-1]
            if data_tab[1] == "1":
                topic = topic + "/temperature"
            else:
                topic = topic + "/humidity"
            publish.single(topic, data_tab[3])
        elif len(data_tab) == 7:
            topic_temp = data_tab[0][:-1] + "/temperature"
            topic_hum = data_tab[0][:-1] + "/humidity"
            msgs = [{'topic': topic_temp, 'payload': data_tab[3][:-1]},
            {'topic':topic_hum, 'payload': data_tab[6]}]
            publish.multiple(msgs)


class Broker_Listening(Thread):

    def __init__(self):
        Thread.__init__(self)

    def run(self):
        client = mqtt.Client()
        client.on_connect = on_connect
        client.on_message = on_message

        client.connect("localhost")

        client.subscribe("$SYS/broker/log/M/#", 0)

        client.loop_forever()

def on_connect(client, userdata, flags, rc):
    print("Connected with result code" + str(rc))

def on_message(client, userdata, msg):
    print(msg.topic + " " + msg.paylaod)
    s = str(msg.payload).split()[-1].split("/")
    for x in str(msg.payload).split()[-1].split("/"):
        s = s + " " + x
    if "/subscribe" in msg.topic:
        r = random.randInt(0, 1)
        if r == 1:
            s = "periodic" + s
        else:
            s = "data_change" + s
    elif "/unsubscribe" in msg.topic:
        s = "unsub" + s
    print("msg : " + s)

def main(argv):
    if(len(argv) == 0):
        print 'usage: gateway.py -m <mote_number>'
        sys.exit()
    mote_nbr = ''
    try:
        opts, args = getopt.getopt(argv, "hm:", ["mote="])
    except getopt.GetoptError:
        print 'gateway.py -m <mote_number>'
        sys.exit(2)
    for opt, arg in opts:
        if opt in ("-m", "--mote"):
            mote_nbr = arg
        elif opt == "-h":
            print 'gateway.py -m <mote_number>'
            sys.exit()
    mqtt_publish = MQTT_Publishing(mote_nbr)
    broker_listen = Broker_Listening()
    mqtt_publish.start()
    broker_listen.start()



if __name__ == '__main__':
    main(sys.argv[1:])
