include_guard(GLOBAL)
# One immutable coordinated source selection for editor and headless asset tools.
# Each consumer configures its native targets before MakeAvailable.
FetchContent_Declare(diligent GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentEngine.git
 GIT_TAG a279e5fa8593cbc758ec46ea1eba0b435cbc2f06)
