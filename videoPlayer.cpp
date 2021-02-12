#include <opencv4/opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <sys/ioctl.h>
#include <stdio.h>
#include <chrono>
#include <unistd.h>
#include <thread>
#include <vector>

int main(int argc, char **argv) {
	std::ios_base::sync_with_stdio(false);
	std::cin.tie(0);

	bool statusBar = true;

	// Manage arguments
	if (argc < 2) {
		std::cout << "Usage: " << argv[0] << " <args> <filename>" << '\n';
		std::cout << "\t-d disable status bar.\n";
		exit(0);
	}
	for (int i = 1; i < argc - 1; ++i) {
		int len = strlen(argv[i]);
		if (argv[i][0] != '-' || len < 2) {
			std::cout << "Invalid argument: " << argv[i] << '\n';
			exit(0);
		}
		for (int j = 1; j < len; ++j) {
			if (argv[i][j] == 'd') statusBar = false;
			else {
				std::cout << "Invalid argument: -" << argv[i][j] << '\n';
				exit(0);
			}
		}
	}

	// Hide cursor.
	printf("\33[?25l");

	// Open video.
	std::string filename = argv[argc-1];
	cv::VideoCapture cap(filename);

	// Check if video is open.
	if (!cap.isOpened()) {
		std::cout << "Error opening video stream or file" << std::endl;
		return -1;
	}

	// Create variables.
	long double fps = cap.get(cv::CAP_PROP_FPS);
	long double frameCount = 0;
	auto start = std::chrono::steady_clock::now();
	auto frameStart = std::chrono::steady_clock::now();
	std::vector<long double> frameTimes;
	cv::Mat frame;
	while (1) {
		// Read frame.
		cap >> frame;

		// Calculate terminal size.
		struct winsize w;
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
		long double lines = w.ws_row;
		long double cols = w.ws_col;

		// Remove 1 from lines to prevent too many newlines.
		lines -= 1;

		// Calculate font aspect ratio. (Assumes that terminal aspect ratio is 16:9)
		long double fac = (cols * 9) / (lines * 16);

		// Calculate new width and height by scaling to terminal resolution (columns x lines).
		long double width = frame.size().width;
		long double height = frame.size().height;
		long double widthScale = width / cols;
		long double heightScale = height / lines;
		long double scale = std::max(widthScale, heightScale / fac);		
		width = std::min((int)width, (int)std::floor(frame.size().width / scale));
		height = std::min((int)(height / fac), (int)std::floor(frame.size().height / (scale * fac)));

		// Resize frame to previously calculated width and height.
		cv::resize(frame, frame, cv::Size(std::floor(width), std::floor(height)));

		std::string status = "";
		
		if (statusBar) {
			status += "Res: " + std::to_string((int)width) + "x" + std::to_string((int)(fac * height));

			// Calculate framerate
			auto end = std::chrono::steady_clock::now();
			long double milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - frameStart).count();
			frameStart = std::chrono::steady_clock::now();
			frameTimes.push_back(milliseconds);
			int amount = 0;
			long double sum = 0;
			for (int i = frameCount; i > 0 && amount < 20; --i) {
				++amount;
				sum += frameTimes[i];
			}
			status += "|fps: " + std::to_string((int)(1000 / (sum / amount)));

			// Calculate how much the player is behind the original video.
			milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
			status += "|behind by: " + std::to_string((int)std::max((long double)0, (1000 * (milliseconds / 1000 - frameCount / fps)))) + "ms";
		}
		int i = 0;
		int len = status.length();
		std::string asciiFrame = "\33[0;0H";
		// Create frame.
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				int b = frame.at<cv::Vec3b>(y, x)[0];
				int g = frame.at<cv::Vec3b>(y, x)[1];
				int r = frame.at<cv::Vec3b>(y, x)[2];
				std::string c = " ";
				if (i != len) {
					// Invert color.
					int invColor = 0xFFFFFF - (r << 16 | g << 8 | b);
					int rr = (invColor >> 16) & 0xFF;
					int rg = (invColor >> 8) & 0xFF;
					int rb = (invColor) & 0xFF;
					c = "\33[38;2;" + std::to_string(rr) + ";" + std::to_string(rg) + ";" + std::to_string(rb) + "m" + status[i];
					++i;
				}
				// Add pixel to frame.
				asciiFrame += "\x1b[48;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m" + c;
			}
			asciiFrame += "\n";
		}
		// Check if the player should sleep.
		auto end = std::chrono::steady_clock::now();
		long double milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
		long double sleepDuration = frameCount / fps - milliseconds/1000;
		if (sleepDuration > 0) std::this_thread::sleep_for(std::chrono::milliseconds((int)(sleepDuration*1000)));

		// Print frame.
		printf(asciiFrame.c_str());

		++frameCount;
	}
	cap.release();
	return 0;
}
