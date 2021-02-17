CXXFLAGS := -I /usr/include/opencv4 -lsfml-system -lsfml-audio -lopencv_shape -lopencv_videoio -lopencv_imgproc -lopencv_core -lpthread -Wshadow -Wall -Wextra -O3 -march=native
tplayer: terminal_video_player.cpp
	$(CXX) $(CXXFLAGS) $? -o $@

clean:
	-rm -rf tplayer

.PHONY: clean
