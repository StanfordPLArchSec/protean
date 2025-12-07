#!/usr/bin/env python3

import argparse

parser = argparse.ArgumentParser()
subparser = parser.add_subparsers(dest="command", required=True)
subparser_clone = subparser.add_parser("clone")
subparser_clone.add_argument("--all", "-a", action="store_true")

repositories = {
    "llvm": {
        "url": 

clone_repository_choices = [
    "llvm",
    "gem5/pincpu",
    "gem5/base",
    "gem5/base-se",
    "gem5/protean",
    "gem5/protean-se",
    "gem5/stt",
    "gem5/spt",
    "gem5/spt-se",
    "amulet",
    "amulet/gem5/protean",
    "amulet/gem5/stt",
    "amulet/gem5/spt",
]

subparser_clone.add_argument("repository", action="append", choices=clone_repository_choices)

args = parser.parse_args()

def handle_clone():
    for repository in args.repository:
        # Does the repository exist?

if args.command == "clone":
    handle_clone()
else:
    assert False
