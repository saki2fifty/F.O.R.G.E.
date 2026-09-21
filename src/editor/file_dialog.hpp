#pragma once
#include <SDL3/SDL.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
namespace forge {
class FileDialog {
  public:
    enum class Kind { OpenScene, SaveScene, OpenProject, ProjectParent, ImportFiles };
    struct Result {
        Kind kind;
        std::string path, error;
        std::vector<std::string> paths;
    };
    bool busy() const { return busy_; }
    void show(Kind kind, SDL_Window* window, std::string location) {
        if (busy_)
            return;
        busy_ = true;
        state_->location = std::move(location);
        state_->kind = kind;
        auto* owned = new std::shared_ptr<State>(state_);
        static const SDL_DialogFileFilter filter{"FORGE scene", "json"};
        if (kind == Kind::ImportFiles)
            SDL_ShowOpenFileDialog(callback, owned, window, nullptr, 0, state_->location.c_str(),
                                   true);
        else if (kind == Kind::OpenScene)
            SDL_ShowOpenFileDialog(callback, owned, window, &filter, 1, state_->location.c_str(),
                                   false);
        else if (kind == Kind::SaveScene)
            SDL_ShowSaveFileDialog(callback, owned, window, &filter, 1, state_->location.c_str());
        else
            SDL_ShowOpenFolderDialog(callback, owned, window, state_->location.c_str(), false);
    }
    std::optional<Result> take() {
        std::lock_guard lock(state_->mutex);
        auto result = std::move(state_->result);
        state_->result.reset();
        if (result)
            busy_ = false;
        return result;
    }

  private:
    struct State {
        std::mutex mutex;
        Kind kind{};
        std::string location;
        std::optional<Result> result;
    };
    static void SDLCALL callback(void* userdata, const char* const* files, int) {
        std::unique_ptr<std::shared_ptr<State>> owned(
            static_cast<std::shared_ptr<State>*>(userdata));
        auto& state = **owned;
        Result result{state.kind, {}, {}};
        if (!files)
            result.error = SDL_GetError();
        else if (*files) {
            result.path = *files;
            for (std::size_t i = 0; files[i]; ++i) {
                if (i == 256) {
                    result.paths.clear();
                    result.path.clear();
                    result.error = "Select no more than256 source files per import.";
                    break;
                }
                result.paths.emplace_back(files[i]);
            }
        }
        std::lock_guard lock(state.mutex);
        state.result = std::move(result);
    }
    std::shared_ptr<State> state_ = std::make_shared<State>();
    bool busy_ = false;
};
} // namespace forge
