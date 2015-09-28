import gm
import lb

import random

class DGI(object):
    def __init__(self, uuid, connmgr):
        self.modules = {}
        self.uuid = uuid
        self.connmgr = connmgr
        connmgr.add_peer(uuid)
        self.modules['gm'] = gm.GM(uuid, connmgr)
        self.modules['lb'] = lb.LB(uuid, connmgr)
        self.mod_schedule = {}
        self.sigma = 0
        
    def receive(self, sender, message, **kwargs):
        print message
        if message['module'] == 'all':
            r = []
            for k in self.modules:
                r += self.modules[k].receive(sender, message)
            return r
        else:
            return self.modules[message['module']].receive(sender, message)

    def ecn(self, *args, **kwargs):
        return self.modules['gm'].ecn(*args, **kwargs)

    def sim_time(self, sim_time):
        for k in self.modules:
            self.modules[k].sim_time = sim_time

    def schedule(self, start, rounds, **kwargs):
        r = []
        self.sigma = kwargs['scheduling_sigma']
        for i in range(rounds):
            fiddle = random.gauss(0, self.sigma)
            for k in self.modules:
                for j in range(self.modules[k].ROUNDS):
                    self.mod_schedule[start] = k
                    tmp = self.modules[k].schedule(start, **kwargs)
                    start = tmp.pop()
                    map(lambda x: x.adjust(fiddle), tmp)
                    r += tmp
        return r

    def __repr__(self):
        s = ["DGI {}".format(self.uuid)]
        for k in self.modules:
            s.append("{}: {}".format(k, str(self.modules[k])))
        return "\n".join(s)

    def __str__(self):
        return self.__repr__()
        
    def module_schedule(self):
        times = [ k for k in self.mod_schedule ]
        times.sort()
        return [ (k, self.mod_schedule[k]) for k in times ]
        
