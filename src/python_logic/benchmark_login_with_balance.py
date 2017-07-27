#!/usr/bin/env python
# -*- coding: utf-8 -*-

import threading
import YouMai.User_pb2
import YouMai.Basic_pb2
import json
import time
import thread
import traceback
import net.unblocktcpclient
import requests


# max threads
thread_count = 8000

# load balance
balance_ip = "192.168.0.202"
balance_port = 8000

# user json file
json_file = "./UserJson/list0.json"

success = 0
failure = 0
exception = 0

# thread locks
success_num_lock = thread.allocate_lock()
failure_num_lock = thread.allocate_lock()
exception_num_lock = thread.allocate_lock()


def login_callback(proto):
    ack = YouMai.User_pb2.User_Login_Ack()
    ack.ParseFromString(proto["body"])

    # print "proto:%s" % proto
    # {'body': '\x10\x00', 'seq': 0, 'cmd': 102, 'userid': 26987801, 'start': 123456789, 'length': 2}

    if YouMai.Basic_pb2.ERRNO_CODE.Name(ack.errer_no) == "ERRNO_CODE_OK":
        success_num_lock.acquire()
        global success
        success += 1
        success_num_lock.release()
        print "userid:%d login success, num:%d" % (proto["userid"], success)
    else:
        failure_num_lock.acquire()
        global failure
        failure += 1
        failure_num_lock.release()
        print "userid:%d login fail, num:%d \n %s" % (proto["userid"], failure, ack)


def login_req(index, users):
    thread_conn = thread_count/50
    for ii in range(0, thread_conn):
        try:
            user_id = users["RECORDS"][index * thread_conn + ii]["id"]
            params = {'userid': str(user_id)}
            r = requests.get("http://" + balance_ip + ":" + str(balance_port) + "/api/v1/sum", params=params)
            d = r.json()

            tcp = net.unblocktcpclient.UnblockTcpClient(d['ip'], d['port'])
            tcp.start_client()

            iii = index*thread_conn+ii
            login = YouMai.User_pb2.User_Login()
            login.user_id = users["RECORDS"][iii]["id"]
            login.phone = users["RECORDS"][iii]["msisdn"]
            login.pwd = users["RECORDS"][iii]["passwd"]
            login.session_id = users["RECORDS"][iii]["sessid"]
            tcp.send_proto(login.user_id, YouMai.Basic_pb2.USER_LOGIN, index, login, login_callback)
        except Exception, e:
            exception_num_lock.acquire()
            global exception
            exception += 1
            exception_num_lock.release()
            print "total_num:%d, success:%d, failure:%d, exception:%d \n %s" \
                  % (thread_count, success, failure, exception, traceback.format_exc())

    while 1:
        time.sleep(1000)  # to keep alive


if __name__ == '__main__':
    with open(json_file) as data_file:
        test_users = json.load(data_file)
        data_file.close()

    for i in range(0, 50):
        threading.Thread(target=login_req, args=(i, test_users,), name='thread-' + str(i)).start()
        time.sleep(0.01)

    while 1:
        time.sleep(10)
