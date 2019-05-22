#!/usr/bin/python
import subprocess, sys, getopt, paho.mqtt.publish as publish, paho.mqtt.client as mqtt, random, shlex, time
from threading import Thread, Lock

buf = []
lock = Lock()

# Class to publish message to the broker and listening root output from serial2pty
class MQTT_Publishing(Thread):

    def __init__(self, mote_nbr):
        Thread.__init__(self)
        self.mote_nbr = mote_nbr
    # listening root output from serial2pty
    def run(self):
        cmd = ['sudo', 'make', 'login', 'TARGET=z1', 'MOTES=/dev/pts/' + str(self.mote_nbr)]
        print "run command :", cmd
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stdin=subprocess.PIPE)
        broker_listen = Broker_Listening(self.mote_nbr, p)
        send_to_node = Send_to_node(p)
        broker_listen.start()
        send_to_node.start()
        while True:
            out = p.stdout.readline()
            if out == '' and p.poll != None:
                break
            if "msg send:" in out:
                lock.acquire()
                if out.split()[1] in buf:
                    buf.remove(out.split()[1])
                lock.release()
            elif len(out.split()) > 1 and '.' in out.split()[0] and ("1 =" or "2 =") in out: #if the message is data
                self.treat_data(out)

    def treat_data(self, data): # split the message into messages from different hosts
        for x in data.split(" - "):
            if len(x) > 0:
                self.mqtt_publish(x.split())

    def mqtt_publish(self, data_tab): # publish to the broker the data
        print "begin publish", data_tab
        if len(data_tab) == 4:
            topic = data_tab[0][:-1]
            if data_tab[1] == "1":
                topic = topic + "/temperature"
            else:
                topic = topic + "/humidity"
            print("single")
            publish.single(topic, data_tab[3])
        elif len(data_tab) == 7:
            topic_temp = data_tab[0][:-1] + "/temperature"
            topic_hum = data_tab[0][:-1] + "/humidity"
            msgs = [{'topic': topic_temp, 'payload': data_tab[3][:-1]},
            {'topic':topic_hum, 'payload': data_tab[6]}]
            print("publish")
            publish.multiple(msgs)

# listen the broker for subscribe and unsubscribe
class Broker_Listening(Thread):

    def __init__(self, mote_nbr, proc):
        Thread.__init__(self)
        self.mote_nbr = mote_nbr
        self.proc = proc

    def run(self):
        client = mqtt.Client()
        client.on_connect = self.on_connect
        client.on_message = self.on_message

        client.connect("localhost")

        client.subscribe("$SYS/broker/log/M/#", 2)

        client.loop_forever()

    def on_connect(self, client, userdata, flags, rc):
        print("Connected with result code " + str(rc))

    def on_message(self, client, userdata, msg):
        s = ""
        for x in str(msg.payload).split()[-1].split("/"):
            s = s + " " + x
        if "/subscribe" in msg.topic:
            r = random.randint(0, 1)
            if r == 1:
                s = "periodic" + s
            else:
                s = "data_change" + s
        elif "/unsubscribe" in msg.topic:
            s = "unsub" + s
        s = s + "\n"
        if ".0" in str(msg.payload):
            lock.acquire()
            l = list(buf)
            lock.release()
            #we check if 's' is unsubscribe, we must remove the subscribe of the buffer
            if "unsub" in s:
                for x in l:
                    if s.split()[1] == x.split()[1] and s.split()[2] == x.split()[2]:
                        lock.acquire()
                        buf.remove(x)
                        lock.release()

            lock.acquire()
            buf.append(s)
            lock.release()

#sending to the root periodically the subscribe and unsubscribe
class Send_to_node(Thread):

    def __init__(self, proc):
        Thread.__init__(self)
        self.proc = proc

    def run(self):
        while True:
            time.sleep(20)
            lock.acquire()
            l = list(buf)
            lock.release()
            for x in l:
                self.proc.stdin.write(x)
                time.sleep(15)

# argument management
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
    mqtt_publish.start()



if __name__ == '__main__':
    main(sys.argv[1:])
