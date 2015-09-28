import random
import itertools

N = 15
M = 15

alpha = 6
beta = 1

def configurations(n_side, m_side):
    l = n_side + m_side
    l.sort()
    p0 = l.pop(0)
    l = set(l)
    
    s = itertools.combinations(l, N-1)
    
    for g in s:
        g1 = set(list(g) + [p0])
        g2 = l - g1
        yield (g1, g2)

for x in configurations(range(N), range(N,N+M)):
    print x

def score_group(group, n_side, m_side, pvs):
    l = min(group)
    l_side, o_side = (n_side, m_side) if l in n_side else (m_side, n_side)

    g = filter(lambda x: x != l, group)

    score = beta * pvs[l]
    for proc in g:
        uses_router = proc in o_side 
        score += alpha * uses_router + beta * pvs[proc]
    
    return score

def main():
    all_procs = range(N+M)
    random.shuffle(all_procs)
    n_side = all_procs[:N]
    m_side = all_procs[:M]
    powervalues = dict([ (i,random.randint(-30,30)) for i in range(N+M) ])
    
