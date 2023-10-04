import os
import types

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

                    extend_prefix = '+'
                    extended = True
                    while extended:
                        extended = False
                        for key in d:
                            if key.startswith(extend_prefix):
                                assert type(d[key]) is list
                                base_key = key.removeprefix(extend_prefix)
                                assert base_key in d and type(d[base_key]) is list
                                d[base_key].extend(d[key])
                                del d[key]
                                extended = True
                                break
                    
                    resolved_one = True
                else:
                    found_unresolved = True
        if found_unresolved and not resolved_one:
            print('fatal error: detected cycle in inheritance', file=sys.stderr)
            exit(1)

    return dict(filter(lambda p: not p[0].startswith('_'), d.items()))


def prepare_subconfig(config: dict, path_keys, reldir) -> types.SimpleNamespace:
    for key in path_keys:
        if key not in config:
            print(config)
        assert key in config
        config[key] = abspath_rel(config[key], reldir)
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
    sw_configs = process_subconfigs(config_in['sw'], ['spec2017', 'test-suite', 'cc', 'cxx'], reldir)
    sim_configs = process_subconfigs(config_in['sim'], ['src'], reldir)
    hwmode_configs = process_subconfigs(config_in['hwmode'], [], reldir)
    exp_configs = process_subconfigs(config_in['exp'], [], reldir)
    vars = prepare_subconfig(config_in['vars'], ['benchspec', 'llsct'], reldir)
    return types.SimpleNamespace(sw=sw_configs, sim=sim_configs, hwmode=hwmode_configs, vars=vars, exp=exp_configs)


def abspath_rel(path: str, reldir: str) -> str:
    if path.startswith('/'):
        return path
    return os.path.abspath(os.path.join(reldir, path))
    
    
