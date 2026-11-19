#include "ui_text_input.hpp"
#include "ui_control_access.hpp"
#include "ui_text_boundaries.hpp"
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
        bool alive = true, composing = false, pointer_drag = false;
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
    // Reentrant public input invalidates an outer transient selection correction.
    // This routing stamp contains no field, caret or selection authority.
    std::uint64_t input_dispatch = 0, focus_generation = 0;
    struct SelectionGate final : Rml::EventListener {
        Rml::ObserverPtr<Rml::Element> element;
        std::function<void(Rml::Event&)> observe;
        Rml::EventId event_id;
        SelectionGate(Rml::Element& field, Rml::EventId event,
                      std::function<void(Rml::Event&)> callback)
            : element(field.GetObserverPtr()), observe(std::move(callback)), event_id(event) {
            element->AddEventListener(event_id, this, true);
        }
        ~SelectionGate() override {
            if (element)
                element->RemoveEventListener(event_id, this, true);
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
        ++focus_generation;
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
            active->pointer_drag = false;
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
            proxy->state->pointer_drag = false;
            if (active == proxy->state) {
                ++focus_generation;
                active.reset();
            }
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
            if (active == proxy->state) {
                ++focus_generation;
                active.reset();
            }
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
        const auto dispatch = input_dispatch, focus = focus_generation;
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
        if (token && token->alive && active == token && input_dispatch == dispatch &&
            focus_generation == focus && context.GetFocusElement() == field) {
            UiTextSelection current;
            field->GetSelection(&current.begin, &current.end, nullptr);
            // Cancelled native key events must not leave the temporary selection behind.
            if (current == *prepared && field->GetValue() == previous)
                field->SetSelectionRange(original.begin, original.end);
        }
        return propagated;
    }
    template <class T, class Native, class Align>
    bool field_selection_event(Rml::Context& context, T* field, Rml::EventId event_id,
                               std::uint64_t dispatch, bool new_focus, Native native, Align align) {
        const auto initial = active;
        const auto initial_focus = focus_generation;
        const auto observer = field->GetObserverPtr();
        const auto previous = field->GetValue();
        UiTextSelection before;
        field->GetSelection(&before.begin, &before.end, nullptr);
        bool allowed = false;
        unsigned observed = 0;
        UiTextSelection received;
        std::shared_ptr<State> token;
        std::uint64_t received_focus = 0;
        // Observe the existing widget's capture action. Later callback changes,
        // reentrant input, composition and field retirement retain native results.
        SelectionGate gate(*field, event_id, [&](Rml::Event& event) {
            if (event_id == Rml::EventId::Keydown && event.GetTargetElement() != field)
                return;
            ++observed;
            if (observer && active && active->alive && !active->composing &&
                (new_focus || (active == initial && focus_generation == initial_focus)) &&
                input_dispatch == dispatch && context.GetFocusElement() == field &&
                field->GetValue() == previous) {
                field->GetSelection(&received.begin, &received.end, nullptr);
                token = active;
                received_focus = focus_generation;
                allowed = observed == 1 && event.IsImmediatePropagating();
            } else
                allowed = false;
        });
        const bool propagated = native();
        if (allowed && observer && token && token->alive && !token->composing && active == token &&
            input_dispatch == dispatch && focus_generation == received_focus &&
            context.GetFocusElement() == field && field->GetValue() == previous) {
            UiTextSelection after;
            field->GetSelection(&after.begin, &after.end, nullptr);
            if (after != received)
                return propagated;
            try {
                if (const auto next = align(previous, before, after))
                    field->SetSelectionRange(next->anchor, next->caret);
            } catch (const std::runtime_error&) {
                // Non-admitted fields retain native editing, without truncation.
            }
        }
        return propagated;
    }
    template <class T>
    bool field_movement_key(Rml::Context& context, T* field, int key, int modifiers,
                            UiTextKey action, std::uint64_t dispatch, bool jump) {
        return field_selection_event(
            context, field, Rml::EventId::Keydown, dispatch, false,
            [&] {
                return context.ProcessKeyDown(static_cast<Rml::Input::KeyIdentifier>(key),
                                              modifiers);
            },
            [&](std::string_view text, UiTextSelection before, UiTextSelection after) {
                return jump || (modifiers & Rml::Input::KM_CTRL)
                           ? ui_text_movement_selection(text, before, after, action,
                                                        bool(modifiers & Rml::Input::KM_SHIFT))
                           : ui_text_shift_selection(text, before, after, action);
            });
    }
    bool mouse_move(Rml::Context& context, int x, int y, int modifiers, bool primary_down) {
        const auto dispatch = ++input_dispatch;
        const auto native = [&] { return context.ProcessMouseMove(x, y, modifiers); };
        if (!primary_down || !active || !active->pointer_drag || active->composing ||
            (modifiers & (Rml::Input::KM_ALT | Rml::Input::KM_META)))
            return native();
        const auto field = ui_text_field(context.GetFocusElement());
        if (!field || !ui_text_field_editable(*field))
            return native();
        return std::visit(
            [&](auto* value) {
                return field_selection_event(
                    context, value, Rml::EventId::Drag, dispatch, false, native,
                    [](std::string_view text, UiTextSelection before, UiTextSelection after) {
                        return ui_text_pointer_selection(text, before, after, true);
                    });
            },
            *field);
    }
    bool mouse_button(Rml::Context& context, int button, bool down, int modifiers) {
        const auto dispatch = ++input_dispatch;
        if (button == 0 && active)
            active->pointer_drag = false;
        const auto native = [&] {
            return down ? context.ProcessMouseButtonDown(button, modifiers)
                        : context.ProcessMouseButtonUp(button, modifiers);
        };
        if (button != 0 || !down || (active && active->composing) ||
            (modifiers & (Rml::Input::KM_ALT | Rml::Input::KM_META)))
            return native();
        auto* hovered = context.GetHoverElement();
        for (std::size_t depth = 0; hovered && depth < ui_control_nodes;
             ++depth, hovered = hovered->GetParentNode()) {
            const auto field = ui_text_field(hovered);
            if (!field)
                continue;
            if (!ui_text_field_editable(*field))
                break;
            return std::visit(
                [&](auto* value) {
                    return field_selection_event(
                        context, value, Rml::EventId::Mousedown, dispatch, true, native,
                        [&](std::string_view text, UiTextSelection before, UiTextSelection after) {
                            const bool extending = bool(modifiers & Rml::Input::KM_SHIFT);
                            const auto next =
                                ui_text_pointer_selection(text, before, after, extending);
                            // Eligibility retains no cursor/anchor. Native multiclick
                            // word gestures keep their private selection policy.
                            active->pointer_drag = !extending && after.begin == after.end;
                            return next;
                        });
                },
                *field);
        }
        return native();
    }
    bool key_down(Rml::Context& context, int key, int modifiers) {
        const auto dispatch = ++input_dispatch;
        std::optional<UiTextKey> action;
        // Boundary alignment uses logical direction; the original native key
        // still chooses its line/page/word destination and geometry.
        bool jump = false;
        switch (key) {
        case Rml::Input::KI_LEFT:
            action = UiTextKey::Left;
            break;
        case Rml::Input::KI_RIGHT:
            action = UiTextKey::Right;
            break;
        case Rml::Input::KI_UP:
        case Rml::Input::KI_HOME:
        case Rml::Input::KI_PRIOR:
            action = UiTextKey::Left;
            jump = true;
            break;
        case Rml::Input::KI_DOWN:
        case Rml::Input::KI_END:
        case Rml::Input::KI_NEXT:
            action = UiTextKey::Right;
            jump = true;
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
                return std::visit(
                    [&](auto* value) {
                        const bool movement =
                            *action == UiTextKey::Left || *action == UiTextKey::Right;
                        if (jump || (movement &&
                                     (modifiers & (Rml::Input::KM_SHIFT | Rml::Input::KM_CTRL))))
                            return field_movement_key(context, value, key, modifiers, *action,
                                                      dispatch, jump);
                        if (!movement && (modifiers & Rml::Input::KM_CTRL)) {
                            UiTextSelection selected;
                            value->GetSelection(&selected.begin, &selected.end, nullptr);
                            if (selected.begin == selected.end)
                                // The native word range is private; preserve that
                                // policy instead of synthesizing movement events.
                                return context.ProcessKeyDown(
                                    static_cast<Rml::Input::KeyIdentifier>(key), modifiers);
                        }
                        return field_key(context, value, key, modifiers, *action);
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
bool UiTextInput::mouse_move(Rml::Context& context, int x, int y, int modifiers,
                             bool primary_down) {
    return impl_->mouse_move(context, x, y, modifiers, primary_down);
}
bool UiTextInput::mouse_button(Rml::Context& context, int button, bool down, int modifiers) {
    return impl_->mouse_button(context, button, down, modifiers);
}
} // namespace forge
