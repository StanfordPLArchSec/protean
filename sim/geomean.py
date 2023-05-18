import sys
import math

values = []
for path in sys.argv[1:]:
    with open(path) as f:
        values.append(float(f.read()))

geomean = pow(math.prod(values), 1 / len(values))
print(geomean)

