CXXFLAGS := -I /usr/include/opencv4 -lsfml-system -lsfml-audio -lopencv_shape -lopencv_videoio -lopencv_imgproc -lopencv_core -lpthread -Wshadow -Wall -Wextra -O3 -march=native
videoPlayer: videoPlayer.cpp
	$(CXX) $(CXXFLAGS) $? -o $@

clean:
	-rm -rf videoPlayer

.PHONY: clean