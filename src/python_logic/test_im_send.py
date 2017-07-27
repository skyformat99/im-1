#!/usr/bin/env python
# -*- coding: utf-8 -*-

import json
import threading

import time

import thread

import YouMai.Chat_pb2
import YouMai.Basic_pb2
import YouMai.User_pb2
import net.unblocktcpclient

users = ""
tm=""
ip="192.168.0.42"
total_send_pkt=0
sec_send_pkt=0
port=8003
success_num = 0
failure_num = 0
login_success=0
login_fail=0
success_num_lock = thread.allocate_lock()
failure_num_lock = thread.allocate_lock()

tcp_client = []


def init_users():
    global users
    with open('./UserJson/list0.json') as data_file:
        users = json.load(data_file)


def im_callback(pdu):
    if pdu["cmd"] == YouMai.Basic_pb2.USER_LOGIN_ACK:
	global login_success
        global login_fail
        ack = YouMai.User_pb2.User_Login_Ack()
        ack.ParseFromString(pdu["body"])

        if YouMai.Basic_pb2.ERRNO_CODE.Name(ack.errer_no) == "ERRNO_CODE_OK":
            success_num_lock.acquire()
            login_success += 1
            success_num_lock.release()
        else:
            failure_num_lock.acquire()
            login_fail += 1
            failure_num_lock.release()
        print "login success num:%d, login failed num:%d" % (login_success, login_fail)

    elif pdu["cmd"] == YouMai.Basic_pb2.IMCHAT_PERSONAL_ACK:
	global success_num
        global failure_num
        ack = YouMai.Chat_pb2.IMChat_Personal_Ack()
        ack.ParseFromString(pdu["body"])

        if YouMai.Basic_pb2.ERRNO_CODE.Name(ack.errer_no) == "ERRNO_CODE_OK":
            success_num_lock.acquire()
            success_num += 1
            success_num_lock.release()
        else:
            failure_num_lock.acquire()
            failure_num += 1
            failure_num_lock.release()
        #print "send success num:%d, send failed num:%d" % (success_num, failure_num)


def login():
    global users
    index = 0
    for j in range(0, 1):
        login_ = YouMai.User_pb2.User_Login()
        login_.user_id = users["RECORDS"][index]["id"]
        login_.phone = users["RECORDS"][index]["msisdn"]
        login_.pwd = users["RECORDS"][index]["passwd"]
        login_.session_id = users["RECORDS"][index]["sessid"]

        tcp = net.unblocktcpclient.UnblockTcpClient(ip, port)
        tcp.start_client()
        tcp.send_proto(login_.user_id, YouMai.Basic_pb2.USER_LOGIN, 0, login_, im_callback)
        tcp_client.append(tcp)
        index += 1
        time.sleep(0.01)


def send():
    global users
    global total_send_pkt
    index = 0
    for j in range(0, len(tcp_client)):
        chat = YouMai.Chat_pb2.IMChat_Personal()
        chat.src_usr_id = users["RECORDS"][index]["id"]
        chat.src_phone = users["RECORDS"][index]["msisdn"]
        chat.target_phone = users["RECORDS"][index + 2500]["msisdn"]

        msg = {}
        msg["CONTENT_TEXT"] = "hello,I am " + str(chat.src_usr_id)
        root = [msg]
        chat.body = json.dumps(root)
        chat.content_type = 1

        tcp = tcp_client[j]
        tcp.send_proto(chat.src_usr_id, YouMai.Basic_pb2.IMCHAT_PERSONAL, 0, chat, None)
	total_send_pkt+=1
        index += 1
        #time.sleep(0.001)


def im():
    login()
    print "-----------------------send msg------------------------"
    num=5000000
    while num:
   	send()
        num-=1

    while 1:
        time.sleep(1)

def timer():
    print "ok"
    global sec_send_pkt
    print "total send %d pkts, send %d pkt/sec,send success %d pkts ,fail %d pkts" % (total_send_pkt,total_send_pkt-sec_send_pkt,success_num,failure_num)
    sec_send_pkt=total_send_pkt
    threading.Timer(1,timer).start()

if __name__ == '__main__':
    global tm
    init_users()
    for i in range(0, 1):
        threading.Thread(target=im, args=(), name=str(i)).start()
        time.sleep(0.2)

    tm=threading.Timer(1,timer)
    tm.start()
    while 1:
        time.sleep(100)
