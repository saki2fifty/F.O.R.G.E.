#include "ui_text_input.hpp"
#include "ui_text_boundaries.hpp"
#include "ui_text_field.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <RmlUi/Core/TextInputContext.h>
#include <RmlUi/Core/TextInputHandler.h>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <variant>

namespace forge {
struct UiTextInput::Impl final : Rml::TextInputHandler {
    struct State {
        Rml::TextInputContext* native = nullptr;
        bool alive = true, composing = false;
    };
    // A lifetime-preserving public delegate, not another editable text context.
    struct Proxy final : Rml::TextInputContext {
        std::shared_ptr<State> state;
        explicit Proxy(Rml::TextInputContext* native)
            : state(std::make_shared<State>(State{native})) {}
        bool GetBoundingBox(Rml::Rectanglef& rectangle) const override {
            return state->alive && state->native->GetBoundingBox(rectangle);
        }
        void GetSelectionRange(int& start, int& end) const override {
            start = end = 0;
            if (state->alive)
                state->native->GetSelectionRange(start, end);
        }
        void SetSelectionRange(int start, int end) override {
            if (state->alive)
                state->native->SetSelectionRange(start, end);
        }
        void SetCursorPosition(int position) override {
            if (state->alive)
                state->native->SetCursorPosition(position);
        }
        void SetText(Rml::StringView text, int start, int end) override {
            if (state->alive)
                state->native->SetText(text, start, end);
        }
        void SetCompositionRange(int start, int end) override {
            const auto token = state;
            if (token->alive) {
                token->native->SetCompositionRange(start, end);
                token->composing = token->alive && start != end;
            }
        }
        void CommitComposition(Rml::StringView text) override {
            const auto token = state;
            if (token->alive) {
                token->native->CommitComposition(text);
                token->composing = false;
            }
        }
    };
    Rml::TextInputHandler* delegate;
    std::string& diagnostic;
    std::map<Rml::TextInputContext*, std::unique_ptr<Proxy>> contexts;
    std::shared_ptr<State> active;
    // Reentrant public key dispatch invalidates an outer transient correction.
    // This routing stamp contains no field, caret or selection authority.
    std::uint64_t key_dispatch = 0;
    struct MovementGate final : Rml::EventListener {
        Rml::ObserverPtr<Rml::Element> element;
        std::function<void(Rml::Event&)> observe;
        MovementGate(Rml::Element& field, std::function<void(Rml::Event&)> callback)
            : element(field.GetObserverPtr()), observe(std::move(callback)) {
            element->AddEventListener(Rml::EventId::Keydown, this, true);
        }
        ~MovementGate() override {
            if (element)
                element->RemoveEventListener(Rml::EventId::Keydown, this, true);
        }
        void ProcessEvent(Rml::Event& event) override { observe(event); }
    };
    Impl(Rml::TextInputHandler* handler, std::string& error)
        : delegate(handler), diagnostic(error) {}
    template <class F> void notify(F operation) {
        try {
            operation();
        } catch (const std::exception& error) {
            if (diagnostic.empty())
                diagnostic = "UI text input: " + std::string(error.what());
        } catch (...) {
            if (diagnostic.empty())
                diagnostic = "UI text input: Native handler callback failed";
        }
    }
    void OnActivate(Rml::TextInputContext* native) override {
        Proxy* proxy = nullptr;
        notify([&] {
            if (!native)
                return;
            auto found = contexts.find(native);
            if (found == contexts.end()) {
                auto next = std::make_unique<Proxy>(native);
                found = contexts.emplace(native, std::move(next)).first;
            }
            proxy = found->second.get();
            active = proxy->state;
        });
        if (!proxy)
            active.reset();
        if (delegate)
            notify([&] { delegate->OnActivate(proxy ? proxy : native); });
    }
    void OnDeactivate(Rml::TextInputContext* native) override {
        const auto found = contexts.find(native);
        auto* proxy = found == contexts.end() ? nullptr : found->second.get();
        if (proxy) {
            proxy->state->composing = false;
            if (active == proxy->state)
                active.reset();
        }
        if (delegate)
            notify([&] { delegate->OnDeactivate(proxy ? proxy : native); });
    }
    void OnDestroy(Rml::TextInputContext* native) override {
        auto removed = contexts.extract(native);
        auto* proxy = removed.empty() ? nullptr : removed.mapped().get();
        if (proxy) {
            proxy->state->alive = false;
            proxy->state->native = nullptr;
            if (active == proxy->state)
                active.reset();
        }
        if (delegate)
            notify([&] { delegate->OnDestroy(proxy ? proxy : native); });
    }
    ~Impl() override {
        // Constructor failure/shutdown cannot leave a platform delegate borrowing a proxy.
        while (!contexts.empty())
            OnDestroy(contexts.begin()->first);
    }
    template <class T>
    bool field_key(Rml::Context& context, T* field, int key, int modifiers, UiTextKey action) {
        const auto token = active;
        const auto previous = field->GetValue();
        UiTextSelection original;
        field->GetSelection(&original.begin, &original.end, nullptr);
        std::optional<UiTextSelection> prepared;
        try {
            prepared = ui_text_key_selection(previous, original, action);
        } catch (const std::runtime_error&) {
            // Oversized/non-admitted field values retain the native editing policy.
            return context.ProcessKeyDown(static_cast<Rml::Input::KeyIdentifier>(key), modifiers);
        }
        if (!prepared)
            return context.ProcessKeyDown(static_cast<Rml::Input::KeyIdentifier>(key), modifiers);
        field->SetSelectionRange(prepared->begin, prepared->end);
        const bool propagated =
            context.ProcessKeyDown(static_cast<Rml::Input::KeyIdentifier>(key), modifiers);
        if (token && token->alive && active == token && context.GetFocusElement() == field) {
            UiTextSelection current;
            field->GetSelection(&current.begin, &current.end, nullptr);
            // Cancelled native key events must not leave the temporary selection behind.
            if (current == *prepared && field->GetValue() == previous)
                field->SetSelectionRange(original.begin, original.end);
        }
        return propagated;
    }
    template <class T>
    bool field_movement_key(Rml::Context& context, T* field, int key, int modifiers,
                            UiTextKey action, std::uint64_t dispatch) {
        const auto token = active;
        const auto previous = field->GetValue();
        UiTextSelection before;
        field->GetSelection(&before.begin, &before.end, nullptr);
        bool allowed = false;
        unsigned observed = 0;
        UiTextSelection received;
        // The pinned native widget edits in its capture listener. Observe the
        // same field after that handler; ancestor cancellation prevents this
        // listener too. Later callback edits must retain their result.
        MovementGate gate(*field, [&](Rml::Event& event) {
            if (event.GetTargetElement() != field)
                return;
            ++observed;
            if (token && token->alive && active == token && key_dispatch == dispatch &&
                context.GetFocusElement() == field && field->GetValue() == previous) {
                field->GetSelection(&received.begin, &received.end, nullptr);
                allowed = observed == 1 && event.IsImmediatePropagating();
            } else
                allowed = false;
        });
        const bool propagated =
            context.ProcessKeyDown(static_cast<Rml::Input::KeyIdentifier>(key), modifiers);
        if (allowed && token && token->alive && active == token && key_dispatch == dispatch &&
            context.GetFocusElement() == field && field->GetValue() == previous) {
            UiTextSelection after;
            field->GetSelection(&after.begin, &after.end, nullptr);
            if (after != received)
                return propagated;
            try {
                const auto next =
                    modifiers & Rml::Input::KM_CTRL
                        ? ui_text_word_selection(previous, before, after, action,
                                                 bool(modifiers & Rml::Input::KM_SHIFT))
                        : ui_text_shift_selection(previous, before, after, action);
                if (next)
                    field->SetSelectionRange(next->anchor, next->caret);
            } catch (const std::runtime_error&) {
                // Non-admitted values retain native editing, without truncation.
            }
        }
        return propagated;
    }
    bool key_down(Rml::Context& context, int key, int modifiers) {
        const auto dispatch = ++key_dispatch;
        std::optional<UiTextKey> action;
        switch (key) {
        case Rml::Input::KI_LEFT:
            action = UiTextKey::Left;
            break;
        case Rml::Input::KI_RIGHT:
            action = UiTextKey::Right;
            break;
        case Rml::Input::KI_BACK:
            action = UiTextKey::Backspace;
            break;
        case Rml::Input::KI_DELETE:
            action = UiTextKey::Delete;
            break;
        }
        if (action && active && !active->composing &&
            !(modifiers & (Rml::Input::KM_ALT | Rml::Input::KM_META)))
            if (const auto field = ui_text_field(context.GetFocusElement());
                field && ui_text_field_editable(*field)) {
                if (!(modifiers & (Rml::Input::KM_SHIFT | Rml::Input::KM_CTRL)))
                    return std::visit(
                        [&](auto* value) {
                            return field_key(context, value, key, modifiers, *action);
                        },
                        *field);
                else if (*action == UiTextKey::Left || *action == UiTextKey::Right)
                    return std::visit(
                        [&](auto* value) {
                            return field_movement_key(context, value, key, modifiers, *action,
                                                      dispatch);
                        },
                        *field);
            }
        return context.ProcessKeyDown(static_cast<Rml::Input::KeyIdentifier>(key), modifiers);
    }
};
UiTextInput::UiTextInput(Rml::TextInputHandler* delegate, std::string& diagnostic)
    : impl_(std::make_unique<Impl>(delegate, diagnostic)) {}
UiTextInput::~UiTextInput() = default;
Rml::TextInputHandler* UiTextInput::handler() { return impl_.get(); }
bool UiTextInput::key_down(Rml::Context& context, int key, int modifiers) {
    return impl_->key_down(context, key, modifiers);
}
} // namespace forge
