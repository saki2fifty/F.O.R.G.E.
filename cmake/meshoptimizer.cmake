# Official stable 1.2. Private offline geometry processing; no runtime SDK ABI.
foreach(option MESHOPT_BUILD_DEMO MESHOPT_BUILD_GLTFPACK MESHOPT_BUILD_SHARED_LIBS
 MESHOPT_INSTALL MESHOPT_WERROR)
 set(${option} OFF CACHE BOOL "Private FORGE geometry tooling" FORCE)
endforeach()
FetchContent_Declare(meshoptimizer
 GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
 GIT_TAG 9d9890c73011d75920af614485296d1e03e95448
 EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(meshoptimizer)
