#!/usr/bin/env python

from PIL import Image as im
import numpy as np
import sys
import time

#size(200, 200)
size = (10, 10)

if len(sys.argv) != 3:
    print sys.argv[0], "<rbm-file> <image-prefix>"
    print "  Expecting size", size
    exit(1)

f = open(sys.argv[1])
prefix = sys.argv[2]

while True:
    z = f.next()
    if z.startswith("weights"):
        break

m = []
for j in f:
    try:
        r = [float(i) for i in j.split(" ")]
    except:
        break

    m.append(r)

m = np.array(m)
m = m.transpose()

#mi = np.min(m)
#ma = np.max(m)
#m = np.clip(255.0*(m-mi)/(ma-mi), 0, 255).astype(int)

limit = 100

for i in xrange(min(limit, m.shape[0])):

    r = m[i]

    mi = np.min(r)
    ma = np.max(r)
    r = np.clip(255.0*(r-mi)/(ma-mi), 0, 255).astype(int)

    r = list(r)
    d = zip(r[0::3], r[1::3], r[2::3])
    
    ii = im.new("RGB", size = size)
    
    ii.putdata(d)
    
    ii = ii.resize((100, 100))

    ii.save(prefix + "-" + str(i).zfill(2) + ".jpg")
    #ii.show()
    #time.sleep(0.1)
