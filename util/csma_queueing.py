import sys

class CSMATraceEntry(object):
    def __init__(self, inp):
        """
        + 0.984 /NodeList/9/DeviceList/0/$ns3::CsmaNetDevice/TxQueue/Enqueue ns3::EthernetHeader ( length/type=0x800, source=00:00:00:00:00:1b, destination=00:00:00:00:00:15) ns3::Ipv4Header (tos 0x0 DSCP Default ECN Not-ECT ttl 64 id 0 protocol 17 offset (bytes) 0 flags [none] length: 72 10.1.2.4 > 10.1.1.4) ns3::UdpHeader (length: 52 9 > 9) Payload (size=44) ns3::EthernetTrailer (fcs=0)
        """
        tokens = inp.split()
        self.event_type = tokens[0]
        self.time = float(tokens[1])
        self.location = NS3TracePath(tokens[2])
        # I'll add more later... don't care right now.

class NS3TracePath(object):
    def __init__(self, path):
        parts = path.split("/")
        assert parts[0] == ""
        parts = parts[1:]
        self.trace = parts.pop()
        typing = filter(lambda x: x.find("$ns3::") != -1, parts)
        parts = filter(lambda x: x.find("$ns3::") == -1, parts)
        self.typing = typing[-1].replace("$ns3::","") if typing else None
        self.attr = None
        while parts:
            comp = parts.pop(0)
            if comp == "NodeList":
                self.node = int(parts.pop(0))
            elif comp == "DeviceList":
                self.device = int(parts.pop(0))
            else:
                self.attr = comp
    
    def __repr__(self):
        return "TP: Node {} Device {} as {}, attr {} trace {}".format(
            self.node, self.device, self.typing, self.attr, self.trace)
    def __str__(self):
        return self.__repr__()
    def askey(self):
        if self.attr:
            return "N{}-D{}-{}".format(self.node, self.device, self.attr) 
        else:
            return "N{}-D{}".format(self.node, self.device)

def dataset_queue_size(entries):
    queue_tracker = {}
    data_points = {}
    drop_events = {}
    for e in entries:
        loc = e.location.askey()
        t = e.time
        
        if e.event_type == "r":
            # We don't care about this.
            continue

        if loc not in queue_tracker:
            queue_tracker[loc] = 0
            data_points[loc] = []
            drop_events[loc] = []
        
        qc = True
        if e.event_type == "+":
            queue_tracker[loc] += 1
        elif e.event_type == "-":
            queue_tracker[loc] -= 1
        else:
            qc = False
        if qc:
            data_points[loc].append((t, queue_tracker[loc]))

        if e.event_type == "d":
            drop_events[loc].append((t, queue_tracker[loc]))
    
    for loc in queue_tracker:
        if len(queue_tracker) == 0:
            continue
        safename = loc
        with open("queue-{}.dat".format(safename), "w+") as fp:
            for t,v in data_points[loc]:
                fp.write("{}\t{}\n".format(t,v))
        with open("drops-{}.dat".format(safename), "w+") as fp:
            for t,v in drop_events[loc]:
                fp.write("{}\t{}\n".format(t,v))
def do(path):
    with open(path) as fp:
        contents = list(fp)
    entries = map(CSMATraceEntry, contents)
    dataset_queue_size(entries)

def main():
    do(sys.argv[1])

if __name__ == "__main__":
    main()
