cmake_minimum_required(VERSION 3.22)
project(ReadyOrNotDelirium)

# Which UE4SS source to build against: a C++ mod only loads on the UE4SS build it was made for.
# Another build of the mod: build_on.bat <commit> (a checkout of that commit, here 83330635 by default; 35d1795d for SWAT Unleashed 0.9.7).
set(UE4SS_SOURCE "RE-UE4SS" CACHE STRING "The UE4SS checkout to build against, relative to this folder")
add_subdirectory(${UE4SS_SOURCE})
add_subdirectory(mod)
