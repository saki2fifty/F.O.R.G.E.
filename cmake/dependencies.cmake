include(FetchContent)
set(FLECS_STATIC ON CACHE BOOL "" FORCE)
set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
set(FLECS_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(flecs GIT_REPOSITORY https://github.com/SanderMertens/flecs.git GIT_TAG fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8) # v4.1.6
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG 55f93686c01528224f448c19128836e7df245f72)
FetchContent_MakeAvailable(flecs json)
