// clang-format off
#include <Jolt/Jolt.h>
// clang-format on
#include "physics_registration.hpp"
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#include <mutex>

namespace forge::physics_detail {
namespace {
std::mutex registration_mutex;
std::size_t registration_users = 0;
} // namespace
struct Registration {
    Registration() {
        std::lock_guard lock(registration_mutex);
        if (registration_users == 0) {
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory;
            JPH::RegisterTypes();
        }
        ++registration_users;
    }
    ~Registration() {
        std::lock_guard lock(registration_mutex);
        if (--registration_users == 0) {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }
};
std::shared_ptr<Registration> registration() { return std::make_shared<Registration>(); }
} // namespace forge::physics_detail
