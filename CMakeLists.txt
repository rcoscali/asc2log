cmake_minimum_required(VERSION 3.14.0)

project(asc2log C)

add_executable(asc2log
  src/asc2log.c
  src/lib.c
)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wno-parentheses -DAF_CAN=PF_CAN -DPF_CAN=29 -DSO_RXQ_OVFL=40 -DSCM_TIMESTAMPING_OPT_STATS=54 -DCLOCK_TAI=11 -DSO_TXTIME=61 -DSCM_TXTIME=SO_TXTIME -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE")

include_directories(asc2log
  ${ASC2LOG_SOURCE_DIR}/include
)

install(TARGETS
  asc2log
)
