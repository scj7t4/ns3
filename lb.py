import random
import logging
import settings

from schedule import ScheduleCommand

STEP_SIZE = 1

lb_logging = logging.getLogger('lb_logging')

class GlobalLBTrace(object):
    traces = {}
    losses = {}

    @staticmethod
    def migrate(uuid, sim_time, power_q, grid_q):
        traces = GlobalLBTrace.traces
        if uuid not in traces:
            traces[uuid] = {}
        traces[uuid][sim_time] = {'power_q': power_q, 'grid_q': grid_q}
   
    @staticmethod
    def lose(uuid, sim_time, num_losses):
        losses = GlobalLBTrace.losses
        if uuid not in losses:
            losses[uuid] = {}
        losses[uuid][sim_time] = num_losses

    @staticmethod
    def find(uuid):
        losses = GlobalLBTrace.losses
        t = max(losses[uuid])
        del losses[uuid][t]

    @staticmethod
    def plot_output():
        traces = GlobalLBTrace.traces
        losses = GlobalLBTrace.losses
        uuids = [ k for k in traces ]
        uuids.sort()
        with open("migrations.dat", "w+") as fp:
            times = set()
            for k in traces:
                for time in traces[k]:
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
                        vals[uuid] = str(traces[uuid][t]['power_q']+traces[uuid][t]['grid_q'])
                    except KeyError:
                        pass
                fp.write("\t".join([ vals[x] for x in uuids ]))
                fp.write("\n")

        with open("losses.dat", "w+") as fp:
            times = set()
            for k in losses:
                for time in losses[k]:
                    times.add(time)
            times = list(times)
            times.sort()
            fp.write("#time\t"+"\t".join([str(x) for x in uuids]))
            fp.write("\n")
            vals = dict([ (x,"0") for x in uuids])
            for t in times:
                fp.write("{}\t".format(t))
                for uuid in uuids:
                    try:
                        vals[uuid] = str(losses[uuid][t])
                    except KeyError:
                        pass
                fp.write("\t".join([ vals[x] for x in uuids ]))
                fp.write("\n")

class LB(object):
    DEMAND = 0
    NORMAL = 1
    SUPPLY = 2

    SCHEDULING_GAP = 75
    ROUNDS = 10

    def __init__(self, uuid, connmgr, power_q=None):
        self.uuid = uuid
        self.connmgr = connmgr
        self.power_q = power_q
        if self.power_q == None:
            self.power_q = float(random.randint(settings.MIN_POWER,settings.MAX_POWER))
        self.grid_q = 0.0
        self.group = []
        self.demand = set([])
        self.supply = set([])
        self.normal = set([])
        self.draftage = {}
        self.lost = 0
        self.counter = 0
        self.congested = False
        GlobalLBTrace.migrate(self.uuid, 0.0, self.power_q, self.grid_q)

    def __repr__(self):
        states = ["DEMAND", "NORMAL", "SUPPLY"]
        return "{} {} Power_Q: {} Grid_Q: {} Lost: {} Group: {}".format(
            self.uuid, states[self.state], self.power_q, self.grid_q, self.lost, self.group)

    def __str__(self):
        return self.__repr__()

    def migrate(self, amount):
        self.grid_q += amount
        GlobalLBTrace.migrate(self.uuid, self.sim_time, self.power_q, self.grid_q)

    def lost_migration(self):
        self.lost += 1
        GlobalLBTrace.lose(self.uuid, self.sim_time, self.lost)

    def found_migration(self):
        self.lost -= 1
        GlobalLBTrace.lose(self.uuid, self.sim_time, self.lost)

    @property
    def power_differential(self):
        return self.grid_q + self.power_q

    @property
    def state(self):
        s = None
        diff = self.power_differential
        # Power Shortage and Grid hasn't given enough
        if self.power_q < 0.0 and diff < 0.0:
            s = self.DEMAND
        # Power Shortage and Grid has given too much
        elif self.power_q < 0.0 and diff > 0.0:
            s = self.SUPPLY
        # Power Excess and Grid can take more
        elif self.power_q > 0.0 and diff > 0.0:
            s = self.SUPPLY
        # Power Excess but the Grid has taken too much
        elif self.power_q > 0.0 and diff < 0.0:
            s = self.DEMAND
        else:
            s = self.NORMAL
        # If the difference is small, who cares?
        if abs(diff) < STEP_SIZE:
            s = self.NORMAL
        return s

    def send(self,peer, msg, dest_mod='lb'):
        msg['module'] = dest_mod
        self.connmgr.channel(self.uuid,peer).send(peer, msg)

    def phase_start(self):
        gap = settings.MESSAGE_DELIVERY_GAP
        rounds = self.ROUNDS
        if self.congested:
            dbg = "{}s: CONGESTED@{}, things are looking nasty.".format(self.sim_time,self.uuid)
            gap = int(gap*settings.CONGESTION_ADJUST)
            rounds = int(rounds/settings.CONGESTION_ADJUST)
        else:
            dbg = "{}s: NORMAL@{}, no congestion right now".format(self.sim_time,self.uuid)
        lb_logging.debug(dbg)
        round = 2*gap
        start = int(self.sim_time*1000)
        r = []
        for i in range(rounds):
            if start != int(self.sim_time*1000):
                r.append(ScheduleCommand(start+0, True, self.load_manage, self))
            r.append(ScheduleCommand(start+gap, True, self.draft_standard, self))
            start += round
        return self.load_manage() + r

    def load_manage(self):
        self.counter += 1
        if self.state == self.DEMAND:
            for peer in self.group:
                if self.uuid == peer:
                    continue
                self.send(peer, {'msg': 'StateChange', 'state': self.DEMAND})
        elif self.state == self.SUPPLY:
            for peer in self.demand:
                if self.uuid == peer:
                    continue
                self.send(peer, {'msg': 'DraftRequest', 'counter': self.counter})
                self.draftage = {}
        return []

    def draft_standard(self):
        if self.state != self.SUPPLY:
            return []
        groupdrafts = [ (k,v) for k,v in self.draftage.iteritems() if k in self.group ]
        for k,v in groupdrafts:
            if abs(v) < STEP_SIZE:
                self.move_to_peerset(k, self.NORMAL)
        try:
            peer, age = min(groupdrafts, key=lambda x: x[1])
            if age <= -STEP_SIZE:
                self.send(peer, {'msg': 'DraftSelect', 'step': STEP_SIZE, 'counter': self.counter})
                self.migrate(-STEP_SIZE)
                self.lost_migration()
        except ValueError:
            # No procs in demand
            pass
        return []

    def move_to_peerset(self, peer, pset):
        self.normal.discard(peer)
        self.supply.discard(peer)
        self.demand.discard(peer)
        if pset == self.DEMAND:
            self.demand.add(peer)
        elif pset == self.SUPPLY:
            self.supply.add(peer)
        else:
            self.normal.add(peer)

    def receive(self, sender, message):
        dbg = "{}s: RECV F: {} T: {} -- {}".format(self.sim_time, sender, self.uuid, message),
        lb_logging.debug(dbg)
        if 'msg' not in message:
            raise KeyError("Bad message {}".format(message))
        if sender not in self.group and message['msg'] != "Peerlist":
            lb_logging.info("Rejected {} from {}: not in group".format(message['msg'], sender))
            return []

        if message['msg'] == 'StateChange':
            self.move_to_peerset(sender, int(message['state']))

        elif message['msg'] == 'DraftRequest':
            self.supply.add(sender)
            if self.state == self.DEMAND:
                diff = self.grid_q + self.power_q
                age = self.power_differential
                self.send(sender, {'msg': 'DraftAge', 'age':age, 'counter': message['counter']})

        elif message['msg'] == 'DraftAge':
            if self.counter == message['counter']:
                self.draftage[sender] = float(message['age'])

        elif message['msg'] == 'DraftSelect':
            amount = message['step']
            if self.state == self.DEMAND:
                self.migrate(amount)
                self.send(sender, {'msg': 'DraftAccept', 'counter': message['counter']})
            else:
                self.send(sender, {'msg': 'TooLate', 'counter': message['counter']})

        elif message['msg'] == 'DraftAccept':
            if self.counter == message['counter']:
                self.found_migration()

        elif message['msg'] == 'TooLate':
            if self.counter == message['counter']:
                self.migrate(STEP_SIZE)
                self.found_migration()

        elif message['msg'] == 'Peerlist':
            self.group = list(message['members'])
            self.group.append(message['leader'])
            self.group.remove(self.uuid)
            self.congested = bool(message['congestion'])
            for l in [self.demand, self.supply, self.normal]:
                r = []
                for peer in l:
                    if peer not in self.group:
                        r.append(peer)
                for peer in r:
                    l.discard(peer)
            for peer in self.group:
                if not any([ peer in l for l in [self.demand, self.supply, self.normal]]):
                    self.normal.add(peer)
        else:
            raise ValueError("Unhandled message type {}".format(message[0]))
        return []

    def schedule(self, start, **kwargs):
        r = []
        r.append(ScheduleCommand(start, True, self.phase_start, self))
        gap = settings.MESSAGE_DELIVERY_GAP
        start += 2*gap*self.ROUNDS
        r.append(start)
        """
        round = 2*gap
        r.append(ScheduleCommand(start, True, self.load_manage, self))
        r.append(ScheduleCommand(start+gap, True, self.draft_standard, self))
        start += round
        """
        return r
