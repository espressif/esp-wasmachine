# SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import os, subprocess, sys
import typing as t
from pathlib import Path
from idf_build_apps.constants import SUPPORTED_TARGETS

IGNORE_WARNINGS = [
    r'warning: \'nvs_handle\' is deprecated: Replace with nvs_handle_t',
    r'warning: \'ADC_ATTEN_DB_11\' is deprecated',
]

def get_mr_files(modified_files: str) -> str:
    if modified_files is None:
        return ''
    files = []
    modified_files = modified_files.split(' ')
    for f in modified_files:
        files.append(f)
    return files

def get_mr_components(modified_files: str) -> str:
    if modified_files is None:
        return ''
    components = []
    modified_files = modified_files.split(' ')
    for f in modified_files:
        file = Path(f)
        if (
            file.parts[0] == 'components' and
            file.parts[1] in {
                'wasmachine_core',
                'wasmachine_data_sequence',
                'wasmachine_ext_wasm_native',
                'wasmachine_ext_wasm_native_rainmaker',
                'wasmachine_ext_wasm_vfs',
                'wasmachine_shell'
            }
            and 'test_apps' not in file.parts
            and file.parts[-1] != '.build-test-rules.yml'
        ):
            components.append(file.parts[0] + '/' + file.parts[1])

    return components

if __name__ == '__main__':
    modified_files = get_mr_files(os.getenv('MODIFIED_FILES'))
    modified_components = get_mr_components(os.getenv('MODIFIED_FILES'))

    preview_targets = []
    root = '.'

    args = [
        'build',
        # Find args
        '-p',
        root,
        '-t',
        'all',
        '--build-dir',
        'build_@t_@w',
        '--build-log',
        'build_log.txt',
        '--size-file',
        'size.json',
        '--recursive',
        '--check-warnings',
        # Build args
        '--collect-size-info',
        'size_info.txt',
        '--keep-going',
        '--copy-sdkconfig',
        '--config',
        'sdkconfig.ci.*=',
        '=default',
        '-v',
    ]

    args += ['--default-build-targets'] + SUPPORTED_TARGETS + preview_targets + ['--ignore-warning-str'] + IGNORE_WARNINGS

    if modified_components:
        args += ['--modified-components'] + modified_components

    if modified_files:
        args += ['--modified-files'] + modified_files

    manifests = [str(p) for p in Path(root).glob('**/.build-test-rules.yml')]
    if manifests:
        args += ['--manifest-file'] + manifests + ['--manifest-rootpath', root]

    if len(sys.argv) > 1:
        args += sys.argv[1:]

    ret = subprocess.run(['idf-build-apps', *args])
    sys.exit(ret.returncode)
