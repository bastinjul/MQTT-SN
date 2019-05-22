#!/usr/bin/python
import sys, getopt, paho.mqtt.client as mqtt, time

def on_connect(client, userdata, flags, rc):
    print("Connected with result code " + str(rc))

def on_message(client, userdata, msg):
    print(msg.topic + " " + str(msg.payload))

"""def on_log(client, userdata, level, buf):
    print(str(buf))"""

#subscribe to the different topic and then unsubscribe
def mqtt_subscribe(topics):
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    #client.on_log = on_log

    client.connect("localhost")

    for x in topics:
        print "subscribe", x
        client.subscribe(x, 2)
        time.sleep(10)

    client.loop_forever()

# arg management
def main(argv):
    if(len(argv) == 0):
        print 'usage: subscriber.py -f <topics_file>'
        sys.exit()
    topics_file = ''
    try:
        opts, args = getopt.getopt(argv, "hf:", ["topics_file="])
    except getopt.GetoptError:
        print 'gateway.py -f <topic_file>'
        sys.exit(2)
    for opt, arg in opts:
        if opt in ("-f", "--topics_file"):
            topics_file = arg
        elif opt == "-h":
            print 'subscriber.py -f <topics_file>'
            sys.exit()

    #get all topic from the file passed in arg
    topics = []
    fo = open(topics_file, "r")
    for x in fo:
        topics.append(x[:-1])
    fo.close()
    mqtt_subscribe(topics)

if __name__ == '__main__':
    main(sys.argv[1:])
