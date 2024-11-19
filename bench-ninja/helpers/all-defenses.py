#!/usr/bin/python3

defenses = ['unsafe-base']

for sm in ['ctrl', 'atret']:
    for sim in ['stt', 'spt', 'recon']:
        defenses.append(f'{sim}-{sm}')
    for sw in ['base', 'ct', 'cts', 'nst', 'ctd']:
        defenses.append(f'tpt-{sw}-{sm}')

defenses_str = ','.join(defenses)
print(f'{{{defenses_str}}}')
