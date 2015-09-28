import logging
import json
import base64

cm_logging = logging.getLogger('cm_logging')

def trans_uuid2nid(nid2uuid, uuid):
    for nid,v in enumerate(nid2uuid):
        if v == uuid:
            return nid

class ConnectionManager(object):
    def __init__(self):
        self.connections = {}
        self.peers = set()
        self.oqueue = []
        self.sim_time = 0

    def channel(self, uuida, uuidb):
        t1 = min(uuida,uuidb)
        t2 = max(uuida,uuidb)
        if (t1,t2) not in self.connections:
            self.add_peer(uuida)
            self.add_peer(uuidb)
            c = Connection(self.oqueue, t1, t2)
            self.connections[(t1,t2)] = c
        return self.connections[(t1,t2)]

    def add_peer(self,uuid):
        self.peers.add(uuid)

    def make_commands(self, nid2ip):
        o = []
        while self.oqueue:
            (f, t, msg) = self.oqueue.pop(0)
            ip = nid2ip[t].ip if f != t else "127.0.0.1"
            dbg = "{}s: SEND F: {} T: {} -- {}".format(self.sim_time, f, t, msg),
            cm_logging.debug(dbg)
            c = {
                u'action': u'send',
                u'destination_ipv4': nid2ip[t].ip,
                u'destination_port': 9,
                u'packet': base64.b64encode(json.dumps(msg))
            }
            o.append(c)
        return o

class Connection(object):
    def __init__(self, oqueue, uuida, uuidb):
        self.uuida = min(uuida,uuidb)
        self.uuidb = max(uuida,uuidb)
        self.oqueue = oqueue

    def send(self, dest, msg):
        if self.uuida == dest:
            sender = self.uuidb
        else:
            sender = self.uuida
        self.oqueue.append((sender, dest, msg))
