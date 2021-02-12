#!/bin/bash
g++ videoPlayer.cpp -o videoPlayer \
	-I /usr/include/opencv4 \
	-lopencv_shape -lopencv_videoio -lopencv_imgproc -lopencv_core -lpthread \
	-Wshadow -Wall -Wextra -O3 -march=native
