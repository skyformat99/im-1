#!/usr/bin/env python
# -*- coding: utf-8 -*-


# import gl
import sys, threading
import YouMai.Chat_pb2
import YouMai.Basic_pb2
import json
import httplib, urllib, urllib2
import time
from net.unblocktcpclient  import UnblockTcpClient
import ConfigParser
import json,thread


seqid_lock = thread.allocate_lock()

recieve_num_lock = thread.allocate_lock()
send_num_lock=thread.allocate_lock()
recieve_num= 0
send_num = 0
seqid=0
phone = "15960565013"
name = "test-name"

users=""

def init_users():
    global users
    with open('users.json') as data_file:
        users = json.load(data_file)


def get_seq():
    seqid_lock.acquire()
    global seqid
    seqid += 1
    seqid_lock.release()
    # print (seqid,"seqid~~~~~~~~~")
    return seqid


def send_im_callback(proto):
    print (sys._getframe().f_code.co_name)
    ack = YouMai.Chat_pb2.IMChat_Personal_Ack()
    ack.ParseFromString(proto["body"])
    # print (proto)
    # recieve_num_lock.acquire() #加锁
    global recieve_num
    recieve_num = recieve_num + 1
    print ("recieve_num",recieve_num)
    # recieve_num_lock.release()

def send(index,client):
    global users
    print (id(client))

    print (sys._getframe().f_code.co_name, __doc__)
    chat = YouMai.Chat_pb2.IMChat_Personal()
    chat.src_usr_id = users["RECORDS"][index]["id"]
    chat.src_phone= users["RECORDS"][index]["msisdn"]
    chat.src_name = users["RECORDS"][index]["nname"]
    chat.target_phone = users["RECORDS"][15000]["nname"]

    msg={}
    msg["CONTENT_TEXT"] = "hello"
    chat.body = "[{\"CONTENT_TEXT\":\"1234567890\"}]"
    chat.content_type = 1
    global send_num
    send_num = send_num +1
    print ("send_num", send_num)
    client.send_proto(chat.src_usr_id, YouMai.Basic_pb2.IMCHAT_PERSONAL, get_seq(), chat, send_im_callback)


def benchmark(index):

    config = ConfigParser.ConfigParser()
    config.read("../service_com/config.d")

    com_ip = config.get("connection", "ip")
    com_port = int(config.get("connection", "port"))

    tcp = UnblockTcpClient(com_ip, com_port)
    print (id(tcp))
    tcp.start_client()
    time.sleep(1)

    print (sys._getframe().f_code.co_name, "THREAD ID", index)

    global users
    print (sys._getframe().f_code.co_name, __doc__)
    chat = YouMai.Chat_pb2.IMChat_Personal()
    chat.src_usr_id = users["RECORDS"][index]["id"]
    chat.src_phone = users["RECORDS"][index]["msisdn"]
    chat.src_name = users["RECORDS"][index]["nname"]
    chat.target_phone = users["RECORDS"][15000]["nname"]

    msg = {}
    msg["CONTENT_TEXT"] = "hello,I am " + str(chat.src_usr_id)
    root = [msg]
    chat.body = json.dumps(root) # "[{\"CONTENT_TEXT\":\"1234567890\"}]"
    chat.content_type = 1

    heartbeat = YouMai.Basic_pb2.Heart_Beat()



    for i in range(0, 10):
        # send(index,tcp)
        send_num_lock.acquire()
        global send_num
        send_num = send_num + 1
        send_num_lock.release()
        print ("send_num", send_num)
        #tcp.send_proto(chat.src_usr_id, YouMai.Basic_pb2.IMCHAT_PERSONAL, get_seq(), chat, send_im_callback)
        tcp.send_proto(users["RECORDS"][index]["id"], YouMai.Basic_pb2.HEART_BEAT, get_seq(), heartbeat, send_im_callback)
        time.sleep(300)
    while(1):
        time.sleep(100)  #保活，以便连接维持。


if __name__ == '__main__':
    init_users()
    for i in range(0, 50000):
        threading.Thread(target=benchmark, args=(i,), name=str(i)).start()
        time.sleep(0.2)
        #threading.Thread(target=doAdd, args=(), name='thread - ' + str(i)).start()
        #benchmark()



    while(1):
        time.sleep(10)
        print ("send_num:========================================",send_num,"recieve_num:", recieve_num)
        print ("send_num:========================================",send_num,"recieve_num:", recieve_num)
        print ("send_num:========================================",send_num,"recieve_num:", recieve_num)