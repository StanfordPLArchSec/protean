import json
import re
from util.util import (
    json_cycles,
    stats_seconds,
    geomean,
)

class SuiteBase:
    def __init__(self, name, benches, baseline, protcc):
        self.name = name
        self.benches = benches
        self.baseline = baseline
        self.protcc = protcc
        for bench in self.benches:
            bench.suite = self

    def geomean(self):
        # If there is a missing number, then bail.
        if any([None in bench.results for bench in self.benches]):
            return None
        ls = [[], [], []]
        for bench in self.benches:
            for l, result in zip(ls, bench.results):
                l.append(result)
        return [geomean(l) for l in ls]

class CheckpointSuite(SuiteBase):
    def __init__(self, name, benches, baseline, protcc, group):
        super().__init__(name, benches, baseline, protcc)
        self.group = group

    def _target(self, bench, bin, hwconf):
        return (
            f"{bench.target_name}/exp/0/{self.group}/{bin}/"
            f"{hwconf}.pcore/results.json"
        )

    def perf(self, target):
        return json_cycles(target)

class Suite(CheckpointSuite):
    pass
    
class EndToEndSuite(SuiteBase):
    def perf(self, target):
        return stats_seconds(target)

class WebserverSuite(EndToEndSuite):
    def _target(self, bench, bin, hwconf):
        hwconf_l = hwconf.split(".")
        hwconf_l.insert(1, "se")
        hwconf = ".".join(hwconf_l)
        return (
            f"webserv/exp/{bench.target_name}/{bin}/{hwconf}/stamp.txt"
        )

class ParsecSuite(EndToEndSuite):
    def _target(self, bench, bin, hwconf):
        return (
            f"parsec/pkgs/{bench.target_name}/{bin}/{hwconf}/stamp.txt"
        )
