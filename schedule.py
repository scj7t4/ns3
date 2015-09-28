class ScheduleCommand(object):
    scheduled = {}
    sid_counter = 10000

    @staticmethod
    def new_id():
        ScheduleCommand.sid_counter += 1
        return ScheduleCommand.sid_counter

    @staticmethod
    def get(sid):
        return ScheduleCommand.scheduled[sid]

    @staticmethod
    def put(obj):
        sid = ScheduleCommand.new_id()
        ScheduleCommand.scheduled[sid] = obj
        return sid

    @staticmethod
    def summarize():
        l = ScheduleCommand.scheduled.values()
        l = filter(lambda x: x.when_is_absolute, l)
        l.sort(key=lambda x: x.when)
        r = []
        for obj in l:
            r.append("{}\t{}\t{}".format(obj.when, obj.fn, obj.sid))
        return "\n".join(r)

    def __init__(self, when, when_is_absolute, fn, obj=None):
        self.fn = fn
        self.when = when
        self.when_is_absolute = when_is_absolute
        self.sid = ScheduleCommand.put(self)
        self.obj = obj

    def to_rpc(self):
        cmd = {
            u'action': u'schedule',
            u'when': self.when,
            u'eventid': self.sid,
            u'when_is_absolute': self.when_is_absolute,
        }
        return cmd

    def adjust(self, amount):
        self.when += amount
        
    def do(self):
        return self.fn()
