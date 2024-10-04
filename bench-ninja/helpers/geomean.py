import sys
import math

lines = sys.stdin.readlines()

n = len(lines[0].split()) - 1

output = ['geomean']
for i in range(n):
    l = list()
    for line in lines:
        l.append(float(line.split()[i+1]))
    output.append(f'{pow(math.prod(l), 1 / len(l)) : .4}')
for line in lines:
    print(line, end = '')
print(*output)
