#include "Basic/SourceConverter.h"
#include "Server/Server.h"

namespace clice {

async::Task<> Server::onInitialize(json::Value id, const proto::InitializeParams& params) {
    proto::InitializeResult result = {};
    result.serverInfo.name = "clice";
    result.serverInfo.version = "0.0.1";

    /// Set `SemanticTokensOptions`
    result.capabilities.semanticTokensProvider.legend.tokenTypes = {
        "keyword",  "class",    "interface",  "enum",   "struct",   "type",   "parameter",
        "variable", "property", "enumMember", "event",  "function", "method", "macro",
        "keyword",  "modifier", "comment",    "string", "number",   "regexp", "operator",
    };

    result.capabilities.semanticTokensProvider.legend.tokenModifiers = {
        "declaration",
        "definition",
        "readonly",
        "static",
        "deprecated",
        "abstract",
        "async",
        "modification",
        "documentation",
        "defaultLibrary",
        "local",
    };

    co_await response(std::move(id), json::serialize(result));

    auto workplace = SourceConverter::toPath(params.workspaceFolders[0].uri);
    config::init(workplace);

    for(auto& dir: config::server.compile_commands_dirs) {
        llvm::SmallString<128> path = {dir};
        path::append(path, "compile_commands.json");
        database.updateCommands(path);
    }
}

async::Task<> Server::onInitialized(const proto::InitializedParams& params) {
    co_return;
}

async::Task<> Server::onExit(const proto::None&) {
    co_return;
}

async::Task<> Server::onShutdown(json::Value id, const proto::None&) {
    co_return;
}

}  // namespace clice
