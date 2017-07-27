#!/usr/bin/env python
# -*- coding: utf-8 -*-
import json
import threading

import time

import thread

import sys
sys.path.append("./")
import YouMai.User_pb2
import YouMai.Basic_pb2
import YouMai.Chat_pb2
import net.unblocktcpclient

users = ""
success_num = 0
failure_num = 0
receive_num = 0
success_num_lock = thread.allocate_lock()
failure_num_lock = thread.allocate_lock()
receive_num_lock = thread.allocate_lock()
ip="192.168.0.42"
port=8003

tcp_client = []


def init_users():
    global users
    with open('./UserJson/list0.json') as data_file:
        users = json.load(data_file)


def im_callback(pdu):
    if pdu["cmd"] == YouMai.Basic_pb2.USER_LOGIN_ACK:
        ack = YouMai.User_pb2.User_Login_Ack()
        ack.ParseFromString(pdu["body"])

        if YouMai.Basic_pb2.ERRNO_CODE.Name(ack.errer_no) == "ERRNO_CODE_OK":
            success_num_lock.acquire()
            global success_num
            success_num += 1
            success_num_lock.release()
        else:
            failure_num_lock.acquire()
            global failure_num
            failure_num += 1
            failure_num_lock.release()
        print "login success num:%d, login failed num:%d" % (success_num, failure_num)

    elif pdu["cmd"] == YouMai.Basic_pb2.IMCHAT_PERSONAL:
        notify = YouMai.Chat_pb2.IMChat_Personal_Notify()
        notify.ParseFromString(pdu["body"])

        receive_num_lock.acquire()
        global receive_num
        receive_num += 1
        receive_num_lock.release()
        print "=====================>receive num:%d" % receive_num


def login():
    global users
    global ip
    global port
    index = 2500
    for j in range(0, 10):
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

    while 1:
        time.sleep(100)

if __name__ == '__main__':
    init_users()
    for i in range(0, 1):
        threading.Thread(target=login, args=(), name=str(i)).start()
        time.sleep(0.2)

    while 1:
        time.sleep(100)

