CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(pdp11unix)

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR})

# The classic PDP-11 UNIX (V6/V7 lineage) personality: a dylib linking the nix
# host layer.  Unlike the m88k personalities it needs no sc2int-generated table
# -- the `sys`-trap dispatch is a hand-written switch over the nix_ host calls.
# Built as a real COM .abi bundle (pdp11unix.abi), matched on `--abi pdp11unix` (or `pdp11`).
libcpu_add_abi(pdp11unix pdp11unix
  MATCH pdp11unix pdp11
  SOURCES pdp11unix-personality.c pdp11unix-abi.cpp
  LIBS nix LibCPU)
