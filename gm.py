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
        self.expected = []
        self.step = 0
        self.fallback = None
        self.splitting = False
        self.DEBUG_FALLBACK = False

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
        return "PROC {} - G({}): {} L: {} FB: {} M: {}".format(self.uuid, self.groupid, self.group, self.leader, self.fallback, self.maintain)

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
                   'congestion': self.maintain,
                   }
        self.send(self.uuid, message, dest_mod='lb')

    def send(self, peer, msg, dest_mod='gm'):
        dbg = "{}s: SEND F: {} T: {} -- {}".format(self.sim_time, self.uuid, peer, msg)
        gm_logging.debug(dbg)
        msg['module'] = dest_mod
        self.connmgr.channel(self.uuid,peer).send(peer, msg)

    def check(self):
        self.step = 1
        gm_logging.debug("{}s: DGI({}).check MNT={}".format(self.sim_time, self.uuid, self.maintain))
        if self.splitting:
            gm_logging.debug("Check exit@{}: splitting".format(self.uuid))
            return []
        #self.sawayc = False
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
            gm_logging.debug("Member {} expects {}".format(self.uuid, self.expected))

        return []

    def merge(self):
        if self.splitting:
            return []
        self.step = 2
        self.coordinators.sort()
        self.expected.sort()
        gm_logging.debug("{}s: DGI({}).merge: {}".format(self.sim_time, self.uuid,self.coordinators))
        if self.maintain:
            return []
        if self.expected:
            gm_logging.debug("{}s DGI({}) still expects {}".format(self.sim_time, self.uuid, self.expected))
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
        if self.splitting:
            return []
        self.step = 3
        gm_logging.debug("{}s: DGI({}).ready".format(self.sim_time, self.uuid))
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
           
            # The fallback group.
            t = list(self.group)
            if len(t) >= 3:
                random.shuffle(t)
                hl = len(t)/2+1
                g1 = t[hl:]
                g2 = t[:hl]
                g1.sort()
                g2.sort()
                gl2 = g2[0]
                g2 = g2[1:]
                g2id = newgid()
                fallback1 = {
                    'leader': self.leader,
                    'group': list(g1),
                    'groupid': self.groupid,
                }
                fallback2 = {
                    'leader': gl2,
                    'group': list(g2),
                    'groupid':  g2id,
                }
                self.fallback = fallback1
            else:
                fallback1 = None
                fallback2 = None
                self.fallback = None

            for peer in self.group:
                self.send(peer, {
                    'msg': "Ready",
                    'groupid': self.groupid,
                    'members': list(self.group),
                    'leader': self.leader,
                    'split': False,
                    'fallback': dict(fallback1 if peer in g1 else fallback2),
                })
            self.new_group(self.leader,self.group,self.groupid)
        return []

    def cleanup(self):
        if self.is_leader():
            self.connmgr.notify_trolls(self.uuid, self.group)
        self.step = 0
        gm_logging.debug("{}s: DGI({}).cleanup".format(self.sim_time, self.uuid))
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
        if self.sawayc == False and not self.splitting and not self.is_leader():
            self.recover()
        self.sawayc = False
        self.announce_to_lb()
        if not self.splitting and self.maintain > 0:
            self.maintain -= 1
        self.splitting = False
        dbg = "{}s END OF CLEAN {}".format(self.sim_time, self)
        gm_logging.debug(dbg)
        if self.uuid == 0 and self.sim_time > 30 and not self.DEBUG_FALLBACK:
            self.use_fallback()
            self.DEBUG_FALLBACK = True
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
                self.fallback = message['fallback']
                #assert(not self.is_leader() or self.groupid == self.pendingid)
                self.send(sender, {'msg': "ReadyAck", 'groupid': self.groupid})
            else:
                #ignore, this isn't the best process.
                pass

        elif message['msg'] == "Fallback":
            self.use_fallback()

        elif message['msg'] == "AYCResponse":
            print "{} r-expects {}".format(self.uuid, self.expected)
            if self.step != 1:
                gm_logging.debug("Discard AYC Response @{}: Too late".format(self.uuid))
            else:
                if sender in self.expected:
                    self.expected.remove(sender)
                    if message['leader'] != self.uuid:
                        if sender in self.group:
                            self.group.remove(sender)
                        self.coordinators.append(sender)

        elif message['msg'] == "AYTResponse":
            if self.step != 1:
                gm_logging.debug("Discard AYT Response @{}: Too late".format(self.uuid))
            else:
                if sender in self.expected:
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

    def use_fallback(self):
        dbg = "{}s: Fallback triggered @{} using {}".format(self.sim_time, self.uuid, self.fallback)
        gm_logging.debug(dbg)
        if self.fallback:
            if self.step != 0:
                self.splitting = True
            self.maintain = 1
            if self.leader == self.uuid:
                for peer in self.group:
                    self.send(peer, {
                        'msg': "Fallback"
                    })
            elif self.fallback['leader'] == self.uuid:
                for peer in self.fallback['group']:
                    self.send(peer, {
                        'msg': "Fallback"
                    })
            self.new_group(self.fallback['leader'],
                list(self.fallback['group']), self.fallback['groupid'])
            self.expected = []
            self.fallback = None
            self.announce_to_lb()

    def ecn(self, kind):
        GlobalGMTrace.ecnevent(self.uuid, self.sim_time, kind)
        dbg = "{}s: ECN@{} of type {}".format(self.sim_time, self.uuid, kind)
        gm_logging.debug(dbg)
        if (settings.ENABLE_SOFT_ECN and kind == "soft") or ((settings.ENABLE_SOFT_ECN and not settings.ENABLE_HARD_ECN) and kind == "hard"):
            self.maintain = 2
        if settings.ENABLE_HARD_ECN and kind == "hard":
            gm_logging.debug("ECN@{} Splitting group".format(self.uuid))
            self.use_fallback()
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
