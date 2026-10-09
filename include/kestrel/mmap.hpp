#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace kestrel {

class MemoryMappedFile {
public:
    MemoryMappedFile() = default;

    explicit MemoryMappedFile(const std::string& filepath) {
        open(filepath);
    }

    ~MemoryMappedFile() {
        close();
    }

    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    MemoryMappedFile(MemoryMappedFile&& other) noexcept {
        move_from(std::move(other));
    }

    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept {
        if (this != &other) {
            close();
            move_from(std::move(other));
        }
        return *this;
    }

    void open(const std::string& filepath) {
        close();
#ifdef _WIN32
        file_handle_ = CreateFileA(
            filepath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr
        );
        if (file_handle_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Failed to open file: " + filepath);
        }

        LARGE_INTEGER file_size_large;
        if (!GetFileSizeEx(file_handle_, &file_size_large)) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("Failed to get file size: " + filepath);
        }
        size_ = static_cast<size_t>(file_size_large.QuadPart);

        if (size_ == 0) {
            data_ = nullptr;
            return;
        }

        mapping_handle_ = CreateFileMappingA(file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping_handle_) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("Failed to create file mapping: " + filepath);
        }

        data_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_handle_, FILE_MAP_READ, 0, 0, size_));
        if (!data_) {
            CloseHandle(mapping_handle_);
            CloseHandle(file_handle_);
            mapping_handle_ = nullptr;
            file_handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("Failed to map view of file: " + filepath);
        }
#else
        fd_ = ::open(filepath.c_str(), O_RDONLY);
        if (fd_ == -1) {
            throw std::runtime_error("Failed to open file: " + filepath);
        }

        struct stat sb;
        if (fstat(fd_, &sb) == -1) {
            ::close(fd_);
            fd_ = -1;
            throw std::runtime_error("Failed to stat file: " + filepath);
        }
        size_ = static_cast<size_t>(sb.st_size);

        if (size_ == 0) {
            data_ = nullptr;
            return;
        }

        void* mapped = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (mapped == MAP_FAILED) {
            ::close(fd_);
            fd_ = -1;
            throw std::runtime_error("Failed to mmap file: " + filepath);
        }
        data_ = static_cast<const uint8_t*>(mapped);
#ifdef POSIX_MADV_SEQUENTIAL
        posix_madvise(mapped, size_, POSIX_MADV_SEQUENTIAL);
#endif
#endif
    }

    void close() noexcept {
#ifdef _WIN32
        if (data_) {
            UnmapViewOfFile(data_);
            data_ = nullptr;
        }
        if (mapping_handle_) {
            CloseHandle(mapping_handle_);
            mapping_handle_ = nullptr;
        }
        if (file_handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (data_) {
            munmap(const_cast<uint8_t*>(data_), size_);
            data_ = nullptr;
        }
        if (fd_ != -1) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
        size_ = 0;
    }

    [[nodiscard]] const uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }

private:
    void move_from(MemoryMappedFile&& other) noexcept {
        data_ = other.data_;
        size_ = other.size_;
#ifdef _WIN32
        file_handle_ = other.file_handle_;
        mapping_handle_ = other.mapping_handle_;
        other.file_handle_ = INVALID_HANDLE_VALUE;
        other.mapping_handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        other.data_ = nullptr;
        other.size_ = 0;
    }

    const uint8_t* data_{nullptr};
    size_t size_{0};
#ifdef _WIN32
    HANDLE file_handle_{INVALID_HANDLE_VALUE};
    HANDLE mapping_handle_{nullptr};
#else
    int fd_{-1};
#endif
};

}
