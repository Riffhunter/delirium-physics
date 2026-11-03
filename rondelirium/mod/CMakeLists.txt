set(TARGET RoNDelirium)

add_library(${TARGET} SHARED
    src/dllmain.cpp
)

target_include_directories(${TARGET} PRIVATE src)
target_link_libraries(${TARGET} PUBLIC UE4SS)
# The debug file's name only in the DLL, not the build machine's path
target_link_options(${TARGET} PRIVATE "/PDBALTPATH:%_PDB%")

# The developer's tools (F7 probe, GTA IV comparison runs): off in releases.
option(RONDELIRIUM_DEV "Build the developer's tools into RoNDelirium" OFF)
if(RONDELIRIUM_DEV)
    target_compile_definitions(${TARGET} PRIVATE RONDELIRIUM_DEV)
endif()
