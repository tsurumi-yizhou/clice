#include "vfs/file_table.h"

#include <chrono>

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/xxhash.h"

namespace clice {

std::optional<ObservedFile> read_file_observed(const char* path) {
    auto fd = llvm::sys::fs::openNativeFileForRead(path);
    if(!fd) {
        llvm::consumeError(fd.takeError());
        return std::nullopt;
    }
    auto close = llvm::make_scope_exit([&] { llvm::sys::fs::closeFile(*fd); });

    fs::FileMetadata before;
    bool have_before = !fs::file_metadata(*fd, before);

    // Force read() instead of mmap (IsVolatile): the bytes must be a
    // snapshot taken between the two fstats — a mapped buffer would keep
    // tracking the file after the post-fstat, unpairing hash and stat.
    auto buf = llvm::MemoryBuffer::getOpenFile(*fd,
                                               path,
                                               /*FileSize=*/-1,
                                               /*RequiresNullTerminator=*/true,
                                               /*IsVolatile=*/true);
    if(!buf) {
        return std::nullopt;
    }

    ObservedFile result;
    result.content = std::move(*buf);
    if(auto text = without_bom(result.content->getBuffer());
       text.size() != result.content->getBufferSize()) {
        result.content = llvm::MemoryBuffer::getMemBufferCopy(text, path);
    }

    fs::FileMetadata after;
    bool have_after = !fs::file_metadata(*fd, after);
    result.obs.hash = llvm::xxh3_64bits(result.content->getBuffer());
    if(!have_after) {
        return result;
    }
    result.obs.size = after.size;
    result.obs.mtime_ns = after.mtime_ns;
    result.obs.uid_device = after.uid_device;
    result.obs.uid_file = after.uid_file;
    result.obs.paired =
        have_before && before.size == after.size && before.mtime_ns == result.obs.mtime_ns;

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    result.obs.reliable =
        result.obs.paired && result.obs.mtime_ns <= fs::stat_baseline_before_ns(now_ms);
    return result;
}

/// `path` with its `..` segments resolved as text.
static Spelling fold(const Spelling& path) {
    llvm::SmallString<256> text(path.str());
    path::remove_dots(text, /*remove_dot_dot=*/true);
    return Spelling::absolute(text);
}

std::string FileTable::display(CanonicalRef identity) const {
    for(auto& [real, root]: spelled_roots) {
        if(path::under(identity, real)) {
            auto rest = llvm::StringRef(identity).drop_front(real.size()).ltrim('/');
            return Spelling(rest, root).str();
        }
    }
    return identity.str();
}

void FileTable::spell_root(const Spelling& root) {
    CanonicalPath real(root);
    auto folded = fold(root);
    if(folded.str() != real.str() && CanonicalPath(folded) == real) {
        spelled_roots.emplace_back(std::move(real), std::move(folded));
    }
}

void FileTable::unspell_root(const Spelling& root) {
    auto folded = fold(root);
    llvm::erase_if(spelled_roots, [&](auto& entry) { return entry.second == folded; });
}

}  // namespace clice
