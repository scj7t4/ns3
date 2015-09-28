import random
import logging
import settings

from schedule import ScheduleCommand

GROUPCOUNTER = 0

gm_logging = logging.getLogger('gm_logging')

class GlobalGMTrace(object):
    sizes = {}
    ecns = {}

    @staticmethod
    def groupsizing(uuid, sim_time, size):
        sizes = GlobalGMTrace.sizes
        if uuid not in sizes:
            sizes[uuid] = {}
        sizes[uuid][sim_time] = size

    @staticmethod
    def ecnevent(uuid, sim_time, kind):
        ecns = GlobalGMTrace.ecns
        if sim_time not in ecns:
            ecns[sim_time] = []
        ecns[sim_time].append({'uuid': uuid, 'kind': kind})

    @staticmethod
    def plot_output():
        sizes = GlobalGMTrace.sizes
        ecns = GlobalGMTrace.ecns
        uuids = [k for k in sizes]
        uuids.sort()
        with open("groupsizes.dat", "w+") as fp:
            times = set()
            for k in sizes:
                for time in sizes[k]:
                    times.add(time)
            times = list(times)
            times.sort()
            fp.write("#time\t"+"\t".join([str(x) for x in uuids]))
            fp.write("\n")
            vals = {}
            for t in times:
                fp.write("{}\t".format(t))
                for uuid in uuids:
                    try:
                        vals[uuid] = str(sizes[uuid][t])
                    except KeyError:
                        pass
                fp.write("\t".join([ vals[x] for x in uuids ]))
                fp.write("\n")

        with open("ecns.dat", "w+") as fp:
            times = list(ecns)
            times.sort()
            fp.write("#time\tuuid\tkind\n")
            for t in times:
                for event in ecns[t]:
                    fp.write("{}\t{}\t{}\n".format(t, event['uuid'], event['kind']))

def newgid():
    global GROUPCOUNTER
    GROUPCOUNTER += 1
    return GROUPCOUNTER

MINLEADER = 0

class GM(object):
    ROUNDS = 1

    def __init__(self, uuid, connmgr, p=1.0):
        self.sim_time = 0.0
        self.uuid = uuid
        self.connmgr = connmgr
        self.recover()
        self.p = p
        self.maintain = 0
        self.splitting = False
        self.expected = []
        self.step = 0

    def recover(self):
        #print "{} RECOVERS".format(self.uuid)
        self.new_group(self.uuid, [], newgid())
        self.coordinators = []
        self.pending = []
        #self.expected = []
        self.pendingid = 0
        self.pendingldr = self.uuid
        self.sawayc = False
        self.maintain = 0
        self.splitting = False

    def __repr__(self):
        return "PROC {} - G({}): {} L: {}".format(self.uuid, self.groupid, self.group, self.leader)

    def __str__(self):
        return self.__repr__()

    def new_group(self, leader, group, groupid):
        self.leader = leader
        self.group = list(group)
        self.groupid = groupid
        if self.leader != self.uuid:
            GlobalGMTrace.groupsizing(self.uuid, self.sim_time, 0)
        else:
            GlobalGMTrace.groupsizing(self.uuid, self.sim_time, len(self.group)+1)

    def announce_to_lb(self):
        message = {'msg': 'Peerlist',
                   'members': self.group,
                   'leader': self.leader,
                   }
        self.send(self.uuid, message, dest_mod='lb')

    def send(self, peer, msg, dest_mod='gm'):
        dbg = "SEND F: {} T: {} -- {}".format(self.uuid, peer, msg)
        gm_logging.debug(dbg)
        msg['module'] = dest_mod
        self.connmgr.channel(self.uuid,peer).send(peer, msg)

    def check(self):
        self.step = 1
        gm_logging.debug("DGI({}).check".format(self.uuid))
        if self.splitting:
            return []
        self.sawayc = False
        self.expected = []
        self.coordinators = []
        self.groupchange = False
        self.pendingldr = self.uuid
        self.pendingid = 0
        if self.is_leader():
            checklist = self.connmgr.peers if not self.maintain else self.group
            for peer in checklist:
                if self.uuid == peer:
                    continue
                self.send(peer, {'msg': "AreYouCoordinator"})
                self.expected.append(peer)
        else:
            if not self.maintain:
                for peer in self.connmgr.peers:
                    if peer >= self.uuid:
                        continue
                    if peer == self.leader:
                        continue
                    self.send(peer, {'msg': "AreYouCoordinator"})
                    self.expected.append(peer)
            self.expected.append(self.leader)
            self.send(self.leader, {'msg':"AreYouThere", 'groupid': self.groupid})
            #print "Member {} expects {}".format(self.uuid, self.expected)

        return []

    def merge(self):
        self.step = 2
        gm_logging.debug("DGI({}).merge".format(self.uuid))
        if self.splitting:
            return []
        if self.maintain:
            return []
        self.pending = []
        groupchange = False
        if self.is_leader() and self.pendingldr >= self.uuid:
            for peer in self.expected:
                if peer in self.group:
                    self.group.remove(peer)
                    groupchange = True
            for peer in self.coordinators:
                if peer in self.group:
                    self.group.remove(peer)
                    groupchange = True
            if groupchange:
                self.new_group(self.leader,self.group,self.groupid)
            if self.coordinators:
                self.pendingid = newgid()
                self.pendingldr = self.uuid
                for peer in self.coordinators:
                    if peer in self.expected:
                        continue
                    if peer < self.uuid:
                        continue
                    self.send(peer, {'msg': "Invite", 'pendingid': self.pendingid, 'leader': self.uuid})
                self.pending = list(self.group)
        elif self.leader in self.expected:
            #self.recover()
            pass
        self.expected = []
        return []

    def ready(self):
        self.step = 3
        gm_logging.debug("DGI({}).ready".format(self.uuid))
        if self.splitting:
            return []
        if self.maintain:
            return []
        # Always send ready
        if self.is_leader() and self.pendingldr == self.uuid:
            oldgroup = list(self.group)
            if self.pending:
                self.group = list(set(self.pending))
                self.groupid = self.pendingid
            else:
                self.group = list(set(self.group))
            if self.uuid in self.group:
                self.group.remove(uuid)
            self.expected = list(self.group)
            for peer in self.group+[self.uuid]:
                self.send(peer, {
                    'msg': "Ready",
                    'groupid': self.groupid,
                    'members': list(self.group),
                    'leader': self.leader,
                    'split': False,
                })
            self.new_group(self.leader,self.group,self.groupid)
        return []

    def cleanup(self):
        self.step = 0
        gm_logging.debug("DGI({}).cleanup".format(self.uuid))
        if self.is_leader() and not self.splitting:
            for p in self.expected:
                if p in self.group:
                    self.group.remove(p)
        self.new_group(self.leader,self.group,self.groupid)
        self.expected = []
        self.coordinators = []
        self.pending = []
        self.pendingldr = self.uuid
        self.pendingid = 0
        self.splitting = False
        if self.sawayc == False and not self.is_leader():
            self.recover()
        self.announce_to_lb()
        self.sawayc = False
        if self.maintain > 0:
            self.maintain -= 1
        return []

    def is_leader(self):
        return self.leader == self.uuid

    def receive(self, sender, message):
        prnt = False #self.uuid == 0 or sender == 0
        dbg = "{}s: RECV F: {} T: {} -- {}".format(self.sim_time, sender, self.uuid, message)
        gm_logging.debug(dbg)
        if 'msg' not in message:
            raise KeyError("Bad message {}".format(message))
        if message['msg'] == "AreYouCoordinator":
            if self.is_leader():
                resp = True
            else:
                resp = False
            if sender == self.leader:
                self.sawayc = True
            self.send(sender, {'msg':"AYCResponse",'resp':resp, 'leader': self.leader})

        elif message['msg'] == "AreYouThere":
            if sender in self.group:
                resp = True
            else:
                resp = False
                self.coordinators.append(sender)
            self.send(sender, {'msg': "AYTResponse", 'resp': resp, 'groupid': self.groupid, 'members': list(self.group)})

        elif message['msg'] == "Invite":
            if ((sender < self.uuid) and (message['leader'] <= self.leader)
             and (self.pendingldr > message['leader']) and (sender in self.coordinators)):
                self.pendingid = message['pendingid']
                self.pendingldr = message['leader']
                self.send(sender, {'msg': "Accept"})

        elif message['msg'] == "Accept":
            self.pending.append(sender)
            # l = list(message[1])
            # if self.uuid in l:
            #   l.remove(self.uuid)
            # self.pending += l

        elif message['msg'] == "Ready":
            if (self.pendingldr == sender or self.leader == sender) and sender != self.uuid:
                self.new_group(message['leader'], message['members'], message['groupid'])
                self.sawayc = True
                #assert(not self.is_leader() or self.groupid == self.pendingid)
                if message['split']:
                    self.maintain = 1
                    self.splitting = True
                else:
                    self.send(sender, {'msg': "ReadyAck", 'groupid': self.groupid})
            else:
                #ignore, this isn't the best process.
                pass

        elif message['msg'] == "AYCResponse":
            print "{} r-expects {}".format(self.uuid, self.expected)
            if self.step != 1 or self.splitting:
                gm_logging.debug("Discard AYC Response @{}: Too late".format(self.uuid))
            else:
                self.expected.remove(sender)
                if message['leader'] != self.uuid:
                    if sender in self.group:
                        self.group.remove(sender)
                    self.coordinators.append(sender)

        elif message['msg'] == "AYTResponse":
            if self.step != 1:
                gm_logging.debug("Discard AYT Response @{}: Too late".format(self.uuid))
            else:
                self.expected.remove(sender)
                if not message['resp']:
                    self.recover()
                    self.coordinators.append(sender)
                else:
                    self.groupid = message['groupid']
                    self.group = list(message['members'])

        elif message['msg'] == "ReadyAck":
            if sender in self.expected:
                self.expected.remove(sender)

        else:
            raise ValueError("Unhandled message type {}".format(message[0]))
        return []

    def ecn(self, kind):
        GlobalGMTrace.ecnevent(self.uuid, self.sim_time, kind)
        dbg = "{}s: ECN@{} of type {}".format(self.sim_time, self.uuid, kind)
        gm_logging.debug(dbg)
        if (settings.ENABLE_SOFT_ECN and kind == "soft") or settings.ENABLE_HARD_ECN:
            self.maintain = 5
        if settings.ENABLE_HARD_ECN and kind == "soft" and len(self.group) > 4 and self.is_leader() and not self.splitting:
            gm_logging.debug("ECN@{} Splitting group".format(self.uuid))
            self.maintain = 5
            peers = list(self.group)
            random.shuffle(peers)

            splitsize = len(self.group)/2
            g2 = peers[splitsize:]
            g2.sort()
            gl = min(g2)
            g2.remove(gl)
            for peer in g2+[gl]:
                self.send(peer, {
                    'msg': 'Ready',
                    'groupid': newgid(),
                    'members': g2,
                    'leader': gl,
                    'split': True,
                })

            g1 = peers[:splitsize]
            g1.sort()
            self.expected = []
            self.splitting = True
            self.group = g1
            for peer in self.group:
                self.send(peer, {
                    'msg': "Ready",
                    'groupid': self.groupid,
                    'members': list(self.group),
                    'leader': self.leader,
                    'split': True,
                })
            #return [ ScheduleCommand(settings.MESSAGE_DELIVERY_GAP, False, self.cleanup) ]
        return []

    def schedule(self, start, **kwargs):
        gap = settings.MESSAGE_DELIVERY_GAP
        return [
            ScheduleCommand(start, True, self.check, self),
            ScheduleCommand(start+gap*1, True, self.merge, self),
            ScheduleCommand(start+gap*2, True, self.ready, self),
            ScheduleCommand(start+gap*3, True, self.cleanup, self),
            start+gap*4
        ]
