#include <opencv4/opencv2/opencv.hpp>
#include <SFML/Audio.hpp>
#include <iostream>
#include <string>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <unistd.h>
#include <thread>
#include <vector>
#include <termios.h>

// Global variables.
auto StartTime = std::chrono::steady_clock::now();

// Global clock.
auto globalTime = std::chrono::steady_clock::now();

// Communication between print and createFrames threads.
std::string buffer[2];
bool frameDone[2];
bool printDone[2];

// Communication between all threads.
bool stopProgram = false;
bool videoPaused = false;

// Global variables set by flags.
bool showStatusText = true;
int colorThreshold = 0;
bool playAudio = true;

void createFrames(cv::VideoCapture cap) {
	int currentBuffer = 0;

	// Create variables.
	auto frameStart = std::chrono::steady_clock::now();
	std::vector<long double> frameTimes;
	long double frameCount = 0;
	long double fps = cap.get(cv::CAP_PROP_FPS);
	cv::Mat frame;
	int prevR = 1e5;
	int prevG = 1e5;
	int prevB = 1e5;
	int prevLines = 0;
	int prevCols = 0;
	while (1) {
		// Catch up with the video.
		long double currentTimeS = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - globalTime).count() / 1000.0;
		long double newFrameCount = currentTimeS * fps;

		// Match the global time.
		while (abs(newFrameCount - frameCount) > fps) {
			cap.set(cv::CAP_PROP_POS_FRAMES, (int)newFrameCount);
			frameCount = newFrameCount;
			currentTimeS = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - globalTime).count() / 1000.0;
			newFrameCount = currentTimeS * fps;
		}

		// Read frame.
		cap >> frame;

		if (frame.empty()) {
			stopProgram = true;
			return;
		}

		// Calculate terminal size.
		struct winsize w;
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
		long double lines = w.ws_row;
		long double cols = w.ws_col;

		// Remove 1 from lines to prevent too many newlines.
		lines -= 1;

		// Calculate font aspect ratio. (Assumes that terminal aspect ratio is 16:9).
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

		std::string statusText = "";

		if (showStatusText) {
			statusText += "Res: " + std::to_string((int)width) + "x" + std::to_string((int)(fac * height));

			// Calculate framerate.
			auto currentTime = std::chrono::steady_clock::now();
			long double milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - frameStart).count();
			frameStart = std::chrono::steady_clock::now();
			frameTimes.push_back(milliseconds);

			int amount = 0;
			long double sum = 0;
			int size = frameTimes.size() - 1;
			for (int i = size; i > 0 && amount < 20; --i) {
				++amount;
				sum += frameTimes[i];
			}
			statusText += "|fps: " + std::to_string((int)(1000 / (sum / amount)));
		}

		buffer[currentBuffer] = "";
		if ((int)lines != prevLines || (int)cols != prevCols) {
			buffer[currentBuffer] += "\33[0m\33[3J\33[2J";
			prevR = 1e9;
			prevLines = (int)lines;
			prevCols = (int)cols;
		}
		buffer[currentBuffer] += "\33[0;0H";

		int i = 0;
		int len = statusText.length();
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
					c = "\33[38;2;" + std::to_string(rr) + ";" + std::to_string(rg) + ";" + std::to_string(rb) + "m" + statusText[i];
					++i;
				}
				// Add pixel to frame.
				// Check if color should be changed.
				if (abs(r - prevR) + abs(g - prevG) + abs(b - prevB) > colorThreshold) {
					prevR = r;
					prevG = g;
					prevB = b;
					buffer[currentBuffer] += "\x1b[48;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
				}
				buffer[currentBuffer] += c;
			}
			buffer[currentBuffer] += "\n";
		}
		++frameCount;

		// Check if the player should sleep.
		auto currentTime = std::chrono::steady_clock::now();
		long double milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - globalTime).count();
		long double sleepDuration = frameCount / fps - milliseconds / 1000;
		if (0.1 > sleepDuration && sleepDuration > 0) std::this_thread::sleep_for(std::chrono::milliseconds((int)(sleepDuration * 1000)));

		frameDone[currentBuffer] = true;
		printDone[currentBuffer] = false;
		currentBuffer = (currentBuffer + 1) % 2;
		while (1) {
			if (stopProgram) return;
			if (!videoPaused && printDone[currentBuffer]) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}

void print() {
	int currentBuffer = 0;

	while (1) {
		while (1) {
			if (stopProgram) return;
			if (!videoPaused && frameDone[currentBuffer]) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		// Print frame.
		printf(buffer[currentBuffer].c_str());
		printDone[currentBuffer] = true;
		frameDone[currentBuffer] = false;
		currentBuffer = (currentBuffer + 1) % 2;
	}
}

void getInputs() {
	auto pauseTime = std::chrono::steady_clock::now();
	auto currentTime = std::chrono::steady_clock::now();
	while(1) {
		char c = getc(stdin);
		switch(c) {
			case 'j':
				globalTime += std::chrono::seconds(5);
				currentTime = std::chrono::steady_clock::now();
				if (std::chrono::operator>(globalTime, currentTime)) globalTime = currentTime;
				break;
			case 'k':
				if (videoPaused) {
					currentTime = std::chrono::steady_clock::now();
					int milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - pauseTime).count();
					globalTime += std::chrono::milliseconds(milliseconds);
				} else pauseTime = std::chrono::steady_clock::now();
				videoPaused = !videoPaused;
				break;
			case 'l':
				globalTime -= std::chrono::seconds(5);
				break;
			case 'q':
				stopProgram = true;
				return;
			default:
				break;
		}
	}
}

void audioPlayer(std::string fileName) {
	sf::Music music;
	if (!playAudio) return;
	if (!music.openFromFile(fileName)) {
		std::cout << "Error opening audio file" << std::endl;
		exit(-1);
	}
	std::cout << "Deleteting " << fileName << std::endl;
	remove(fileName.c_str());

	music.play();
	while (1) {
		if (stopProgram) return;
		if (videoPaused && music.getStatus() == music.Playing) music.pause();
		if (!videoPaused && music.getStatus() == music.Paused) music.play();
		int currentPosition = music.getPlayingOffset().asMilliseconds();
		int currentTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - globalTime).count();
		while (abs(currentPosition - currentTimeMs) > 1000) {
			sf::Time current = sf::milliseconds(currentTimeMs);
			music.setPlayingOffset(current);
			currentPosition = music.getPlayingOffset().asMilliseconds();
			currentTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - globalTime).count();
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

int main(int argc, char **argv) {
	std::string fileName = "";
	bool help = false;

	// Manage arguments.
	for (int i = 1; i < argc; ++i) {
		bool skip = false;
		int len = strlen(argv[i]);
		if (argv[i][0] != '-') {
			fileName = argv[i];
			continue;
		}
		for (int j = 1; j < len; ++j) {
			switch(argv[i][j]) {
				case 'a':
					playAudio = false;
					break;
				case 'c':
					colorThreshold = atoi(argv[i+1]);
					skip = true;
					break;
				case 'h':
					help = true;
					break;
				case 's':
					showStatusText = false;
					break;
				default:
					std::cout << "Invalid argument: -" << argv[i][j] << '\n';
					exit(0);
			}
		}
		if (skip) ++i;
	}
	if (help || fileName == "") {
		std::cout << "Usage: " << argv[0] << " <args> <filename>\n";
		std::cout << "\t'-a' | Disable audio.\n";
		std::cout << "\t'-c <color threshold>' | Threshold for changing color. Bigger values result in better performance but lower quality. 0 By default.\n";
		std::cout << "\t'-h' | Show this menu and exit.\n";
		std::cout << "\t'-s' | Disable status text.\n";
		std::cout << "\n";
		std::cout << "Player controls:\n";
		std::cout << "\t'j' | Skip backward by 5 seconds.\n";
		std::cout << "\t'k' | Pause.\n";
		std::cout << "\t'l' | Skip forward by 5 seconds.\n";
		std::cout << "\t'q' | Exit.\n";
		exit(0);
	}

	printDone[0] = true;
	printDone[1] = true;

	// Open video.
	cv::VideoCapture cap(fileName);

	// Check if video is open.
	if (!cap.isOpened()) {
		std::cout << "Error opening video stream or file" << std::endl;
		exit(-1);
	}

	// Play audio.
	if (playAudio) {
		std::cout << "Extraction audio" << std::endl;
		std::string command = "ffmpeg -y -i \"" + fileName + "\" tmp.mp3 &> /dev/null";
		std::cout << command << std::endl;
		system(command.c_str());
		command = "ffmpeg -y -i tmp.mp3 audio.ogg &> /dev/null";
		std::cout << command << std::endl;
		system(command.c_str());
		std::cout << "Deleteting tmp.mp3" << std::endl;
		remove("tmp.mp3");
	}

	// Hide cursor.
	std::cout << "\33[?25l";

	// Don't wait for newline when asking for input.
	static struct termios newt;
	tcgetattr(STDIN_FILENO, &newt);
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);

	// Set time 0 to now.
	StartTime = std::chrono::steady_clock::now();
	globalTime = StartTime;

	// Start threads.
	std::thread audioThread(audioPlayer, "audio.ogg");
	std::thread printThread(print);
	std::thread bufferThread(createFrames, cap);
	std::thread inputThread(getInputs);

	// Join threads.
	bufferThread.join();
	printThread.join();
	audioThread.join();
	
	// Detach input thread because it may be stuck waiting for input.
	inputThread.detach();

	cap.release();

	// Reset terminal.
	system("tput reset");

	return 0;
}
