def observe(obs):
    d = {}
    prev = obs[0]
    for o in obs[1:]:
        if o == 'X':
            prev = 'X'
            continue
        if prev == 'X':
            prev = o
            continue
        try:
            d[(prev,o)] += 1
        except KeyError:
            d[(prev,o)] = 1
        prev = o
    return d

def likely_observe(obs):
    d = {}
    prev = obs[0]
    c = 0
    for o in obs[1:]:
        if o == 'X':
            prev = 'X'
            continue
        if prev == 'X':
            prev = o
            c = 0
            continue
        try:
            d[(c,prev,o)] += 1
        except KeyError:
            d[(c,prev,o)] = 1
        prev = o
        c += 1
    return d

def observe2nd(obs):
    d = {}
    p2 = obs[0]
    p1 = obs[1]
    for o in obs[2:]:
        try:
            d[((p2,p1),o)] += 1
        except KeyError:
            d[((p2,p1),o)] = 1
        p2 = p1
        p1 = o
    return d

def chainify(obs):
    states = []
    statetot = {}
    for k,_ in obs:
        states.append(k)
        statetot[k] = 0
    mkov = {}
    for k,k2 in obs:
        if k == 'X' or k2 == 'X':
            continue
        statetot[k] += obs[(k,k2)]
    for k,k2 in obs:
        if k == 'X' or k2 == 'X':
            continue
        mkov[(k,k2)] = obs[(k,k2)] / (statetot[k] * 1.0)
    return mkov


def pretty(d):
    for k in d:
        print "{} : {}".format(k,d[k])

def applyonce(procs):
    for p in procs:
        p.check()
    readuntilempty(procs)
    for p in procs:
        p.merge()
    readuntilempty(procs)
    for p in procs:
        p.ready()
    readuntilempty(procs)
    for p in procs:
        p.cleanup()

def mkv_state(syst):
    return len(syst[0].group)+1

def sys_state(syst):
    return tuple([ tuple(x.group) for x in syst])

def make_chain_2(procs, prob, applications=10):
    cm = ConnectionManager()
    syst = [ GM(x,cm,p=p) for (x,p) in zip(range(procs),[prob]*procs) ]
    queue = deque([])
    states = set()
    states.add(sys_state(syst))
    queue.append(pickle.dumps(syst))
    observations = []
    c = 0
    while queue:
        c += 1
        if c % 10 == 0:
            print "c={} q={}".format(c,len(queue))
        pick = queue.popleft()
        for _ in range(applications):
            syst = pickle.loads(pick)
            ob = mkv_state(syst)
            observations.append(ob)
            applyonce(syst)
            ob = mkv_state(syst)
            observations.append( ob )
            observations.append('X')
            if sys_state(syst) not in states:
                states.add(sys_state(syst))
                queue.append(pickle.dumps(syst))
    o = observe(observations)
    pretty(o)
    c = chainify(o)
    pretty(c)
    return (o, likely_observe(observations))

def make_chain(procs,prob,sets=1,iters=10000):
    observations = []
    for _ in range(sets):
        cm = ConnectionManager()
        syst = [ GM(x,cm,p=p) for (x,p) in zip(range(procs),[prob]*procs) ]
        observations.append(1)
        for __ in range(iters):
            logging.info(str(syst))
            applyonce(syst)
            ob = len(syst[0].group)+1
            # if ob == 2 and True:
                # with open('scenario.pickle','w+') as cfp:
                    # pickle.dump(syst, cfp)
                # exit()
            observations.append( ob )
        logging.info(str(syst))
        observations.append('X')


    o = observe(observations)
    pretty(o)
    c = chainify(o)
    pretty(c)
    return (o, likely_observe(observations))


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    make_chain(5,0.75)
