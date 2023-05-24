import sys

for line in sys.stdin:
    tokens = line.split()
    if len(tokens) == 0:
        continue
    output = [tokens[0]]
    assert len(tokens) >= 2
    ref = float(tokens[1])
    for exp_s in tokens[1:]:
        exp = float(exp_s)
        output.append(f'{ref / exp : .4}')
    print(*output)

