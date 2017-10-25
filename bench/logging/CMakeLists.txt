#
# File: bench/shared_counter/CMakeLists.txt
# Part of commonpp.
#
# Distributed under the 2-clause BSD licence (See LICENCE.TXT file at the
# project root).
#
# Copyright (c) 2015 Thomas Sanchez.  All rights reserved.
#

add_executable(logging_perf  main.cpp)
target_link_libraries(logging_perf commonpp boost_program_options boost_system boost_timer) 

