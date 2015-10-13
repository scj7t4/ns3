from flask import Flask, request

import json
import base64
import logging
import random

import dgi
import lb
import gm
import connectionmanager as cm
import settings

from schedule import ScheduleCommand

app = Flask(__name__)

random.seed(settings.SEED)

def process_addr(addrs):
    print "ADDRS {}".format(addrs)
    parts = addrs.split(';')
    parts = map(lambda x: x.strip(), parts)
    tups = map(lambda x: tuple(x.split('=')), parts);
    return dict(tups)

class Ipv4Node(object):
    def __init__(self, nid, ipv4d):
        """
        {u'addresses':
            [u'm_local=10.1.1.2; m_mask=255.255.255.0; m_broadcast=10.1.1.255; m_scope=2; m_secondary=0'],
            u'up': u'true',
            u'mtu': u'1500'}
        """
        ip4 = map(process_addr, ipv4d)
        self.nid = nid
        self.ip = ip4[0]['m_local']
        self.subnet = ip4[0]['m_mask']
        self.broadcast = ip4[0]['m_broadcast']
    def __str__(self):
        return "Node{} @{}".format(self.nid, self.ip)
    def __repr__(self):
        return str(self)

def to_rpcs(objs):
    return map(lambda x: x.to_rpc(), objs)

def print_state(nid):
    print "--------------------"
    print DGIs[nid]
    return []

def save_traces():
    lb.GlobalLBTrace.plot_output()
    gm.GlobalGMTrace.plot_output()
    return []

PROCS = 30
nodes = {}
CM = cm.ConnectionManager()
CM.add_troll("10.1.1.2", 9)
CM.add_troll("10.1.2.2", 9)
CM.add_troll("10.1.3.2", 9)
DGIs = [ dgi.DGI(x,CM) for x in range(PROCS) ]
UUIDs = range(PROCS)
#random.shuffle(UUIDs)

def save_net_layout():
    cut = len(UUIDs)/2
    with open('network.layout','w+') as fp:
        fp.write(str(UUIDs[:cut]))
        fp.write("\n")
        fp.write(str(UUIDs[cut:]))

@app.route("/", methods=["GET", "PUT"])
def rpc_serv():
    rpc = json.loads(request.data)
    #print "GOT {}".format(rpc)
    rid = int(rpc[u'id'])
    nid = int(rpc[u'node_id'])
    sim_time = float(rpc[u'simulation_time'])

    CM.sim_time = sim_time
    try:
        uuid = nid
        DGIs[uuid].sim_time(sim_time)
    except IndexError:
        uuid = None
    command = rpc['method']
    resp = {
        u'id': rpc[u'id'],
        u'error': None,
        u'commands': []
    }

    #print nodes
    if command == u'recv':
        contents = base64.b64decode(rpc[u'params'][u'packet'])
        try:
            senderid = [ k for (k,v) in nodes.iteritems() if v.ip == rpc[u'params'][u'sender_ipv4'] ][0]
            senderuuid = senderid #UUIDs[senderid]
            scheds = DGIs[uuid].receive(senderuuid, json.loads(contents))
            resp[u'commands'] += to_rpcs(scheds)
            resp[u'commands'] += CM.make_commands(nodes)
        except IndexError:
            scheds = DGIs[uuid].ecn(json.loads(contents)["mode"])
            resp[u'commands'] += to_rpcs(scheds)

    elif command == u'start':
        ipv4 = rpc[u'params'][u'ipv4']['addresses']
        node = Ipv4Node(nid, ipv4)
        nodes[node.nid] = node
        # Schedule Check
        resp[u'commands'] += to_rpcs(DGIs[uuid].schedule(1000, 12, scheduling_sigma=settings.SCHEDULING_SIGMA))
        resp[u'commands'] += to_rpcs([ScheduleCommand(299000, True, lambda: print_state(nid))])
        if uuid == 0:
            resp[u'commands'] += to_rpcs([ScheduleCommand(299000, True, lambda: save_traces())])
        with open("schedule.dat","w+") as fp:
            fp.write(ScheduleCommand.summarize())
        with open("mod_labels.dat","w+") as fp:
            fp.write("{}\n".format(DGIs[0].sigma))
            ms = DGIs[0].module_schedule()
            for (time, mod) in ms:
                fp.write("{}\t{}\n".format(time,mod))

    if command == u'event':
        eid = int(rpc[u'params'][u'eventid'])
        scheds = ScheduleCommand.get(eid).do()
        resp[u'commands'] += to_rpcs(scheds)
        resp[u'commands'] += CM.make_commands(nodes)

    else:
        pass

    #print "RESP {}".format(resp)
    return json.dumps(resp)

if __name__ == "__main__":
    save_net_layout()
    gm_logging = logging.getLogger('gm_logging')
    handler = logging.FileHandler('gm.log', mode='w+')
    gm_logging.setLevel(logging.DEBUG)
    gm_logging.addHandler(handler)
    lb_logging = logging.getLogger('lb_logging')
    handler = logging.FileHandler('lb.log', mode='w+')
    lb_logging.setLevel(logging.DEBUG)
    lb_logging.addHandler(handler)
    app.run(debug=True, host='0.0.0.0')
    app.logger.setLevel(logging.CRITICAL)
