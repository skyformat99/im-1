
# max threads
import json
import threading

import time
import traceback

import YouMai.Basic_pb2
from net.unblocktcpclient import UnblockTcpClient

# max threads
thread_count = 8000

# service com
com_ip = "192.168.0.12"
com_port = 8003

# user json file
json_file = "./UserJson/list0.json"


def heartbeat_callback(proto):
    ack = YouMai.Basic_pb2._HEART_BEAT_ACK
    print "heartbeat_callback ----- ack"


def heartbeat(index, users):
    thread_conn = thread_count / 50
    for ii in range(0, thread_conn):
        try:
            tcp = UnblockTcpClient(com_ip, com_port)
            tcp.start_client()

            iii = index * thread_conn + ii
            heartbeat = YouMai.Basic_pb2.Heart_Beat()
            tcp.send_proto(users["RECORDS"][iii]["id"], YouMai.Basic_pb2.HEART_BEAT, index, heartbeat, heartbeat_callback)
            print "heartbeat %d" % users["RECORDS"][iii]["id"]
            time.sleep(0.01)
        except Exception, e:
            print "Exception : %s" % traceback.format_exc()

    while 1:
        time.sleep(100)

if __name__ == '__main__':
    with open(json_file) as data_file:
        test_users = json.load(data_file)
        data_file.close()

    for i in range(0, 50):
        threading.Thread(target=heartbeat, args=(i, test_users), name='thread-' + str(i)).start()
        time.sleep(0.01)

    while 1:
        time.sleep(10)