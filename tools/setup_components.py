#!/usr/bin/env python3
#
# Copyright 2022 Espressif Systems (Shanghai) PTE LTD
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import subprocess

LVGL = { 'name': 'lvgl',
         'url': 'https://github.com/lvgl/lvgl.git',
         'path': 'components/lvgl',
         'branch': 'v8.1.0',
         'patch': True }

RAINMAKER = { 'name': 'esp-rainmaker',
              'url': 'https://github.com/espressif/esp-rainmaker.git', 
              'path': 'components/esp-rainmaker',
              'branch': 'master',
              'commit_id': 'fa00c1b0',
              'patch': False }

WAMR = { 'name': 'wasm-micro-runtime',
         'url': 'https://github.com/bytecodealliance/wasm-micro-runtime.git', 
         'path': 'components/wamr/wasm-micro-runtime',
         'branch': 'fast-jit-06-29-2022',
         'patch': True }

ESP_BOX = { 'name': 'esp-bsp',
            'url': 'https://github.com/espressif/esp-bsp.git',
            'path': 'components/esp-bsp',
            'branch': 'master',
            'commit_id': '2fe2ed4a',
            'patch': True }

COMPONENTS = [ LVGL, RAINMAKER, WAMR ]

ESP_IDF = { 'name': 'esp-idf',
            'url': 'https://github.com/espressif/esp-idf.git',
            'path': os.environ['IDF_PATH'],
            'branch': 'v5.0.5',
            'patch': False }

ROOT_PATH=os.getcwd()

def run(cmd):
    p = subprocess.Popen(cmd,
                         shell=True,
                         stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)

    ret_dsc, ret_err = p.communicate()
    ret_code = p.returncode

    return ret_code, ret_dsc, ret_err

def repo_version(path):
    cmd = 'cd %s && git tag'%(path, )
    ret, dsc, err = run(cmd)
    if len(dsc) == 0:
        cmd = 'cd %s && git branch --show-current'%(path, )
        ret, dsc, err = run(cmd)
    dsc = bytes.decode(dsc)
    dsc = dsc.rstrip('\n')
    return dsc

def clone_repo(name, url, path, branch, commit_id=None, patch=False):
    if os.path.exists(path):
        print('destination path \'%s\' exists and skip this process'%(path))
        return

    print('clone \'%s\' branch \'%s\' into \'%s\''%(url, branch, path))
    if commit_id == None:
        cmd = 'git clone --recursive --branch %s --depth 1 %s %s'%(branch, url, path)
    else:
        cmd = 'git clone --recursive --branch %s %s %s'%(branch, url, path)
    run(cmd)
    
    if commit_id != None:
        print('checkout \'%s\' to commit id \'%s\''%(path, commit_id))
        cmd = 'cd %s && git checkout %s'%(path, commit_id)
        run(cmd)
    
    if patch == True:
        print('patch \'%s\''%(path, ))
        cmd = 'cd %s && git apply %s/%s/%s.patch'%(path, ROOT_PATH, 'tools/patch', name)
        run(cmd)

def patch_components():
    for c in COMPONENTS:
        commit_id = None
        if 'commit_id' in c:
            commit_id = c['commit_id']

        if 'patch' in c:
            patch = c['patch']
        else:
            patch = False

        clone_repo(c['name'], c['url'], c['path'], c['branch'], commit_id, patch)

def patch_esp_idf():
    if 'patch' in ESP_IDF and ESP_IDF['patch'] == True:
        print('patch \'esp-idf\'')

        cmd = 'cd %s && git apply %s/%s'%(ESP_IDF['path'], ROOT_PATH, 'tools/patch/esp-idf.patch')
        ret, dsc, err = run(cmd)
        if ret != 0:
            print('patch esp-idf failed and skip this process')

def main():
    patch_components()
    patch_esp_idf()

def _main():
    try:
        main()
    except FatalError as e:
        print('\nA fatal error occurred: %s' % e)
        sys.exit(2)

if __name__ == '__main__':
    _main()
