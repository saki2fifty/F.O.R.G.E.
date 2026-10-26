// Test-only Linux interposer. Create a competing directory after the real
// owner's last absence check, before whichever publication primitive it uses.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void after_status(const char* path, int result) {
    static unsigned checks;
    const int saved = errno;
    const char* target = getenv("FORGE_PROJECT_RACE_TARGET");
    if (result < 0 && saved == ENOENT && target && strcmp(path, target) == 0 && ++checks == 2) {
        const char* signal = getenv("FORGE_PROJECT_RACE_SIGNAL");
        if (!signal || mkdir(path, 0700))
            _exit(71);
        int file = open(signal, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (file < 0 || write(file, "injected", 8) != 8 || close(file))
            _exit(72);
    }
    errno = saved;
}
int stat(const char* path, struct stat* value) {
    int (*real_status)(const char*, struct stat*) = dlsym(RTLD_NEXT, "stat");
    if (!real_status)
        _exit(73);
    int result = real_status(path, value);
    after_status(path, result);
    return result;
}
int stat64(const char* path, struct stat64* value) {
    int (*real_status)(const char*, struct stat64*) = dlsym(RTLD_NEXT, "stat64");
    if (!real_status)
        _exit(74);
    int result = real_status(path, value);
    after_status(path, result);
    return result;
}
