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
#include "wm_ext_wasm_native_vfs_ioctl.h"

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

#define WASI_ESUCCESS        (0)
#define WASI_E2BIG           (1)
#define WASI_EACCES          (2)
#define WASI_EADDRINUSE      (3)
#define WASI_EADDRNOTAVAIL   (4)
#define WASI_EAFNOSUPPORT    (5)
#define WASI_EAGAIN          (6)
#define WASI_EALREADY        (7)
#define WASI_EBADF           (8)
#define WASI_EBADMSG         (9)
#define WASI_EBUSY           (10)
#define WASI_ECANCELED       (11)
#define WASI_ECHILD          (12)
#define WASI_ECONNABORTED    (13)
#define WASI_ECONNREFUSED    (14)
#define WASI_ECONNRESET      (15)
#define WASI_EDEADLK         (16)
#define WASI_EDESTADDRREQ    (17)
#define WASI_EDOM            (18)
#define WASI_EDQUOT          (19)
#define WASI_EEXIST          (20)
#define WASI_EFAULT          (21)
#define WASI_EFBIG           (22)
#define WASI_EHOSTUNREACH    (23)
#define WASI_EIDRM           (24)
#define WASI_EILSEQ          (25)
#define WASI_EINPROGRESS     (26)
#define WASI_EINTR           (27)
#define WASI_EINVAL          (28)
#define WASI_EIO             (29)
#define WASI_EISCONN         (30)
#define WASI_EISDIR          (31)
#define WASI_ELOOP           (32)
#define WASI_EMFILE          (33)
#define WASI_EMLINK          (34)
#define WASI_EMSGSIZE        (35)
#define WASI_EMULTIHOP       (36)
#define WASI_ENAMETOOLONG    (37)
#define WASI_ENETDOWN        (38)
#define WASI_ENETRESET       (39)
#define WASI_ENETUNREACH     (40)
#define WASI_ENFILE          (41)
#define WASI_ENOBUFS         (42)
#define WASI_ENODEV          (43)
#define WASI_ENOENT          (44)
#define WASI_ENOEXEC         (45)
#define WASI_ENOLCK          (46)
#define WASI_ENOLINK         (47)
#define WASI_ENOMEM          (48)
#define WASI_ENOMSG          (49)
#define WASI_ENOPROTOOPT     (50)
#define WASI_ENOSPC          (51)
#define WASI_ENOSYS          (52)
#define WASI_ENOTCONN        (53)
#define WASI_ENOTDIR         (54)
#define WASI_ENOTEMPTY       (55)
#define WASI_ENOTRECOVERABLE (56)
#define WASI_ENOTSOCK        (57)
#define WASI_ENOTSUP         (58)
#define WASI_ENOTTY          (59)
#define WASI_ENXIO           (60)
#define WASI_EOVERFLOW       (61)
#define WASI_EOWNERDEAD      (62)
#define WASI_EPERM           (63)
#define WASI_EPIPE           (64)
#define WASI_EPROTO          (65)
#define WASI_EPROTONOSUPPORT (66)
#define WASI_EPROTOTYPE      (67)
#define WASI_ERANGE          (68)
#define WASI_EROFS           (69)
#define WASI_ESPIPE          (70)
#define WASI_ESRCH           (71)
#define WASI_ESTALE          (72)
#define WASI_ETIMEDOUT       (73)
#define WASI_ETXTBSY         (74)
#define WASI_EXDEV           (75)
#define WASI_ENOTCAPABLE     (76)

#define FLAGS_CHECK(v, f)   (((v) & (f)) == (f))

#define X(v) [v] = WASI_##v

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


static uint32_t errno_c2wasm(int error)
{
    static const uint8_t errors[] = {
        X(E2BIG),
        X(EACCES),
        X(EADDRINUSE),
        X(EADDRNOTAVAIL),
        X(EAFNOSUPPORT),
        X(EAGAIN),
        X(EALREADY),
        X(EBADF),
        X(EBADMSG),
        X(EBUSY),
        X(ECANCELED),
        X(ECHILD),
        X(ECONNABORTED),
        X(ECONNREFUSED),
        X(ECONNRESET),
        X(EDEADLK),
        X(EDESTADDRREQ),
        X(EDOM),
        X(EDQUOT),
        X(EEXIST),
        X(EFAULT),
        X(EFBIG),
        X(EHOSTUNREACH),
        X(EIDRM),
        X(EILSEQ),
        X(EINPROGRESS),
        X(EINTR),
        X(EINVAL),
        X(EIO),
        X(EISCONN),
        X(EISDIR),
        X(ELOOP),
        X(EMFILE),
        X(EMLINK),
        X(EMSGSIZE),
        X(EMULTIHOP),
        X(ENAMETOOLONG),
        X(ENETDOWN),
        X(ENETRESET),
        X(ENETUNREACH),
        X(ENFILE),
        X(ENOBUFS),
        X(ENODEV),
        X(ENOENT),
        X(ENOEXEC),
        X(ENOLCK),
        X(ENOLINK),
        X(ENOMEM),
        X(ENOMSG),
        X(ENOPROTOOPT),
        X(ENOSPC),
        X(ENOSYS),
#ifdef ENOTCAPABLE
        X(ENOTCAPABLE),
#endif
        X(ENOTCONN),
        X(ENOTDIR),
        X(ENOTEMPTY),
        X(ENOTRECOVERABLE),
        X(ENOTSOCK),
        X(ENOTSUP),
        X(ENOTTY),
        X(ENXIO),
        X(EOVERFLOW),
        X(EOWNERDEAD),
        X(EPERM),
        X(EPIPE),
        X(EPROTO),
        X(EPROTONOSUPPORT),
        X(EPROTOTYPE),
        X(ERANGE),
        X(EROFS),
        X(ESPIPE),
        X(ESRCH),
        X(ESTALE),
        X(ETIMEDOUT),
        X(ETXTBSY),
        X(EXDEV),
#undef X
#if EOPNOTSUPP != ENOTSUP
        [EOPNOTSUPP] = WASI_ENOTSUP,
#endif
#if EWOULDBLOCK != EAGAIN
        [EWOULDBLOCK] = WASI_EAGAIN,
#endif
    };
    if (error < 0 || (size_t)error >= sizeof(errors) / sizeof(errors[0]) ||
        errors[error] == 0) {
        return WASI_ENOSYS;
    }

    return errors[error];
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

    argv[0] = (uint32_t)errno_c2wasm(c_errno);
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
    int ret;

    switch(cmd) {
#ifdef CONFIG_WASMACHINE_EXT_VFS_GPIO
        case GPIOCSCFG:
            ret = wm_ext_wasm_native_gpio_ioctl(exec_env, fd, cmd, va_args);
            break;
#endif
        default:
            errno = EINVAL;
            ret = -1;
    }

    if (ret < 0) {
        set_wasm_errno(exec_env, errno);
    }

    return ret;
}

static int fstat_wrapper(wasm_exec_env_t exec_env, int fd, struct stat *st)
{
    ESP_LOGW(TAG, "function %s is not supported", __func__);

    return -1;
}

static unsigned long sleep_wrapper(wasm_exec_env_t exec_env, unsigned long s)
{
    ESP_LOGV(TAG, "sleep(%ld)", s);

    return sleep(s);
}

static int usleep_wrapper(wasm_exec_env_t exec_env, unsigned long us)
{
    ESP_LOGV(TAG, "usleep(%ld)", us);

    return usleep(us);
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
    REG_NATIVE_FUNC(fstat,  "(i*)i"),
    REG_NATIVE_FUNC(sleep,  "(i)i"),
    REG_NATIVE_FUNC(usleep, "(i)i")
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
