// rex::memory::MappedMemory for Horizon (replaces mapped_memory_posix.cpp): no mmap, so a mapping is
// the file's bytes read into RAM. Used for the STFS/disc containers and host files
// (host_path_entry.cpp). Writable mappings write back on Close/Flush.
#include <cstdio>
#include <vector>

#include <rex/memory/mapped_memory.h>

namespace rex::memory {

namespace {

class ReadMappedMemory final : public MappedMemory {
   public:
    ReadMappedMemory(std::filesystem::path path, Mode mode, size_t offset, std::vector<uint8_t> bytes)
        : path_(std::move(path)), mode_(mode), offset_(offset), bytes_(std::move(bytes)) {
        data_ = bytes_.data();
        size_ = bytes_.size();
    }
    ~ReadMappedMemory() override { Flush(); }

    void Close(uint64_t truncate_size) override {
        if (truncate_size && truncate_size < bytes_.size()) {
            bytes_.resize(truncate_size);
            size_ = bytes_.size();
        }
        Flush();
        bytes_.clear();
        data_ = nullptr;
        size_ = 0;
    }

    void Flush() override {
        if (mode_ != Mode::kReadWrite || bytes_.empty()) {
            return;
        }
        FILE* f = std::fopen(path_.string().c_str(), "r+b");
        if (!f) {
            return;
        }
        std::fseek(f, long(offset_), SEEK_SET);
        std::fwrite(bytes_.data(), 1, bytes_.size(), f);
        std::fclose(f);
    }

   private:
    std::filesystem::path path_;
    Mode mode_;
    size_t offset_;
    std::vector<uint8_t> bytes_;
};

}  // namespace

std::unique_ptr<MappedMemory> MappedMemory::Open(const std::filesystem::path& path, Mode mode, size_t offset,
                                                 size_t length) {
    FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) {
        return nullptr;
    }
    std::fseek(f, 0, SEEK_END);
    const long file_size = std::ftell(f);
    if (file_size < 0 || size_t(file_size) < offset) {
        std::fclose(f);
        return nullptr;
    }
    if (length == 0 || offset + length > size_t(file_size)) {
        length = size_t(file_size) - offset;
    }
    std::vector<uint8_t> bytes(length);
    std::fseek(f, long(offset), SEEK_SET);
    const size_t read = length ? std::fread(bytes.data(), 1, length, f) : 0;
    std::fclose(f);
    if (read != length) {
        return nullptr;
    }
    return std::make_unique<ReadMappedMemory>(path, mode, offset, std::move(bytes));
}

std::unique_ptr<ChunkedMappedMemoryWriter> ChunkedMappedMemoryWriter::Open(const std::filesystem::path& path,
                                                                           size_t chunk_size,
                                                                           bool low_address_space) {
    (void)path;
    (void)chunk_size;
    (void)low_address_space;
    return nullptr;
}

}  // namespace rex::memory
