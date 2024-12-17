#include <windows.h>
#include <unordered_map>
#include <list>
#include <iostream>
#include "cache.h"

// Logging flag
bool g_loggingEnabledCache = true;

// Logging macros
#define LOG(x) if (g_loggingEnabledCache) std::cout << x << std::endl;
#define LOG_ERROR(x) if (g_loggingEnabledCache) std::cerr << x << std::endl;

const size_t PAGE_SIZE = 4096;
const size_t NUM_OF_BLOCKS = 100;

class BlockCache {
public:
    static BlockCache &getInstance(size_t cacheSize = NUM_OF_BLOCKS) {
        static BlockCache instance(cacheSize);
        return instance;
    }

    bool read(int fd, void *buf, size_t count, size_t offset);

    bool write(int fd, const void *buf, size_t count, size_t offset);

    void sync(int fd);

    void close(int fd);

private:
    BlockCache(size_t cacheSize) : cacheSize(cacheSize), alignedBuffer(_aligned_malloc(PAGE_SIZE, PAGE_SIZE)) {
        if (!alignedBuffer) {
            throw std::bad_alloc();
        }
    }

    ~BlockCache() {
        if (alignedBuffer) {
            _aligned_free(alignedBuffer);
        }
    }

    struct CacheBlock {
        void *data;
        int fd;
        size_t offset;
        bool dirty;

        CacheBlock() : data(_aligned_malloc(PAGE_SIZE, PAGE_SIZE)), fd(-1), offset(0), dirty(false) {}
    };

    size_t cacheSize;
    std::list<CacheBlock> cache;
    std::unordered_map<int, std::unordered_map<size_t, std::list<CacheBlock>::iterator>> cacheMap;
    void *alignedBuffer;

    void evict();

    void writeBlockToDisk(const CacheBlock &block);
};

bool BlockCache::read(int fd, void *buf, size_t count, size_t offset) {
    LOG("[BlockCache::read] Reading " << count << " bytes from fd: " << fd << " at offset: " << offset)
    size_t alignedOffset = (offset / PAGE_SIZE) * PAGE_SIZE;
    auto fdIt = cacheMap.find(fd);
    if (fdIt != cacheMap.end()) {
        auto offsetIt = fdIt->second.find(alignedOffset);
        if (offsetIt != fdIt->second.end()) {
            LOG("[BlockCache::read] Cache hit for fd: " << fd << " at offset: " << alignedOffset)
            memcpy(buf, static_cast<char*>(offsetIt->second->data) + (offset - alignedOffset), count);
            return true;
        }
    }

    LOG("[BlockCache::read] Cache miss for fd: " << fd << " at offset: " << alignedOffset)
    HANDLE handle = reinterpret_cast<HANDLE>(fd);
    LARGE_INTEGER li;
    li.QuadPart = alignedOffset;
    SetFilePointerEx(handle, li, NULL, FILE_BEGIN);

    DWORD bytesRead = 0;
    if (!ReadFile(handle, alignedBuffer, PAGE_SIZE, &bytesRead, NULL)) {
        LOG_ERROR("[BlockCache::read] Failed to read from file.")
        return false;
    }

    memcpy(buf, static_cast<char*>(alignedBuffer) + (offset - alignedOffset), count);

    if (cache.size() >= cacheSize) evict();
    CacheBlock newBlock;
    memcpy(newBlock.data, alignedBuffer, PAGE_SIZE);
    newBlock.fd = fd;
    newBlock.offset = alignedOffset;
    newBlock.dirty = false;
    cache.push_front(newBlock);
    cacheMap[fd][alignedOffset] = cache.begin();

    return true;
}

bool BlockCache::write(int fd, const void *buf, size_t count, size_t offset) {
    LOG("[BlockCache::write] Writing " << count << " bytes to fd: " << fd << " at offset: " << offset)
    size_t alignedOffset = (offset / PAGE_SIZE) * PAGE_SIZE;
    auto fdIt = cacheMap.find(fd);
    if (fdIt != cacheMap.end()) {
        auto offsetIt = fdIt->second.find(alignedOffset);
        if (offsetIt != fdIt->second.end()) {
            LOG("[BlockCache::write] Cache hit for fd: " << fd << " at offset: " << alignedOffset)
            memcpy(static_cast<char*>(offsetIt->second->data) + (offset - alignedOffset), buf, count);
            offsetIt->second->dirty = true;
            return true;
        }
    }

    LOG("[BlockCache::write] Cache miss for fd: " << fd << " at offset: " << alignedOffset)
    HANDLE handle = reinterpret_cast<HANDLE>(fd);
    LARGE_INTEGER li;
    li.QuadPart = alignedOffset;
    SetFilePointerEx(handle, li, NULL, FILE_BEGIN);
    DWORD bytesRead = 0;
    if (!ReadFile(handle, alignedBuffer, PAGE_SIZE, &bytesRead, NULL)) {
        LOG_ERROR("[BlockCache::write] Failed to read from file.")
        return false;
    }

    memcpy(static_cast<char*>(alignedBuffer) + (offset - alignedOffset), buf, count);

    if (cache.size() >= cacheSize) evict();
    CacheBlock newBlock;
    memcpy(newBlock.data, alignedBuffer, PAGE_SIZE);
    newBlock.fd = fd;
    newBlock.offset = alignedOffset;
    newBlock.dirty = true;
    cache.push_front(newBlock);
    cacheMap[fd][alignedOffset] = cache.begin();

    return true;
}

void BlockCache::sync(int fd) {
    LOG("[BlockCache::sync] Syncing cache for fd: " << fd)
    auto fdIt = cacheMap.find(fd);
    if (fdIt != cacheMap.end()) {
        for (auto &offsetIt : fdIt->second) {
            if (offsetIt.second->dirty) {
                LOG("[BlockCache::sync] Writing dirty block to disk for fd: " << fd << " at offset: " << offsetIt.first)
                writeBlockToDisk(*offsetIt.second);
                offsetIt.second->dirty = false;
            }
            _aligned_free(offsetIt.second->data);
            cache.erase(offsetIt.second);
        }
        cacheMap.erase(fdIt);
    }
}

void BlockCache::close(int fd) {
    LOG("[BlockCache::close] Closing cache for fd: " << fd)
    sync(fd);
}

void BlockCache::evict() {
    LOG("[BlockCache::evict] Evicting least frequently used block.")
    auto it = std::min_element(cache.begin(), cache.end(), [](const CacheBlock &a, const CacheBlock &b) {
        return a.offset < b.offset;
    });

    if (it != cache.end()) {
        if (it->dirty) {
            LOG("[BlockCache::evict] Writing dirty block to disk for fd: " << it->fd << " at offset: " << it->offset)
            writeBlockToDisk(*it);
        }
        cacheMap[it->fd].erase(it->offset);
        _aligned_free(it->data);
        cache.erase(it);
    }
}

void BlockCache::writeBlockToDisk(const CacheBlock &block) {
    LOG("[BlockCache::writeBlockToDisk] Writing block to disk for fd: " << block.fd << " at offset: " << block.offset)
    HANDLE handle = reinterpret_cast<HANDLE>(block.fd);

    LARGE_INTEGER li;
    li.QuadPart = block.offset;
    SetFilePointerEx(handle, li, NULL, FILE_BEGIN);

    DWORD bytesWritten = 0;
    if (!WriteFile(handle, block.data, PAGE_SIZE, &bytesWritten, NULL)) {
        LOG_ERROR("[BlockCache::writeBlockToDisk] Failed to write block to disk.")
    }
}

int lab2_open(const char *path) {
    LOG("[lab2_open] Opening file: " << path)
    HANDLE hFile = CreateFileA(
            path,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_ALWAYS,
            FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
            NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        LOG_ERROR("[lab2_open] Failed to open file: " << path)
        return -1;
    }

    LOG("[lab2_open] File opened successfully: " << path << ". Handle: " << hFile)
    return reinterpret_cast<int>(hFile);
}

ssize_t lab2_read(int fd, void *buf, size_t count) {
    LOG("[lab2_read] Reading " << count << " bytes from fd: " << fd)
    HANDLE handle = reinterpret_cast<HANDLE>(fd);

    LARGE_INTEGER currentPos;
    if (!SetFilePointerEx(handle, {0}, &currentPos, FILE_CURRENT)) {
        LOG_ERROR("[lab2_read] Failed to get current file position for fd: " << fd)
        return -1;
    }

    size_t offset = static_cast<size_t>(currentPos.QuadPart);
    LOG("[lab2_read] Current file position: " << offset)

    if (BlockCache::getInstance().read(fd, buf, count, offset)) {
        LOG("[lab2_read] Data retrieved from cache for fd: " << fd << " at offset: " << offset)

        // Adjust file pointer
        LARGE_INTEGER li;
        li.QuadPart = offset + count;
        if (!SetFilePointerEx(handle, li, NULL, FILE_BEGIN)) {
            LOG_ERROR("[lab2_read] Failed to set new file position for fd: " << fd)
            return -1;
        }

        return static_cast<ssize_t>(count);
    }

    LOG_ERROR("[lab2_read] Failed to read from cache for fd: " << fd << " at offset: " << offset)
    return -1;
}

ssize_t lab2_write(int fd, const void *buf, size_t count, size_t offset) {
    LOG("[lab2_write] Writing " << count << " bytes to fd: " << fd << " at offset: " << offset)
    HANDLE handle = reinterpret_cast<HANDLE>(fd);

    if (BlockCache::getInstance().write(fd, buf, count, offset)) {
        LOG("[lab2_write] Data written to cache for fd: " << fd << " at offset: " << offset)
        return static_cast<ssize_t>(count);
    }

    LOG_ERROR("[lab2_write] Failed to write data to cache for fd: " << fd << " at offset: " << offset)
    return -1;
}

int lab2_close(int fd) {
    LOG("[lab2_close] Closing fd: " << fd)
    HANDLE handle = reinterpret_cast<HANDLE>(fd);
    if (handle != INVALID_HANDLE_VALUE) {
        BlockCache::getInstance().close(fd);
        if (CloseHandle(handle)) {
            LOG("[lab2_close] File handle closed successfully for fd: " << fd)
            return 0;
        } else {
            LOG_ERROR("[lab2_close] Failed to close file handle for fd: " << fd)
            return -1;
        }
    }
    LOG_ERROR("[lab2_close] Invalid handle for fd: " << fd)
    return -1;
}

int64_t lab2_lseek(int fd, int64_t offset, int whence) {
    LOG("[lab2_lseek] Seeking in fd: " << fd << " with offset: " << offset << " and whence: " << whence)
    HANDLE handle = reinterpret_cast<HANDLE>(fd);
    LARGE_INTEGER li;
    li.QuadPart = offset;
    if (!SetFilePointerEx(handle, li, NULL, whence)) {
        LOG_ERROR("[lab2_lseek] Failed to set file pointer for fd: " << fd)
        return -1;
    }

    LARGE_INTEGER newPos;
    if (!SetFilePointerEx(handle, {0}, &newPos, FILE_CURRENT)) {
        LOG_ERROR("[lab2_lseek] Failed to get new file position for fd: " << fd)
        return -1;
    }

    LOG("[lab2_lseek] New file position for fd: " << fd << " is: " << newPos.QuadPart)
    return static_cast<int64_t>(newPos.QuadPart);
}

int lab2_fsync(int fd) {
    LOG("[lab2_fsync] Syncing fd: " << fd << " with disk.")
    HANDLE handle = reinterpret_cast<HANDLE>(fd);
    BlockCache::getInstance().sync(fd);
    if (FlushFileBuffers(handle)) {
        LOG("[lab2_fsync] Successfully synced fd: " << fd << " with disk.")
        return 0;
    } else {
        LOG_ERROR("[lab2_fsync] Failed to flush file buffers for fd: " << fd)
        return -1;
    }
}