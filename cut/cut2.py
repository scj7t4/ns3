def score(side):
    s = 0.0
    for k in side:
        s += side[k]
    return s

def cut(left, right):
    closed = set()
    ls = score(left)
    rs = score(right)
    nl = {}
    nr = {}
    for k1 in left:
        v1 = left[k1]
        for k2 in right:
            if k2 in closed:
                continue
            v2 = right[k2]
            newleft = ls-v1+v2
            newright = rs-v2+v1
            print "{} {} -> {} {}".format(k1,k2,newleft,newright)
            if ls < 0 and rs < 0:
                swap = (abs(newleft-newright) < abs(ls-rs))
            else:
                btrleft = newleft > ls
                btrright = newright > rs
                t1 = btrright or (rs >= 0 and newright >=0 and abs(newleft-newright) < abs(ls-rs))
                t2 = btrleft or (ls >= 0 and newleft >=0 and abs(newleft-newright) < abs(ls-rs))
                swap = (btrleft and t1) or (btrright and t2)
            
            
            if swap:
                tmp1 = (k1, v1)
                tmp2 = (k2, v2)
                nl[k2] = v2
                nr[k1] = v1
                closed.add(k2)
                ls = newleft
                rs = newright
                break
        else:
            nl[k1] = left[k1]
    for k2 in right:
        if k2 in closed:
            continue
        nr[k2] = right[k2]
    return nl, nr


def test():
    import random
    left = {}
    right = {}
    for i in range(10):
        left[i] = random.randint(-10,10)
        right[i+10] = random.randint(-10,10)
    print "Init left: {}".format(score(left))
    print left
    print "Init right: {}".format(score(right))
    print right
    l2, r2 = cut(left,right)

    print "Out left: {}".format(score(l2))
    print l2
    print "Out right: {}".format(score(r2))
    print r2

test()
