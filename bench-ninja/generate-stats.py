#!/usr/bin/python3

import argparse
import ninja_syntax

parser = argparse.ArgumentParser()
args = parser.parse_args()

# Ninja
f = open('stats.ninja', 'w')
ninja = ninja_syntax.Writer(f)

# Define rules.
## custom-command -- cmd, id, desc
ninja.rule(
    name = 'custom-command',
    command = '$cmd',
    description = '($id) $desc',
)

# Define builds 
