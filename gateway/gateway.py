#!/usr/bin/python
import subprocess, sys, getopt, paho.mqtt.publish as publish

def mqtt_publishing(data_tab):
    topic_temp = data_tab[0][:-1] + "/temperature"
    topic_hum = data_tab[0][:-1] + "/humidity"
    print topic_temp
    msgs = [{'topic': topic_temp, 'payload': data_tab[3][:-1]},
    {'topic':topic_hum, 'payload': data_tab[6]}]
    publish.multiple(msgs)

def run_serialdump(mote_nbr):
    cmd = "sudo make login TARGET=z1 MOTES=/dev/pts/" + mote_nbr
    print "run command :", cmd
    p = subprocess.Popen(cmd, shell=True ,stdout=subprocess.PIPE)

    while True:
        out = p.stdout.readline()
        if out == '' and p.poll != None:
            break
        if '.' == out.split()[0][-3]:
            mqtt_publishing(out.split())

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
    run_serialdump(mote_nbr)


if __name__ == '__main__':
    main(sys.argv[1:])
