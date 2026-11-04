#pragma once
#include "../view_image.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace forge {
class FileDialog {
  public:
    enum class Kind {
        OpenScene,
        SaveScene,
        OpenProject,
        ProjectParent,
        ImportFiles,
        SaveImage,
        SequenceFramesParent
    };
    struct Result {
        Kind kind;
        std::string path, error;
        std::vector<std::string> paths;
        int filter_index = -1;
    };
#ifdef FORGE_UI_FIXTURE
    // Native picker boundary only. The fixture still clicks the actual command;
    // its result goes through the same busy state, take() and owning controller.
    inline static std::optional<Result> fixture_selection;
    inline static unsigned fixture_deliveries = 0;
#endif
    // One ordered owner for the native filter array and returned index mapping.
    struct ImageChoice {
        SDL_DialogFileFilter filter;
        ViewImageFormat format;
    };
    inline static constexpr std::array image_choices{
        ImageChoice{{"PNG image", "png"}, ViewImageFormat::Png},
        ImageChoice{{"Lossless WebP image", "webp"}, ViewImageFormat::Webp},
        ImageChoice{{"JPEG image", "jpg;jpeg"}, ViewImageFormat::Jpeg}};
    inline static constexpr ImageChoice hdr_choice{{"HDR image (Radiance RGBE)", "hdr"},
                                                   ViewImageFormat::Hdr};
    static const ImageChoice& image_choice(ViewImageFormat format) {
        if (format == hdr_choice.format)
            return hdr_choice;
        for (const auto& choice : image_choices)
            if (choice.format == format)
                return choice;
        throw std::runtime_error("The image dialog has no matching format");
    }
    static std::optional<ViewImageFormat>
    image_format(int index, std::optional<ViewImageFormat> only_format = {}) {
        if (index == -1)
            return {};
        if (index < 0 || (only_format ? index != 0 : std::size_t(index) >= image_choices.size()))
            throw std::runtime_error("The image dialog returned an unknown filter");
        return only_format ? image_choice(*only_format).format
                           : image_choices[std::size_t(index)].format;
    }
    bool busy() const { return busy_; }
    void show(Kind kind, SDL_Window* window, std::string location,
              std::optional<ViewImageFormat> only_format = {}) {
        if (busy_)
            return;
        busy_ = true;
        state_->location = std::move(location);
        state_->kind = kind;
#ifdef FORGE_UI_FIXTURE
        if (fixture_selection) {
            if (fixture_selection->kind != kind)
                throw std::runtime_error("Fixture file picker kind differs from the command");
            std::lock_guard lock(state_->mutex);
            state_->result = std::move(fixture_selection);
            fixture_selection.reset();
            ++fixture_deliveries;
            return;
        }
#endif
        auto* owned = new std::shared_ptr<State>(state_);
        static const SDL_DialogFileFilter filter{"FORGE scene", "json"};
        static constexpr auto image_filters = [] {
            std::array<SDL_DialogFileFilter, image_choices.size()> result{};
            for (std::size_t i = 0; i < result.size(); ++i)
                result[i] = image_choices[i].filter;
            return result;
        }();
        if (kind == Kind::ImportFiles)
            SDL_ShowOpenFileDialog(callback, owned, window, nullptr, 0, state_->location.c_str(),
                                   true);
        else if (kind == Kind::OpenScene)
            SDL_ShowOpenFileDialog(callback, owned, window, &filter, 1, state_->location.c_str(),
                                   false);
        else if (kind == Kind::SaveScene)
            SDL_ShowSaveFileDialog(callback, owned, window, &filter, 1, state_->location.c_str());
        else if (kind == Kind::SaveImage)
            SDL_ShowSaveFileDialog(
                callback, owned, window,
                only_format ? &image_choice(*only_format).filter : image_filters.data(),
                only_format ? 1 : int(image_filters.size()), state_->location.c_str());
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
    static void SDLCALL callback(void* userdata, const char* const* files, int filter_index) {
        std::unique_ptr<std::shared_ptr<State>> owned(
            static_cast<std::shared_ptr<State>*>(userdata));
        auto& state = **owned;
        Result result{state.kind, {}, {}};
        result.filter_index = filter_index;
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
