# CMake generated Testfile for 
# Source directory: /home/li/disruptor-cpp
# Build directory: /home/li/disruptor-cpp/build-release
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[disruptor_tests]=] "/home/li/disruptor-cpp/build-release/disruptor_tests")
set_tests_properties([=[disruptor_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/li/disruptor-cpp/CMakeLists.txt;70;add_test;/home/li/disruptor-cpp/CMakeLists.txt;0;")
subdirs("external/backward-cpp")
subdirs("external/concurrentqueue")
subdirs("external/readerwriterqueue")
subdirs("external/Catch2")
