// app.cpp
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include "cache.h" // Updated cache module

// Logging flag
bool g_loggingEnabledApp = true;

// Logging macros
#define LOG(x) if (g_loggingEnabledApp) std::cout << x << std::endl;
#define LOG_ERROR(x) if (g_loggingEnabledApp) std::cerr << x << std::endl;

std::string getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &in_time_t);
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %X");
    return ss.str();
}

// Function to enable or disable logging
void setLogging(bool enable) {
    g_loggingEnabledApp = enable;
    LOG("[app] Logging " << (enable ? "enabled." : "disabled."));
}

const size_t FILE_SIZE = 8 * 1024 * 100; // 8MB
const size_t SECTOR_SIZE = 4096;
const size_t ELEMENT_SIZE = sizeof(int);
const size_t NUM_ELEMENTS = FILE_SIZE / ELEMENT_SIZE;

// Function declarations before their usage
bool get_element(int fd, size_t index, int &value, void *alignedBuffer);

bool set_element(int fd, size_t index, int value, void *alignedBuffer);

bool swap_elements(int fd, size_t index1, size_t index2, void *alignedBuffer);

// Function to create a file with random data
bool create_file(const char *path) {
    LOG("[create_file] " << ELEMENT_SIZE << " bytes per element. " << NUM_ELEMENTS << " total elements.");
    LOG("[create_file] Called with path: " << path);
    int fd = lab2_open(path);
    if (fd == -1) {
        LOG_ERROR("[create_file] Failed to open file for writing.");
        return false;
    }

    srand(static_cast<unsigned>(time(nullptr)));

    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        int value = rand();
        if (lab2_write(fd, &value, ELEMENT_SIZE, i*sizeof(value)) != static_cast<ssize_t>(ELEMENT_SIZE)) {
            LOG_ERROR("[create_file] Error writing value at element index: " << i);
            lab2_close(fd);
            return false;
        }
        if (i % 100000 == 0 && i != 0) {
            LOG("[create_file] " << i << " elements written.");
        }
    }

    LOG("[create_file] Data writing completed. Syncing cache.");
    if (lab2_fsync(fd) != 0) {
        LOG_ERROR("[create_file] Failed to perform fsync on cache.");
        lab2_close(fd);
        return false;
    }
    if (lab2_close(fd) != 0) {
        LOG_ERROR("[create_file] Failed to close file.");
        return false;
    }
    LOG("[create_file] File creation successful: " << path);
    return true;
}

// Function to swap two elements in the file
bool swap_elements(int fd, size_t index1, size_t index2, void *alignedBuffer) {
    if (index1 == index2) {
        return true;
    }
    int val1, val2;

    // Reading the first element
    if (!get_element(fd, index1, val1, alignedBuffer)) {
        LOG_ERROR("[swap_elements] Failed to get value at index1: " << index1);
        return false;
    }

    // Reading the second element
    if (!get_element(fd, index2, val2, alignedBuffer)) {
        LOG_ERROR("[swap_elements] Failed to get value at index2: " << index2);
        return false;
    }

    if (val1 == val2) {
        LOG("[swap_elements] No need to swap indices " << index1 << " and " << index2);
        return true;
    }

    // Writing the first element to the second position
    if (!set_element(fd, index2, val1, alignedBuffer)) {
        LOG_ERROR("[swap_elements] Failed to write value at index2: " << index2);
        return false;
    }

    // Writing the second element to the first position
    if (!set_element(fd, index1, val2, alignedBuffer)) {
        LOG_ERROR("[swap_elements] Failed to write value at index1: " << index1);
        return false;
    }

    LOG("[swap_elements] Successfully swapped indices " << index1 << " and " << index2);
    return true;
}

// Function to get the value of an element from the file
bool get_element(int fd, size_t index, int &value, void *alignedBuffer) {
    size_t offset = (index * ELEMENT_SIZE) / SECTOR_SIZE * SECTOR_SIZE;
    size_t alignedSize = SECTOR_SIZE;

    if (lab2_lseek(fd, offset, SEEK_SET) == -1) {
        LOG_ERROR("[get_element] Failed to set offset at index: " << index);
        return false;
    }
    if (lab2_read(fd, alignedBuffer, alignedSize) != static_cast<ssize_t>(alignedSize)) {
        LOG_ERROR("[get_element] Failed to read value at index: " << index);
        return false;
    }
    size_t offsetInBuffer = (index * ELEMENT_SIZE) % SECTOR_SIZE;
    value = *(int *)((char *)alignedBuffer + offsetInBuffer);
    return true;
}

// Function to set the value of an element in the file
bool set_element(int fd, size_t index, int value, void *alignedBuffer) {
    size_t offset = (index * ELEMENT_SIZE) / SECTOR_SIZE * SECTOR_SIZE;
    size_t alignedSize = SECTOR_SIZE;

    // Read the sector first
    if (lab2_lseek(fd, offset, SEEK_SET) == -1) {
        LOG_ERROR("[set_element] Failed to set offset at index: " << index);
        return false;
    }
    if (lab2_read(fd, alignedBuffer, alignedSize) != static_cast<ssize_t>(alignedSize)) {
        LOG_ERROR("[set_element] Failed to read sector at index: " << index);
        return false;
    }

    // Modify the specific value
    size_t offsetInBuffer = (index * ELEMENT_SIZE) % SECTOR_SIZE;
    *(int *)((char *)alignedBuffer + offsetInBuffer) = value;

    // Write back the entire sector
    if (lab2_lseek(fd, offset, SEEK_SET) == -1) {
        LOG_ERROR("[set_element] Failed to reset offset at index: " << index);
        return false;
    }
    if (lab2_write(fd, alignedBuffer, alignedSize, offset) != static_cast<ssize_t>(alignedSize)) {
        LOG_ERROR("[set_element] Failed to write sector at index: " << index);
        return false;
    }

    return true;
}

// Function to print the first N elements of the file
bool print_first_n(int fd, int n, void *alignedBuffer) {
    std::cout << "[print_first_n] Printing first " << n << " elements:" << std::endl;
    for (int i = 0; i < n; ++i) {
        int value;
        if (!get_element(fd, i, value, alignedBuffer)) {
            LOG_ERROR("[print_first_n] Failed to retrieve element at index: " << i);
            return false;
        }
        std::cout << value << " ";
    }
    std::cout << std::endl;
    return true;
}

// Function to verify if the file is sorted
bool verify_sorted(int fd, void *alignedBuffer) {
    LOG("[verify_sorted] Verifying if the file is sorted.");
    int previous = 0;
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        int current;
        if (!get_element(fd, i, current, alignedBuffer)) {
            LOG_ERROR("[verify_sorted] Failed to retrieve element at index: " << i);
            return false;
        }
        if (i > 0 && current < previous) {
            LOG_ERROR("[verify_sorted] File is not sorted. Element at index " << i << " (" << current
                                                                              << ") is less than previous element ("
                                                                              << previous << ").");
            return false;
        }
        previous = current;
    }
    LOG("[verify_sorted] File is sorted correctly.");
    return true;
}

// Partition function for QuickSort
int partition(int fd, int low, int high, void *alignedBuffer) {
    int pivot;
    if (!get_element(fd, high, pivot, alignedBuffer)) {
        LOG_ERROR("[partition] Failed to get pivot element at index: " << high);
        return -1; // Indicate an error
    }

    int i = low - 1;

    for (int j = low; j <= high - 1; j++) {
        int current;
        if (!get_element(fd, j, current, alignedBuffer)) {
            LOG_ERROR("[partition] Failed to get element at index: " << j);
            return -1;
        }
        if (current < pivot) {
            ++i;
            if (i != j) {
                if (!swap_elements(fd, i, j, alignedBuffer)) {
                    LOG_ERROR("[partition] Failed to swap elements at indices: " << i << " and " << j);
                    return -1;
                }
            }
        }
    }

    if (!swap_elements(fd, i + 1, high, alignedBuffer)) {
        LOG_ERROR("[partition] Failed to swap pivot element with element at index: " << i + 1);
        return -1;
    }

    return i + 1;
}

// QuickSort implementation
bool quicksort(int fd, int low, int high, void *alignedBuffer) {

    // Base case
    if (low < high) {
        // Partition the array
        int pi = partition(fd, low, high, alignedBuffer);
        if (pi == -1) {
            LOG_ERROR("[quicksort] Partition failed between indices " << low << " and " << high);
            return false;
        }

        // Separately sort elements before and after partition
        if (!quicksort(fd, low, pi - 1, alignedBuffer)) {
            LOG_ERROR("[quicksort] Failed to sort left part from " << low << " to " << pi - 1);
            return false;
        }
        if (!quicksort(fd, pi + 1, high, alignedBuffer)) {
            LOG_ERROR("[quicksort] Failed to sort right part from " << pi + 1 << " to " << high);
            return false;
        }
    }
    return true;
}

int main() {
    const char *file_path = "data.bin";
    std::cout << "[main] " << getCurrentTime() << "Starting program. File path: " << file_path << std::endl;

    // Optionally create the data file
    if (!create_file(file_path)) {
        LOG_ERROR("[main] Failed to create file.\n");
        return EXIT_FAILURE;
    }

    // Opening the file for sorting
    int fd = lab2_open(file_path);
    if (fd == -1) {
        LOG_ERROR("[main] Failed to open file for sorting.\n");
        return EXIT_FAILURE;
    }
    LOG("[main] File opened for sorting. Descriptor: " << fd);

    // Allocate aligned buffer
    void *alignedBuffer = _aligned_malloc(SECTOR_SIZE, SECTOR_SIZE);
    if (!alignedBuffer) {
        LOG_ERROR("[main] Failed to allocate aligned buffer.\n");
        return EXIT_FAILURE;
    }

    // Print first 10 elements before sorting
    if (!print_first_n(fd, 10, alignedBuffer)) {
        LOG_ERROR("[main] Failed to print first 10 elements before sorting.\n");
    }

    // Performing QuickSort
    LOG("[main] Starting QuickSort.");
    if (!quicksort(fd, 0, static_cast<int>(NUM_ELEMENTS - 1), alignedBuffer)) {
        LOG_ERROR("[main] Error during sorting.\n");
        _aligned_free(alignedBuffer);
        if (lab2_close(fd) != 0) {
            LOG_ERROR("[main] Failed to close file after sorting error.\n");
        }
        return EXIT_FAILURE;
    }
    LOG("[main] QuickSort completed successfully.");

    // Print first 10 elements after sorting
    if (!print_first_n(fd, 10, alignedBuffer)) {
        LOG_ERROR("[main] Failed to print first 10 elements after sorting.\n");
    }

    // Verify if the file is sorted
    if (!verify_sorted(fd, alignedBuffer)) {
        LOG_ERROR("[main] File is not sorted correctly.\n");
    } else {
        std::cout << "[main] File has been sorted correctly." << std::endl;
        LOG("[main] File has been sorted correctly.");
    }

    // Syncing and closing the file
    LOG("[main] Syncing cache with disk.");
    if (lab2_fsync(fd) != 0) {
        LOG_ERROR("[main] Failed to perform fsync on cache.\n");
        _aligned_free(alignedBuffer);
        if (lab2_close(fd) != 0) {
            LOG_ERROR("[main] Failed to close file after fsync error.\n");
        }
        return EXIT_FAILURE;
    }
    if (lab2_close(fd) != 0) {
        LOG_ERROR("[main] Failed to close file.\n");
        _aligned_free(alignedBuffer);
        return EXIT_FAILURE;
    }
    _aligned_free(alignedBuffer);
    LOG("[main] File closed successfully.");

    LOG("[main] Sorting completed.");
    return EXIT_SUCCESS;
}