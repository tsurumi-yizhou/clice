#include "server/file_tracker.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "support/filesystem.h"
#include "support/logging.h"
#include "support/timer.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/FileSystem.h"

namespace clice {

FileTracker::FileTracker(Project& project, const SessionStore& store, CanonicalPath root) :
    project(project), store(store), cdb(project, std::move(root)) {}

/// Diff ids and event ids share the single file table.
static void push_delta(const CDBDiff& diff, llvm::SmallVectorImpl<FileEvent>& events) {
    if(diff.empty()) {
        return;
    }
    FileEvent::CDBDelta delta;
    delta.added.assign(diff.added.begin(), diff.added.end());
    delta.removed.assign(diff.removed.begin(), diff.removed.end());
    delta.changed.assign(diff.changed.begin(), diff.changed.end());
    events.push_back(FileEvent::cdb_changed(std::move(delta)));
}

llvm::SmallVector<FileEvent> FileTracker::tick_cdb(bool force) {
    llvm::SmallVector<Fid> open_files;
    for(auto& [path_id, session]: store.sessions) {
        open_files.push_back(path_id);
    }
    llvm::SmallVector<FileEvent> events;
    push_delta(cdb.tick(open_files, force), events);
    return events;
}

llvm::SmallVector<FileEvent> FileTracker::discover_around(Fid path_id) {
    llvm::SmallVector<FileEvent> events;
    push_delta(cdb.discover_around(path_id), events);
    return events;
}

kota::task<llvm::SmallVector<FileEvent>> FileTracker::tick_workspace() {
    constexpr std::size_t batch_size = 500;

    if(sweeping) {
        // The poll hook can land while the live loop is suspended between
        // batches; the running sweep already covers this request.
        co_return llvm::SmallVector<FileEvent>{};
    }
    sweeping = true;
    auto guard = llvm::make_scope_exit([this] { sweeping = false; });

    ScopedTimer timer;
    // What the lexical scan saw included, what compiles actually read (the
    // files index rows came from) — a header only a macro include reaches
    // is invisible to the former — and every place last seen empty, where
    // a file appearing changes what some compile would see.
    auto files = project.dep_graph.all_files();
    llvm::append_range(files, llvm::make_first_range(project.project_index.contributions));
    llvm::append_range(files, project.file_table.missing_files());
    llvm::sort(files);
    files.erase(llvm::unique(files), files.end());
    for(std::size_t begin = 0; begin < files.size(); begin += batch_size) {
        struct Query {
            Fid fid;
            std::string path;
            std::uint64_t revision;
            fs::FileMetadata status;
            std::error_code error;
        };

        llvm::SmallVector<Query> batch;
        auto batch_end = std::min(begin + batch_size, files.size());
        batch.reserve(batch_end - begin);
        for(std::size_t i = begin; i < batch_end; i += 1) {
            auto fid = files[i];
            batch.push_back({fid,
                             project.file_table.resolve(fid).str(),
                             project.file_table.observation_revision(fid),
                             {},
                             {}});
        }

        // Only the owned batch crosses threads. queue waits for running work
        // before completing cancellation, keeping the captured storage alive.
        auto result = co_await kota::queue([&batch] {
            for(auto& query: batch) {
                query.error = fs::file_metadata(query.path, query.status);
            }
        });
        if(!result) {
            // No observations from an incomplete batch. The next sweep retries.
            continue;
        }

        for(auto& query: batch) {
            auto path_id = query.fid;
            if(project.file_table.observation_revision(path_id) != query.revision) {
                continue;
            }
            if(query.error) {
                project.file_table.saw_missing(path_id);
                continue;
            }
            // Reads only when the shared pair cannot vouch for the stat. A
            // file that stats fine but cannot be read right now (e.g. an
            // antivirus scanner briefly holding a fresh file on Windows)
            // leaves what was seen untouched; the next tick looks again.
            project.file_table.observe_for(path_id, query.status);
        }
    }

    // A file created under a default-command rule joins the build: the
    // same gain of a command a database reload reports as added. One
    // deleted left through DiskRemoved, like any tracked file.
    llvm::SmallVector<FileEvent> events;
    push_delta({.added = project.build.refresh_default_sources()}, events);

    LOG_PERF("tracker", "phase=workspace_sweep files={} elapsed_ms={}", files.size(), timer.ms());
    co_return events;
}

}  // namespace clice
