#!/usr/bin/env python

from PIL import Image as im
import numpy as np
import sys

size = (200, 200)
#size = (10, 10)
#size = (39, 39)

f = open(sys.argv[1])

f.next()

limit = 10

for j in f:
    limit -= 1
    if limit <=0:
        break

    r = [float(i)*255 for i in j.split(" ")]
    r = np.clip(r, 0, 255)
    r = list(np.array(r).astype(int))
    
    d = zip(r[0::3], r[1::3], r[2::3])
    
    ii = im.new("RGB", size = size)
    
    ii.putdata(d)
    
    ii = ii.resize((size[0]*5, size[0]*5))

    ii.show()

