#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <memory>
#include <stdexcept>
namespace forge::test {
struct GamepadMotionDeviceFixture {
    static void require(bool value, const char* error) {
        if (!value)
            throw std::runtime_error(error);
    }
    bool enabled = false, fail = false;
    SDL_JoystickID id = 0;
    std::unique_ptr<SDL_Joystick, decltype(&SDL_CloseJoystick)> joystick{nullptr,
                                                                         SDL_CloseJoystick};
    static bool SDLCALL mode(void* data, bool value) {
        auto& device = *static_cast<GamepadMotionDeviceFixture*>(data);
        if (device.fail)
            return SDL_SetError("FORGE fixture sensor transition failed");
        device.enabled = value;
        return true;
    }
    GamepadMotionDeviceFixture() {
        const std::array<SDL_VirtualJoystickSensorDesc, 2> sensors{
            {{SDL_SENSOR_ACCEL, 100}, {SDL_SENSOR_GYRO, 200}}};
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = 6;
        desc.nbuttons = 15;
        desc.axis_mask = 63;
        desc.button_mask = 32767;
        desc.name = "FORGE motion owner fixture";
        desc.nsensors = 2;
        desc.sensors = sensors.data();
        desc.userdata = this;
        desc.SetSensorsEnabled = mode;
        id = SDL_AttachVirtualJoystick(&desc);
        require(id != 0, SDL_GetError());
        joystick.reset(SDL_OpenJoystick(id));
        require(bool(joystick), SDL_GetError());
    }
    ~GamepadMotionDeviceFixture() {
        joystick.reset();
        if (id)
            SDL_DetachVirtualJoystick(id);
    }
    void send(SDL_SensorType type, const std::array<float, 3>& value, Uint64 timestamp) {
        require(SDL_SendJoystickVirtualSensorData(joystick.get(), type, timestamp, value.data(), 3),
                SDL_GetError());
    }
};
} // namespace forge::test
