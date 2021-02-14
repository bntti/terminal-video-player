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

// Communication between print and createFrames threads.
std::string buffer[2];
bool frameDone[2];
bool printDone[2];

// Communication between all threads.
bool stopProgram = false;
bool paused = false;
int move[2];

// Global variables set by flags.
bool statusBar = true;
long double fpscap = 1e9;
int colorThreshold = 0;
bool playAudio = true;

void createFrames(cv::VideoCapture cap, int threadID) {
	int currentBuffer = 0;

	// Create variables.
	auto start = std::chrono::steady_clock::now();
	auto frameStart = std::chrono::steady_clock::now();
	std::vector<long double> frameTimes;
	long double frameCount = 0;
	long double frameCount2 = 0;
	long double fps = cap.get(cv::CAP_PROP_FPS);
	cv::Mat frame;
	int pr = 1e5;
	int pg = 1e5;
	int pb = 1e5;
	int plines = 0;
	int pcols = 0;
	while (1) {
		if (move[threadID] != 0) {
			long double increase = move[threadID];
			if (frameCount < fps * -increase) increase = -frameCount / fps;
			frameCount += (int)(fps * increase);
			frameCount2 += (int)(fpscap * increase);
			start -= std::chrono::milliseconds((int) (increase * 1000));
			cap.set(cv::CAP_PROP_POS_FRAMES, frameCount);
			move[threadID] = 0;
		}
		// Read frame.
		cap >> frame;

		long double frameTime = frameCount / fps;
		long double frameTime2 = frameCount2 / fpscap;
		if (frameTime2 > frameTime) {
			++frameCount;
			continue;
		}

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
			int size = frameTimes.size() - 1;
			for (int i = size; i > 0 && amount < 20; --i) {
				++amount;
				sum += frameTimes[i];
			}
			status += "|fps: " + std::to_string((int)(1000 / (sum / amount)));

			// Calculate how much the player is behind the original video.
			milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
			status += "|behind by: " + std::to_string((int)std::max((long double)0, (1000 * (milliseconds / 1000 - frameCount / fps)))) + "ms";
		}

		buffer[currentBuffer] = "";
		if ((int)lines != plines || (int)cols != pcols) {
			buffer[currentBuffer] += "\33[0m\33[3J\33[2J";
			pr = 1e9;
			plines = (int)lines;
			pcols = (int)cols;
		}
		buffer[currentBuffer] += "\33[0;0H";

		int i = 0;
		int len = status.length();
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
				// Check if color should be changed.
				if (abs(r - pr) + abs(g - pg) + abs(b - pb) > colorThreshold) {
					pr = r;
					pg = g;
					pb = b;
					buffer[currentBuffer] += "\x1b[48;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
				}
				buffer[currentBuffer] += c;
			}
			buffer[currentBuffer] += "\n";
		}
		++frameCount;
		++frameCount2;

		frameDone[currentBuffer] = true;
		printDone[currentBuffer] = false;
		currentBuffer = (currentBuffer + 1) % 2;
		while (1) {
			if (stopProgram) return;
			if (paused) start += std::chrono::milliseconds(1);
			if (!paused && printDone[currentBuffer]) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}

void print(cv::VideoCapture cap) {
	int currentBuffer = 0;

	// Create variables.
	long double fps = cap.get(cv::CAP_PROP_FPS);
	long double frameCount = 0;
	auto start = std::chrono::steady_clock::now();
	while (1) {
		while (1) {
			if (stopProgram) return;
			if (paused) start += std::chrono::milliseconds(1);
			if (!paused && frameDone[currentBuffer]) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		// Check if the player should sleep.
		auto end = std::chrono::steady_clock::now();
		long double milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
		long double sleepDuration = frameCount / std::min(fps, (long double)fpscap) - milliseconds/1000;
		if (sleepDuration > 0) std::this_thread::sleep_for(std::chrono::milliseconds((int)(sleepDuration*1000)));

		// Print frame.
		printf(buffer[currentBuffer].c_str());
		++frameCount;
		printDone[currentBuffer] = true;
		frameDone[currentBuffer] = false;
		currentBuffer = (currentBuffer + 1) % 2;
	}
}

void getInputs() {
    while(1) {
        char c = getc(stdin);
		switch(c) {
			case 'j':
				for (int& x : move) x -= 5;
				break;
			case 'k':
				paused = !paused;
				break;
			case 'l':
				for (int& x : move) x += 5;
				break;
			case 'q':
				stopProgram = true;
				return;
			default:
				break;
		}
    }
}

void audioPlayer(std::string fileName, int threadID) {
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
		if (paused && music.getStatus() == music.Playing) music.pause();
		if (!paused && music.getStatus() == music.Paused) music.play();
		if (move[threadID] != 0) {
			sf::Time current = music.getPlayingOffset();
			current += sf::seconds(move[threadID]);
			if (current.asSeconds() <= 0) current = current.Zero;
			music.setPlayingOffset(current);
			move[threadID] = 0;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

int main(int argc, char **argv) {
	std::string fileName = "";
	bool help = false;

	// Manage arguments
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
				case 'f':
					fpscap = atof(argv[i+1]);
					skip = true;
					break;
				case 'h':
					help = true;
					break;
				case 's':
					statusBar = false;
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
		std::cout << "\t'-f <fps>' | Set fps cap.\n";
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
	printf("\33[?25l");

	// Don't wait for newline when asking for input.
	static struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

	// Start threads.
	std::thread audioThread(audioPlayer, "audio.ogg", 0);
	std::thread printThread(print, cap);
	std::thread bufferThread(createFrames, cap, 1);
	std::thread inputThread(getInputs);
	inputThread.join();
	bufferThread.join();
	printThread.join();
	audioThread.join();
	cap.release();

	// Reset terminal.
	system("tput reset");

	tcsetattr( STDIN_FILENO, TCSANOW, &oldt);
	return 0;
}
