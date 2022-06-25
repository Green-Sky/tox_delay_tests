cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

project(tox_delay_tests C CXX)

########################################

add_executable(test1_receiver
	./test1_common.hpp
	./test1_receiver.cpp
)

target_compile_features(test1_receiver PUBLIC cxx_std_17)

target_link_libraries(test1_receiver
	toxcore
)

########################################

add_executable(test1_sender
	./test1_common.hpp
	./test1_sender.cpp
)

target_compile_features(test1_sender PUBLIC cxx_std_17)

target_link_libraries(test1_sender
	toxcore
)

