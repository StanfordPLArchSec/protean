import json
import re

class Bench:
    def __init__(self, name, target):
        self.name = name
        self.target_name = target
        self.results = [None] * 3

    def _target(self, bin, hwconf):
        return self.suite._target(self, bin, hwconf)

    def baseline(self):
        return self.suite.baseline

    def protcc(self):
        return self.suite.protcc

    def target_unsafe(self):
        return self._target("base", "unsafe")

    def target_baseline(self):
        return self._target("base", self.baseline())

    def target_prottrack(self):
        return self._target(self.protcc(), "prottrack.atret")

    def target_protdelay(self):
        return self._target(self.protcc(), "protdelay.atret")

    def targets(self):
        return [
            self.target_unsafe(),
            self.target_baseline(),
            self.target_prottrack(),
            self.target_protdelay(),
        ]

    def perf(self, target):
        return self.suite.perf(target)

    def perf_unsafe(self):
        return self.perf(self.target_unsafe())

    def perf_baseline(self):
        return self.perf(self.target_baseline())

    def perf_prottrack(self):
        return self.perf(self.target_prottrack())

    def perf_protdelay(self):
        return self.perf(self.target_protdelay())
