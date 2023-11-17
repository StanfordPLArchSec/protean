import os
import types
import sys

def merge_extensions(cls):
    for key, value in cls.items():
        if key.startswith('+'):
            assert type(value) is list
            basekey = key.removeprefix('+')
            cls[basekey] = list(cls[basekey]) + value
    return dict(filter(lambda p: not p[0].startswith('+'), cls.items()))


def resolve_inheritance(d: dict) -> dict:
    resolved = dict()

    def check_resolved(key) -> bool:
        return 'inherit' not in d[key]

    found_unresolved = True
    while found_unresolved:
        found_unresolved = False
        resolved_one = False
        for key in d:
            if not check_resolved(key):
                super = d[key]['inherit']
                if check_resolved(super):
                    del d[key]['inherit']
                    d[key] = {**d[super], **d[key]}
                    d[key] = merge_extensions(d[key])
                    resolved_one = True
                else:
                    found_unresolved = True
        if found_unresolved and not resolved_one:
            print('fatal error: detected cycle in inheritance', file=sys.stderr)
            exit(1)

    d = dict(filter(lambda p: not p[0].startswith('_'), d.items()))
    # for name, cls in d.items():
    #     for key, value in cls.items():
    #         if key.startswith('+'):
    #             basekey = key.removeprefix('+')
    #             cls[basekey].extend(value)
    #     d[name] = dict(filter(lambda p: not p[0].startswith('+'), cls.items()))

    return d


def prepare_subconfig(config: dict, path_keys, reldir) -> types.SimpleNamespace:
    for key in path_keys:
        if key not in config:
            print(f'config {config} is missing key {key}', file=sys.stderr)
            exit(1)
        if type(config[key]) is list:
            config[key] = [abspath_rel(path, reldir) for path in config[key]]
        elif type(config[key]) is str:
            config[key] = abspath_rel(config[key], reldir)
        else:
            assert False
    config = dict(map(lambda p: (p[0].replace('-', '_'), p[1]), config.items()))
    config = types.SimpleNamespace(**config)
    return config


def process_subconfigs(configs, path_keys, reldir):
    configs = resolve_inheritance(configs)
    for name, config in configs.items():
        config = prepare_subconfig(config, path_keys, reldir)
        configs[name] = config
    return configs


def process_config(config_in: dict, reldir: str) -> types.SimpleNamespace:
    sw_configs = process_subconfigs(config_in['sw'], ['spec2017', 'test-suite', 'cc', 'cxx', 'deps', 'llvm'], reldir)
    sim_configs = process_subconfigs(config_in['sim'], ['src'], reldir)
    hwmode_configs = process_subconfigs(config_in['hwmode'], [], reldir)
    exp_configs = process_subconfigs(config_in['exp'], [], reldir)
    vars = prepare_subconfig(config_in['vars'], ['valgrind', 'simpoint'], reldir)
    return types.SimpleNamespace(sw=sw_configs, sim=sim_configs, hwmode=hwmode_configs, vars=vars, exp=exp_configs)


def abspath_rel(path: str, reldir: str) -> str:
    if path.startswith('/'):
        return path
    return os.path.abspath(os.path.join(reldir, path))
    



def process_benchspec(benchspec: dict) -> dict:
    benchspec = dict(filter(lambda p: 'skip' not in p[1], benchspec.items()))
    for bench_name, bench_spec in benchspec.items():
        bench_spec['exe'] = bench_name
        if 'outputs' not in bench_spec:
            bench_spec['outputs'] = list()
        bench_spec['args'] = bench_spec['args'].split()
        bench_spec['litargs'] = list(bench_spec['args'])
        bench_spec['stdout'] = None
        if '>' in bench_spec['litargs']:
            idx = bench_spec['litargs'].index('>')
            bench_spec['stdout'] = bench_spec['litargs'][idx + 1]
            bench_spec['outputs'].append(bench_spec['stdout'])
            del bench_spec['litargs'][idx:idx+2]

        bench_spec = types.SimpleNamespace(**bench_spec)

        if type(bench_spec.verify) is not list:
            bench_spec.verify = [bench_spec.verify]

        benchspec[bench_name] = bench_spec

    return benchspec
