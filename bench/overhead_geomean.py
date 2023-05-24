import sys
import collections
import math

lines = sys.stdin.read().splitlines()

# group by benchmark
benchmarks = collections.defaultdict(list)
for line in lines[1:]:
    benchmarks[line.split('-')[0]].append(float(line.split()[1]))

total = dict()
for name, overheads in benchmarks.items():
    geomean = pow(math.prod(overheads), 1 / len(overheads))
    total[name] = geomean

for name, geomean in total.items():
    print(name, geomean)


print('geomean', pow(math.prod(total.values()), 1 / len(total.values())))
