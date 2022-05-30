// Copyright 2022 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <unistd.h>
#include <fcntl.h>
#include <sys/errno.h>

#include "esp_log.h"

#include "bh_platform.h"
#include "wasm_export.h"
#include "wasm_native.h"
#include "wasm_runtime_common.h"

#include "wm_ext_wasm_native_macro.h"
#include "wm_ext_wasm_native_export.h"

#define WASM_O_APPEND       (1 << 0)
#define WASM_O_NONBLOCK     (1 << 2)
#define WASM_O_SYNC         (1 << 4)
#define WASM_O_CREAT        (1 << 12)
#define WASM_O_DIRECTORY    (1 << 13)
#define WASM_O_EXCL         (1 << 14)
#define WASM_O_TRUNC        (1 << 15)
#define WASM_O_NOFOLLOW     (1 << 24)
#define WASM_O_RDONLY       (1 << 26)
#define WASM_O_WRONLY       (1 << 28)

#define WASM_O_RDWR         (WASM_O_RDONLY | WASM_O_WRONLY)

#define FLAGS_CHECK(v, f)   (((v) & (f)) == (f))

static const char *TAG = "wm_libc_wrapper";

static int flags_wasm2c(int wasm_flags)
{
    int gcc_flags = 0;

    if (FLAGS_CHECK(wasm_flags, WASM_O_RDWR)) {
        gcc_flags |= O_RDWR;
    } else if (FLAGS_CHECK(wasm_flags, WASM_O_RDONLY)) {
        gcc_flags |= O_RDONLY;
    } else if (FLAGS_CHECK(wasm_flags, WASM_O_WRONLY)) {
        gcc_flags |= O_WRONLY;
    }

    if (FLAGS_CHECK(wasm_flags, WASM_O_APPEND)) {
        gcc_flags |= O_APPEND;
    }

    if (FLAGS_CHECK(wasm_flags, WASM_O_CREAT)) {
        gcc_flags |= O_CREAT;
    }

    if (FLAGS_CHECK(wasm_flags, WASM_O_DIRECTORY)) {
        gcc_flags |= O_DIRECTORY;
    }

    if (FLAGS_CHECK(wasm_flags, WASM_O_EXCL)) {
        gcc_flags |= O_EXCL;
    }

    if (FLAGS_CHECK(wasm_flags, WASM_O_SYNC)) {
        gcc_flags |= O_SYNC;
    }

    if (FLAGS_CHECK(wasm_flags, WASM_O_NONBLOCK)) {
        gcc_flags |= O_NONBLOCK;
    }

    if (FLAGS_CHECK(wasm_flags, WASM_O_TRUNC)) {
        gcc_flags |= O_TRUNC;
    }

    if (FLAGS_CHECK(wasm_flags, WASM_O_APPEND)) {
        gcc_flags |= O_APPEND;
    }

    return gcc_flags;
}

static void set_wasm_errno(wasm_exec_env_t exec_env, int c_errno)
{
    uint32_t argv[1];
    WASMFunctionInstanceCommon *func;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    const char *func_name = "libc_builtin_set_errno";

    func = wasm_runtime_lookup_function(module_inst, func_name, "(i32)");
    if (!func) {
        ESP_LOGW(TAG, "failed to find function %s", func_name);
        return ;
    }

    argv[0] = (uint32_t)c_errno;
    if (!wasm_runtime_call_wasm(exec_env, func, 1, argv)) {
        ESP_LOGE(TAG, "failed to call function %s", func_name);
        return ;
    }
}

static int open_wrapper(wasm_exec_env_t exec_env,
                        const char *pathname,
                        int flags,
                        int mode)
{
    int ret;
    int gcc_flags = flags_wasm2c(flags);

    ESP_LOGV(TAG, "open(%s, %x(%x), %x)", pathname, flags, gcc_flags, mode);

    ret = open(pathname, gcc_flags, mode);
    if (ret < 0) {
        set_wasm_errno(exec_env, errno);
    }

    ESP_LOGV(TAG, "open(%s, %x(%x), %x)=%d", pathname, flags, gcc_flags, mode, ret);

    return ret;
}

static ssize_t read_wrapper(wasm_exec_env_t exec_env, int fd, void *buffer, size_t n) 
{
    int ret;

    ESP_LOGV(TAG, "read(%d, %p, %d)", fd, buffer, n);

    ret = read(fd, buffer, n);
    if (ret < 0) {
        set_wasm_errno(exec_env, errno);
    }

    ESP_LOGV(TAG, "read(%d, %p, %d)=%d", fd, buffer, n, ret);

    return ret;
}

static ssize_t write_wrapper(wasm_exec_env_t exec_env, int fd, const void *buffer, size_t n) 
{
    int ret;

    ESP_LOGV(TAG, "write(%d, %p, %d)", fd, buffer, n);

    ret = write(fd, buffer, n);
    if (ret < 0) {
        set_wasm_errno(exec_env, errno);
    }

    ESP_LOGV(TAG, "write(%d, %p, %d)=%d", fd, buffer, n, ret);

    return ret;
}

static ssize_t pread_wrapper(wasm_exec_env_t exec_env, int fd, void *dst, size_t size, off_t offset)
{
    int ret;

    ESP_LOGV(TAG, "pread(%d, %p, %u, %lu)", fd, dst, size, offset);

    ret = pread(fd, dst, size, offset);
    if (ret < 0) {
        set_wasm_errno(exec_env, errno);
    }

    ESP_LOGV(TAG, "pread(%d, %p, %u, %lu)=%d", fd, dst, size, offset, ret);

    return ret;
}

static ssize_t pwrite_wrapper(wasm_exec_env_t exec_env, int fd, const void *dst, size_t size, off_t offset)
{
    int ret;

    ESP_LOGV(TAG, "pwrite(%d, %p, %u, %lu)", fd, dst, size, offset);

    ret = pwrite(fd, dst, size, offset);
    if (ret < 0) {
        set_wasm_errno(exec_env, errno);
    }

    ESP_LOGV(TAG, "pwrite(%d, %p, %u, %lu)=%d", fd, dst, size, offset, ret);

    return ret;
}

static off_t lseek_wrapper(wasm_exec_env_t exec_env, int fd, int64_t offset, int whence)
{
    int ret;

    ESP_LOGV(TAG, "lseek(%d, %llx, %x)", fd, offset, whence);

    ret = lseek(fd, (off_t)offset, whence);
    if (ret < 0) {
        set_wasm_errno(exec_env, errno);
    }

    ESP_LOGV(TAG, "lseek(%d, %llx, %x)=%d", fd, offset, whence, ret);

    return (int64_t)ret;    
}

static int fcntl_wrapper(wasm_exec_env_t exec_env, int fd, int cmd, int arg)
{
    int ret;

    ESP_LOGV(TAG, "fcntl(%d, %x, %x)", fd, cmd, arg);

    ret = fcntl(fd, cmd, arg);
    if (ret < 0) {
        set_wasm_errno(exec_env, errno);
    }

    ESP_LOGV(TAG, "fcntl(%d, %x, %x)=%d", fd, cmd, arg, ret);

    return ret;
}

static int fsync_wrapper(wasm_exec_env_t exec_env, int fd)
{
    int ret;

    ESP_LOGV(TAG, "fsync(%d)", fd);

    ret = fsync(fd);
    if (ret < 0) {
        set_wasm_errno(exec_env, errno);
    }

    ESP_LOGV(TAG, "fsync(%d)=%d", fd, ret);

    return ret;
}

static int close_wrapper(wasm_exec_env_t exec_env, int fd)
{
    int ret;

    ESP_LOGV(TAG, "close(%d)", fd);

    ret = close(fd);
    if (ret < 0) {
        set_wasm_errno(exec_env, errno);
    }

    ESP_LOGV(TAG, "close(%d)=%d", fd, ret);

    return ret;
}

static int ioctl_wrapper(wasm_exec_env_t exec_env, int fd, int cmd, char *va_args)
{
    ESP_LOGW(TAG, "function %s is not supported", __func__);

    return -1;
}

static int fstat_wrapper(int fd, struct stat *st)
{
    ESP_LOGW(TAG, "function %s is not supported", __func__);

    return -1;
}

static NativeSymbol wm_libc_wrapper_native_symbol[] = {
    REG_NATIVE_FUNC(open,   "($ii)i"),
    REG_NATIVE_FUNC(read,   "(i*~)i"),
    REG_NATIVE_FUNC(write,  "(i*~)i"),
    REG_NATIVE_FUNC(pread,  "(i*~i)i"),
    REG_NATIVE_FUNC(pwrite, "(i*~i)i"),
    REG_NATIVE_FUNC(lseek,  "(iIi)I"),
    REG_NATIVE_FUNC(fcntl,  "(iii)i"),
    REG_NATIVE_FUNC(fsync,  "(i)i"),
    REG_NATIVE_FUNC(close,  "(i)i"),
    REG_NATIVE_FUNC(ioctl,  "(ii*)i"),
    REG_NATIVE_FUNC(fstat,  "(i*)i")
};

int wm_ext_wasm_native_libc_export(void)
{
    NativeSymbol *sym = (NativeSymbol *)wm_libc_wrapper_native_symbol;
    int num = sizeof(wm_libc_wrapper_native_symbol) / sizeof(wm_libc_wrapper_native_symbol[0]);

    if (!wasm_native_register_natives("env", sym,  num)) {
        return -1;
    }

    return 0;
}
