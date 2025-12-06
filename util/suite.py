import json
import re
from util.util import ResultPath

class SuiteBase:
    def __init__(self, name, benches, baseline, protcc):
        self.name = name
        self.benches = benches
        self.baseline = baseline
        self.protcc = protcc
        for bench in self.benches:
            bench.suite = self

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
        j = json.loads(ResultPath(target).read_text())
        return j["stats"]["cycles"]

class Suite(CheckpointSuite):
    pass
    
class EndToEndSuite(SuiteBase):
    def perf(self, target):
        l = []
        with ResultPath(target).with_name("stats.txt").open() as f:
            for line in f:
                if m := re.match(r"simSeconds\s+([0-9.]+)", line):
                    l.append(float(m.group(1)))
        assert len(l) >= 1
        return l[-1]

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
