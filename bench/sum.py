import sys

sums = list()

for line in sys.stdin:
    tokens = line.split()
    for i, token in enumerate(tokens):
        x = int(token)
        if i >= len(sums):
            sums.append(0)
        sums[i] += x

print(*sums)

total = sum(sums)
percents = []
for i in range(len(sums)):
    percents.append('{:.2f}%'.format(sums[i] / total * 100))
print(*percents)
