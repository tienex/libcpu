CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(pdp11unix)

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR})

# The classic PDP-11 UNIX (V6/V7 lineage) personality: a dylib linking the nix
# host layer.  Unlike the m88k personalities it needs no sc2int-generated table
# -- the `sys`-trap dispatch is a hand-written switch over the nix_ host calls.
ADD_LIBRARY(pdp11unix SHARED pdp11unix-personality.c)
TARGET_LINK_LIBRARIES(pdp11unix nix)
