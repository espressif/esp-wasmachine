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

#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include "bh_platform.h"
#include "wasm_export.h"
#include "wasm_native.h"
#include "wasm_runtime_common.h"

#define WASI_CLOCK_REALTIME           (0)
#define WASI_CLOCK_MONOTONIC          (1)
#define WASI_CLOCK_PROCESS_CPUTIME_ID (2)
#define WASI_CLOCK_THREAD_CPUTIME_ID  (3)

#define WASI_FDFLAG_APPEND   (0x0001)
#define WASI_FDFLAG_DSYNC    (0x0002)
#define WASI_FDFLAG_NONBLOCK (0x0004)
#define WASI_FDFLAG_RSYNC    (0x0008)
#define WASI_FDFLAG_SYNC     (0x0010)

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

#define X(v) [v] = WASI_##v

#define REG_NATIVE_FUNC(func_name, signature) \
    { #func_name, wasi_##func_name, signature, NULL }

#define validate_native_addr(addr, size) \
    wasm_runtime_validate_native_addr(module_inst, addr, size)

#define get_wasi_ctx(module_inst) \
    wasm_runtime_get_wasi_ctx(module_inst)

typedef uint16_t wasi_errno_t;
typedef uint32_t wasi_clockid_t;
typedef uint64_t wasi_timestamp_t;
typedef uint32_t wasi_clockid_t;
typedef uint32_t wasi_fd_t;
typedef uint8_t wasi_filetype_t;
typedef uint16_t wasi_fdflags_t;
typedef uint64_t wasi_rights_t;

typedef struct __wasi_fdstat_t {
    wasi_filetype_t fs_filetype;
    wasi_fdflags_t fs_flags;
    uint8_t __paddings[4];
    wasi_rights_t fs_rights_base;
    wasi_rights_t fs_rights_inheriting;
} wasi_fdstat_t __attribute__((aligned(4)));

typedef struct __wasi_ctx_t {
    char *argv_buf;
    char **argv_list;
    char *env_buf;
    char **env_list;
} *wasi_ctx_t;

typedef struct __iovec_app_t {
    uint32_t buf_offset;
    uint32_t buf_len;
} iovec_app_t;

static bool convert_clockid(wasi_clockid_t in, clockid_t *out)
{
    switch (in) {
        case WASI_CLOCK_MONOTONIC:
            *out = CLOCK_MONOTONIC;
            return true;
#ifdef CLOCK_PROCESS_CPUTIME_ID
        case WASI_CLOCK_PROCESS_CPUTIME_ID:
            *out = CLOCK_PROCESS_CPUTIME_ID;
            return true;
#endif
        case WASI_CLOCK_REALTIME:
            *out = CLOCK_REALTIME;
            return true;
#ifdef CLOCK_THREAD_CPUTIME_ID
        case WASI_CLOCK_THREAD_CPUTIME_ID:
            *out = CLOCK_THREAD_CPUTIME_ID;
            return true;
#endif
        default:
            return false;
    }
}

static wasi_errno_t convert_errno(int error)
{
    static const wasi_errno_t errors[] = {
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

static wasi_timestamp_t convert_timespec(const struct timespec *ts)
{
    wasi_timestamp_t ret;

    if (ts->tv_sec < 0) {
        return 0;
    }

    if ((wasi_timestamp_t)ts->tv_sec >= UINT64_MAX / 1000000000) {
        return UINT64_MAX;
    }

    ret = (wasi_timestamp_t)ts->tv_sec * 1000000000 + (wasi_timestamp_t)ts->tv_nsec;

    return ret;
}

static wasi_errno_t wasmtime_ssp_clock_time_get(wasi_clockid_t clock_id,
                                                wasi_timestamp_t precision,
                                                wasi_timestamp_t *time)
{
    clockid_t nclock_id;
    struct timespec ts;

    if (!convert_clockid(clock_id, &nclock_id)) {
        return WASI_EINVAL;
    }

    if (clock_gettime(nclock_id, &ts) < 0) {
        return convert_errno(errno);
    }

    *time = convert_timespec(&ts);

    return 0;
}

static wasi_errno_t wasmtime_ssp_fd_fdstat_get(wasi_fd_t fd, 
                                               wasi_fdstat_t *buf)
{
    int ret;

    ret = fcntl(fd, F_GETFL);
    if (ret < 0)
        return convert_errno(errno);

    if ((ret & O_APPEND) != 0)
        buf->fs_flags |= WASI_FDFLAG_APPEND;
#ifdef O_DSYNC
    if ((ret & O_DSYNC) != 0)
        buf->fs_flags |= WASI_FDFLAG_DSYNC;
#endif
    if ((ret & O_NONBLOCK) != 0)
        buf->fs_flags |= WASI_FDFLAG_NONBLOCK;
#ifdef O_RSYNC
    if ((ret & O_RSYNC) != 0)
        buf->fs_flags |= WASI_FDFLAG_RSYNC;
#endif
    if ((ret & O_SYNC) != 0)
        buf->fs_flags |= WASI_FDFLAG_SYNC;

    return 0;
}

static wasi_errno_t wasi_clock_time_get(wasm_exec_env_t exec_env,
                                        wasi_clockid_t clock_id,
                                        wasi_timestamp_t precision,
                                        wasi_timestamp_t *time)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (!validate_native_addr(time, sizeof(wasi_timestamp_t)))
        return (wasi_errno_t)-1;

    return wasmtime_ssp_clock_time_get(clock_id, precision, time);
}

static wasi_errno_t wasi_fd_fdstat_get(wasm_exec_env_t exec_env,
                                       wasi_fd_t fd,
                                       wasi_fdstat_t *fdstat_app)
{
    wasi_fdstat_t fdstat;
    wasi_errno_t ret;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (!fdstat_app)
        return (wasi_errno_t)-1;

    if (!validate_native_addr(fdstat_app, sizeof(wasi_fdstat_t)))
        return (wasi_errno_t)-1;

    ret = wasmtime_ssp_fd_fdstat_get(fd, &fdstat);
    if (ret)
        return ret;

    memcpy(fdstat_app, &fdstat, sizeof(wasi_fdstat_t));

    return 0;
}

static wasi_errno_t wasi_fd_write(wasm_exec_env_t exec_env,
                                  wasi_fd_t fd,
                                  const iovec_app_t *iovec_app,
                                  uint32_t iovs_len,
                                  uint32_t *nwritten_app)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    uint64_t total_size;
    uint32_t nwritten = 0;
    uint32_t i;
    wasi_errno_t ret;

    if (!iovec_app || !iovec_app)
        return (wasi_errno_t)-1;

    total_size = sizeof(iovec_app_t) * iovs_len;
    if (!validate_native_addr(nwritten_app, sizeof(uint32_t))
        || total_size >= UINT32_MAX
        || !validate_native_addr((void *)iovec_app, total_size))
        return (wasi_errno_t)-1;

    for (i = 0; i < iovs_len; i++) {
        void *buf;
        int n;
        const iovec_app_t *iov = &iovec_app[i];

        if (!validate_app_addr(iov->buf_offset, iov->buf_len)) {
            return (wasi_errno_t)-1;
        }

        buf = addr_app_to_native(iov->buf_offset);
        n   = iov->buf_len;

        ret = write(fd, buf, n);
        if (ret != n) {
            return convert_errno(errno);;
        }

        nwritten += ret;
    }

    *nwritten_app = (uint32)nwritten;

    return 0;
}

static const NativeSymbol s_esp_native_symbols_libc_wasi[] = {
    REG_NATIVE_FUNC(clock_time_get, "(iI*)i"),
    REG_NATIVE_FUNC(fd_fdstat_get, "(i*)i"),
    REG_NATIVE_FUNC(fd_write, "(i*i*)i"),
};

int esp_libc_wasi_import(void)
{
    NativeSymbol *sym = (NativeSymbol *)s_esp_native_symbols_libc_wasi;
    int num = sizeof(s_esp_native_symbols_libc_wasi) / sizeof(NativeSymbol);

    if (!wasm_native_register_natives("wasi_snapshot_preview1", sym,  num)) {
        return -1;
    }

    return 0;
}
