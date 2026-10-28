#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Exercise the ESP-native C REST sections with deterministic SDK stubs."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
IDF = Path(os.environ['IDF_PATH'])
OT = IDF / 'components/openthread/openthread/include'


def run(args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def functions(node, result):
    if node.get('kind') == 'FunctionDecl' and node.get('name', '').startswith('ot'):
        result[node['name']] = node
    for child in node.get('inner', []):
        functions(child, result)


def main():
    api = (ROOT / 'src/esp_br_web_api.c').read_text()
    web = (ROOT / 'src/esp_br_web.c').read_text()
    base = (ROOT / 'src/esp_br_web_base.c').read_text()
    header = (ROOT / 'private_include/esp_br_web_api.h').read_text()
    state = api[api.index('static const char s_ot_state'):api.index('/*------', api.index('static const char s_ot_state'))]
    epskc = api[api.index('static const char *epskc_state_to_string'):api.index('/**', api.index('static const char *epskc_state_to_string'))]
    api = state + epskc + api[api.index('#define THREAD_REST_COLLECTION_LIMIT'):]
    web = web[web.index('#define THREAD_REST_MAX_BODY'):web.index('/*-----------------------------------------------------\nNote: Http Server WEB-GUI API')]
    base = base[base.index('cJSON *Timestamp2Json'):base.index('                       Set Thread dataset')]
    base = base[:base.rfind('/*------')]
    header = header[header.index('/** Internal Thread REST contract'):header.index('/*---------------------------------------------------------------------\n            ESP Thread Border Router Wer Server REST API')]
    headers = set(re.findall(r'#include [<"](openthread/[^">]+)[">]', (ROOT / 'tests/host_platform.h').read_text()))
    cjson = IDF / 'components/json/cJSON'
    if not cjson.exists():
        cjson = next((ROOT.parents[1] / 'examples/basic_thread_border_router/managed_components').glob('*cjson/cJSON'))
    with tempfile.TemporaryDirectory(prefix='esp-native-rest-tests-') as directory:
        directory = Path(directory)
        declarations = directory / 'declarations.cpp'
        declarations.write_text(''.join(f'#include <{h}>\n' for h in sorted(headers)))
        ast_path = directory / 'ast.json'
        with ast_path.open('w') as output:
            run(['clang++', '-I', OT, '-Xclang', '-ast-dump=json', '-fsyntax-only', declarations], stdout=output)
        table = {}
        functions(json.loads(ast_path.read_text()), table)
        used = set(re.findall(r'\bot[A-Z]\w*\b', api + web + base))
        mocks = '#include "host_platform.h"\n#include <string.h>\n'
        mocks += (ROOT / 'tests/openthread_mock.inc').read_text()
        custom = json.loads((ROOT / 'tests/mock_functions.json').read_text())
        for name in sorted(used & table.keys()):
            node = table[name]
            result = node['type']['qualType'].split('(', 1)[0].strip()
            params = [c for c in node.get('inner', []) if c.get('kind') == 'ParmVarDecl']  # codespell:ignore ParmVarDecl
            signature = ', '.join(f'{p["type"]["qualType"]} p{i}' for i, p in enumerate(params))
            if name in custom:
                body = custom[name]
            elif result == 'void':
                body = ''
            elif result == 'otError':
                body = 'return OT_ERROR_NOT_FOUND;'
            elif '*' in result:
                pointee = result.replace('*', '').replace('const', '').strip()
                body = 'return NULL;' if pointee.startswith('otMeshDiag') or pointee in {'otMessage', 'otInstance'} else f'static {pointee} value; return &value;'
            else:
                body = 'return 0;'
            mocks += f'{result} {name}({signature}) {{ {body} }}\n'
        (directory / 'openthread_mock.c').write_text(mocks)
        (directory / 'rest_api.inc').write_text(header + '\n' + base + '\n' + api)
        (directory / 'rest_web.inc').write_text(web)
        flags = ['-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                 '-I', ROOT / 'tests', '-I', directory, '-I', OT, '-I', cjson]
        binary = directory / 'test_rest'
        run(['gcc', *flags, ROOT / 'tests/test_rest.c', ROOT / 'tests/host_platform.c',
             directory / 'openthread_mock.c', cjson / 'cJSON.c', '-lm', '-o', binary])
        run([binary])


if __name__ == '__main__':
    main()
