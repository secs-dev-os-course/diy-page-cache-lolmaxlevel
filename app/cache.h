// cache.h
#ifndef CACHE_H
#define CACHE_H

#include <cstddef>  // Для size_t
#include <BaseTsd.h>

#define BLOCK_SIZE 4096
#define CACHE_SIZE 1024  // Максимальное количество блоков в кэше

typedef SSIZE_T ssize_t;

// Объявления функций
int lab2_open(const char* path);
int lab2_close(int fd);
ssize_t lab2_read(int fd, void* buf, size_t count);
ssize_t lab2_write(int fd, const void* buf, size_t count, size_t offset);
int64_t lab2_lseek(int fd, int64_t offset, int whence);
int lab2_fsync(int fd);

#endif // CACHE_H