#!/usr/bin/env python
# -*- coding: utf-8 -*-
import socket
import sys
from struct import pack, unpack
import threading

import time

import thread

import YouMai.Chat_pb2
import YouMai.Basic_pb2


class UnblockTcpClient(threading.Thread):
    def __init__(self, _ip, _port):
        self.port_ = _port
        self.ip_ = _ip
        self.pdu_length_ = 24
        self.notify_count = 0
        self.callback_queue_ = {}
        self.start_flag_ = 123456789
        self.socket_ = socket.socket
        self.buffer_ = ""
        if self.port_ == 0:
            self.port_ = 8003
        if len(self.ip_) == 0:
            self.ip_ = "192.168.0.42"
        threading.Thread.__init__(self)
        self.send_lock = thread.allocate_lock()

    def start_client(self):
        addr = (self.ip_, self.port_)
        self.socket_ = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket_.setblocking(1)
        self.socket_.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 10240)
        self.socket_.connect(addr)
        self.start()
        # thread.start_new(self.run, ())
        # 方法没有参数时需要传入空tuple

    def on_receive(self, pdu):
        self.notify_count += 1
        if pdu["cmd"] == YouMai.Basic_pb2.USER_LOGIN_ACK:
            print "login ack"

        elif pdu["cmd"] == YouMai.Basic_pb2.IMCHAT_PERSONAL:
            # notify = YouMai.Chat_pb2.IMChat_Personal_Notify()
            # notify.ParseFromString(pdu["body"])
            print "recv notify"

        elif pdu["cmd"] == YouMai.Basic_pb2.IMCHAT_PERSONAL_ACK:
            print "send ack"

        elif pdu["cmd"]==YouMai.Basic_pb2.BULLETIN_NOTIFY:
            notify = YouMai.Bulletin_pb2.Bulletin_Notify()
            notify.ParseFromString(pdu["body"])
            print ("sys._getframe().f_code.co_name", sys._getframe().f_code.co_name)
            print (notify)

        elif pdu["cmd"]==YouMai.Basic_pb2.SHOWDIAL_NOTIFY:
            print ("-------------------------SHOWDIAL_NOTIFY")
            try:
                ack = YouMai.ShowDial_pb2.ShowDial_Ack()
                ack.ParseFromString(pdu["body"])
                print (ack)
            except:
                print ("err happened")
        else:
            print(pdu)
        # raise NotImplementedError

    def send(self, pdu):
        self.send_lock.acquire()
        buffer = pack(">I", self.start_flag_) +\
                 pack(">I", pdu["userid"]) +\
                 pack(">I", pdu["cmd"]) +\
                 pack(">I", pdu["seq"]) +\
                 pack(">I", 0) + \
                 pack(">I", pdu["length"]) +\
                 pdu["body"]
        # print (sys._getframe().f_code.co_name,"send buffer ", buffer)
        self.socket_.send(buffer)
        self.send_lock.release()

    def send_proto(self, userid, cmd, seq, proto, callback):

        sendbuffer = pack(">I", self.start_flag_) +\
                     pack(">I", userid) +\
                     pack(">I", cmd) +\
                     pack(">I", seq) +\
                     pack(">I", 0) +\
                     pack(">I", proto.ByteSize()) +\
                     proto.SerializeToString()
        # print (sys._getframe().f_code.co_name,sendbuffer,"length is \n", len(sendbuffer), "proto size:", proto.ByteSize())
        if(callback!=None):
            self.callback_queue_[seq] = callback
        self.socket_.send(sendbuffer)


    def send_json(self, userid, cmd, seq, jsonstr, callback):
        sendbuffer = pack(">I", self.start_flag_) +\
                     pack(">I", userid) +\
                     pack(">I", cmd) +\
                     pack(">I", seq) +\
                     pack(">I", 0) +\
                     pack(">I", len(jsonstr)) +\
                     jsonstr
        # print (sys._getframe().f_code.co_name,sendbuffer,"length is \n", len(sendbuffer), "proto size:", len(jsonstr))
        if(callback!=None):
            self.callback_queue_[seq] = callback
        self.socket_.send(sendbuffer)

    def run(self):
        # print "run recv thread"
        while 1:
            # print (sys._getframe().f_code.co_name, "while looop")
            # print (self.socket_)
            self.socket_.recvfrom
            data = self.socket_.recv(10240)
            # print (sys._getframe().f_code.co_name, "read bytes", len(data))

            self.buffer_ = self.buffer_ + data
            if len(self.buffer_) < 4:
                print ("数据不足标记位长度，继续读")
                time.sleep(10)
                continue
            packet = {}
            # print ("len(data)",len(self.buffer_))
            packet["start"], = unpack(">I", self.buffer_[0:4])
            if self.start_flag_!=packet["start"]:
                self.buffer_ = ""
                continue
            packet["userid"], = unpack(">I", self.buffer_[4:8])
            packet["cmd"], = unpack(">I", self.buffer_[8:12])
            packet["seq"], = unpack(">I", self.buffer_[12:16])
            # datatype, reserved = unpack(">hh", self._buffer[16:20])
            # print (sys._getframe().f_code.co_name,datatype,reserved)
            packet["length"], = unpack(">I", self.buffer_[20:24])
            if packet["length"]+ self.pdu_length_ <= len(self.buffer_):
                # print (self.pdu_length_, self.pdu_length_ + packet["length"])
                packbuf = self.buffer_[self.pdu_length_: self.pdu_length_ + packet["length"]]
                packet["body"] = packbuf
                self.buffer_ = self.buffer_[packet["length"] + self.pdu_length_:-1]
                if packet["seq"] in self.callback_queue_:
                    self.callback_queue_[packet["seq"]](packet)
                    # print (sys._getframe().f_code.co_name, "[succed certain callback]", packet)
                    #删除回调
                    # self.callback_queue_.pop(packet["seq"])
                else:
                    self.on_receive(packet)
                    # print (sys._getframe().f_code.co_name, len(self.callback_queue_), "[succed default callback]", packet)
            else:
                continue

            # eee = str(data[20:20 + leng])
            # print "book list  is:", eee, "\n\n"
            # print eval(eee)
