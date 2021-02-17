#include <opencv4/opencv2/opencv.hpp>
#include <SFML/Audio.hpp>
#include <sys/ioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>

// Global clock.
auto global_time = std::chrono::steady_clock::now();

// Communication between print and createFrames threads.
std::string buffer[2];
bool frame_done[2];
bool print_done[2];

// Communication between all threads.
bool stop_program = false;
bool video_paused = false;
bool restart = false;
bool clear = false;

// Global variables set by flags.
bool show_status_text = false;
int color_threshold = 0;
bool play_audio = true;
bool loop = false;
bool debug = false;
bool center = true;

void createFrames(cv::VideoCapture cap) {
	int current_buffer = 0;

	// Create variables.
	auto frame_start = std::chrono::steady_clock::now();
	std::vector<long double> frame_times;
	long double frame_count = 0;
	long double fps = cap.get(cv::CAP_PROP_FPS);
	cv::Mat frame;
	int prev_r = 1e9;
	int prev_g = 0;
	int prev_b = 0;
	int prev_lines = 0;
	int prev_cols = 0;
	while (1) {
		// Catch up with the video.
		long double current_time_s = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - global_time).count() / 1000.0;
		long double new_frame_count = current_time_s * fps;

		if (!restart) {
			// Fast catching up if difference isn't big.
			while (new_frame_count > frame_count && new_frame_count - frame_count < fps) {
				cap >> frame;
				++frame_count;
			}
			if (new_frame_count < frame_count && frame_count - new_frame_count < fps) {
				long double seconds = (frame_count - new_frame_count) / fps;
				std::this_thread::sleep_for(std::chrono::milliseconds((int)(seconds * 1000)));
			}
		}

		// Match the global time.
		while (restart || abs(new_frame_count - frame_count) >= fps) {
			// Fast catching up if difference isn't big.
			bool skip = false;
			if (!restart) {
				while (new_frame_count > frame_count && new_frame_count - frame_count < fps) {
					cap >> frame;
					++frame_count;
					skip = true;
				}
				if (new_frame_count < frame_count && frame_count - new_frame_count < fps) {
					long double seconds = (frame_count - new_frame_count) / fps;
					std::this_thread::sleep_for(std::chrono::milliseconds((int)(seconds * 1000)));
					skip = true;
				}
			}
			if (skip) break;

			restart = false;
			cap.set(cv::CAP_PROP_POS_FRAMES, (int)new_frame_count);
			frame_count = new_frame_count;
			current_time_s = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - global_time).count() / 1000.0;
			new_frame_count = current_time_s * fps;
		}

		// Read frame.
		cap >> frame;

		if (frame.empty()) {
			if (loop) {
				restart = true;
				global_time = std::chrono::steady_clock::now();
				continue;
			}
			stop_program = true;
			return;
		}

		// Calculate terminal size.
		struct winsize w;
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
		long double lines = w.ws_row;
		long double cols = w.ws_col;
		long double terminal_height = w.ws_ypixel;
		long double terminal_width = w.ws_xpixel;

		// If terminal size is unavailable assume that it is 16:9.
		if (terminal_width == 0 || terminal_height == 0) {
			terminal_height = 9;
			terminal_width = 16;
		}

		// Remove 1 from lines to prevent too many newlines.
		lines -= 1;

		// Calculate font aspect ratio.
		long double aspect_ratio_scale = (cols * terminal_height) / (lines * terminal_width);

		// Calculate new width and height by scaling to terminal resolution (columns x lines).
		long double width = frame.size().width;
		long double height = frame.size().height;
		long double width_scale = width / cols;
		long double height_scale = height / lines;
		long double scale = std::max(width_scale, height_scale / aspect_ratio_scale);
		width = std::min((int)width, (int)std::floor(frame.size().width / scale));
		height = std::min((int)(height / aspect_ratio_scale), (int)std::floor(frame.size().height / (scale * aspect_ratio_scale)));

		// Resize frame to previously calculated width and height.
		cv::resize(frame, frame, cv::Size(std::floor(width), std::floor(height)));

		std::string status_text = "";

		if (show_status_text) {
			status_text += "Res: " + std::to_string((int)width) + "x" + std::to_string((int)(aspect_ratio_scale * height));

			// Calculate framerate.
			auto current_time = std::chrono::steady_clock::now();
			long double milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - frame_start).count();
			frame_start = std::chrono::steady_clock::now();
			frame_times.push_back(milliseconds);

			int amount = 0;
			long double sum = 0;
			int size = frame_times.size() - 1;
			for (int i = size; i > 0 && amount < 20; --i) {
				++amount;
				sum += frame_times[i];
			}
			status_text += "|fps: " + std::to_string((int)(1000 / (sum / amount)));
		}

		buffer[current_buffer] = "";
		if (clear || (int)lines != prev_lines || (int)cols != prev_cols) {
			buffer[current_buffer] += "\33[0m\33[3J\33[2J";
			prev_r = 1e9;
			prev_lines = (int)lines;
			prev_cols = (int)cols;
			clear = false;
		}
		int current_line = ((lines + 1) - height) / 2 + 1;
		int start_column = (cols - width) / 2 + 1;
		buffer[current_buffer] += "\33[1;1H";

		int i = 0;
		int len = status_text.length();
		// Create frame.
		for (int y = 0; y < height; ++y) {
			if (center) buffer[current_buffer] += "\33[" + std::to_string(current_line) + ";" + std::to_string(start_column) + "H";
			++current_line;
			for (int x = 0; x < width; ++x) {
				int b = frame.at<cv::Vec3b>(y, x)[0];
				int g = frame.at<cv::Vec3b>(y, x)[1];
				int r = frame.at<cv::Vec3b>(y, x)[2];
				std::string c = " ";
				if (i != len) {
					// Invert color.
					int inv_color = 0xFFFFFF - (r << 16 | g << 8 | b);
					int inv_r = (inv_color >> 16) & 0xFF;
					int inv_g = (inv_color >> 8) & 0xFF;
					int inv_b = (inv_color) & 0xFF;
					c = "\33[38;2;" + std::to_string(inv_r) + ";" + std::to_string(inv_g) + ";" + std::to_string(inv_b) + "m" + status_text[i];
					++i;
				}
				// Add pixel to frame.
				// Check if color should be changed.
				if (abs(r - prev_r) + abs(g - prev_g) + abs(b - prev_b) > color_threshold) {
					prev_r = r;
					prev_g = g;
					prev_b = b;
					buffer[current_buffer] += "\x1b[48;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
				}
				buffer[current_buffer] += c;
			}
			if (!center) buffer[current_buffer] += "\n";
		}
		++frame_count;

		frame_done[current_buffer] = true;
		print_done[current_buffer] = false;
		current_buffer = (current_buffer + 1) % 2;
		while (1) {
			if (stop_program) return;
			if (!video_paused && print_done[current_buffer]) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}

void print() {
	int current_buffer = 0;

	while (1) {
		while (1) {
			if (stop_program) return;
			if (!video_paused && frame_done[current_buffer]) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		// Print frame.
		printf(buffer[current_buffer].c_str());
		print_done[current_buffer] = true;
		frame_done[current_buffer] = false;
		current_buffer = (current_buffer + 1) % 2;
	}
}

void getInputs() {
	auto pause_time = std::chrono::steady_clock::now();
	auto current_time = std::chrono::steady_clock::now();
	while(1) {
		char c = getc(stdin);
		switch(c) {
			case 'c':
				center = !center;
				clear = true;
				break;
			case 'j':
				global_time += std::chrono::seconds(5);
				current_time = std::chrono::steady_clock::now();
				if (std::chrono::operator>(global_time, current_time)) global_time = current_time;
				break;
			case 'k':
				if (video_paused) {
					current_time = std::chrono::steady_clock::now();
					int milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - pause_time).count();
					global_time += std::chrono::milliseconds(milliseconds);
				} else pause_time = std::chrono::steady_clock::now();
				video_paused = !video_paused;
				break;
			case 'l':
				global_time -= std::chrono::seconds(5);
				break;
			case 'q':
				stop_program = true;
				return;
			case 'r':
				restart = true;
				global_time = std::chrono::steady_clock::now();
				break;
			case 's':
				show_status_text = !show_status_text;
				break;
			default:
				break;
		}
	}
}

void extractAudio(std::string file_name) {
	if (!play_audio) return;
	// mp4 -> wav conversion.
	std::string command = "ffmpeg -y -i \"" + file_name + "\" audio.wav";
	if (!debug) command +=  " &> /dev/null";
	if (debug) std::cout << command << std::endl;
	int return_code = system(command.c_str());
	if (return_code != 0) {
		if (!debug) {
			std::cout << "\n";
			system("tput reset");
		}
		if (return_code != 65280) std::cout << "ffmpeg returned: " << return_code << ". Exiting..." << std::endl;
		exit(1);
	}
}

void audioPlayer(std::string file_name) {
	if (!play_audio) return;
	sf::Music music;

	// Try to open audio and wait for audio extraction.
	clear = true;
	int sleep_count = 0;
	video_paused = true;
	while (!music.openFromFile(file_name)) {
		if (sleep_count > 10) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		++sleep_count;
	}
	video_paused = false;

	if (!music.openFromFile(file_name)) {
		std::cout << "Error opening audio file" << std::endl;
		exit(1);
	}

	if (debug) std::cout << "Deleteting " << file_name << std::endl;
	remove(file_name.c_str());

	music.play();
	while (1) {
		if (stop_program) return;
		if (music.getStatus() == music.Stopped) music.play();
		if (video_paused && music.getStatus() == music.Playing) music.pause();
		if (!video_paused && music.getStatus() == music.Paused) music.play();
		int current_position = music.getPlayingOffset().asMilliseconds();
		int current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - global_time).count();
		while (abs(current_position - current_time_ms) > 1000) {
			sf::Time current = sf::milliseconds(current_time_ms);
			music.setPlayingOffset(current);
			current_position = music.getPlayingOffset().asMilliseconds();
			current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - global_time).count();
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

void signal_callback_handler(int signum) {
	std::cout << "\n";
	system("tput reset");
    exit(signum);
}

int main(int argc, char **argv) {
	signal(SIGINT, signal_callback_handler);

	std::string file_name = "";
	bool help = false;

	// Manage arguments.
	for (int i = 1; i < argc; ++i) {
		bool skip = false;
		int len = strlen(argv[i]);
		if (argv[i][0] != '-') {
			file_name = argv[i];
			continue;
		}
		for (int j = 1; j < len; ++j) {
			switch(argv[i][j]) {
				case 'a':
					play_audio = false;
					break;
				case 'd':
					debug = true;
					break;
				case 'h':
					help = true;
					break;
				case 'l':
					loop = true;
					break;
				case 't':
					color_threshold = atoi(argv[i+1]);
					skip = true;
					break;
				default:
					std::cout << "Invalid argument: '-" << argv[i][j] << "'" << std::endl;
					exit(1);
			}
		}
		if (skip) ++i;
	}
	if (help || file_name == "") {
		std::cout << "Usage: " << argv[0] << " <args> <filename>\n";
		std::cout << "\t'-a' | Disable audio.\n";
		std::cout << "\t'-d' | Enable debug prints.\n";
		std::cout << "\t'-h' | Show this menu and exit.\n";
		std::cout << "\t'-l' | Loop video.\n";
		std::cout << "\t'-t <color threshold>' | Threshold for changing color. Bigger values result in better performance but lower quality. 0 By default.\n";
		std::cout << "\n";
		std::cout << "Player controls:\n";
		std::cout << "\t'c' | Toggle center video.\n";
		std::cout << "\t'j' | Skip backward by 5 seconds.\n";
		std::cout << "\t'k' | Pause.\n";
		std::cout << "\t'l' | Skip forward by 5 seconds.\n";
		std::cout << "\t'q' | Exit.\n";
		std::cout << "\t'r' | Restart video.\n";
		std::cout << "\t's' | Toggle status text.\n";
		exit(0);
	}

	print_done[0] = true;
	print_done[1] = true;

	// Open video.
	cv::VideoCapture cap(file_name);

	// Check if video is open.
	if (!cap.isOpened()) {
		std::cout << "Error opening video stream or file" << std::endl;
		exit(1);
	}

	// Hide cursor.
	std::cout << "\33[?25l";

	// Don't wait for newline when asking for input.
	static struct termios newt;
	tcgetattr(STDIN_FILENO, &newt);
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);

	// Set time 0 to now.
	global_time = std::chrono::steady_clock::now();

	// Start threads.
	std::thread extractAudioThread(extractAudio, file_name);
	std::thread audioThread(audioPlayer, "audio.wav");
	std::thread printThread(print);
	std::thread bufferThread(createFrames, cap);
	std::thread inputThread(getInputs);

	// Join threads.
	bufferThread.join();
	printThread.join();
	audioThread.join();
	
	// Detach threads.
	inputThread.detach();
	extractAudioThread.detach();

	cap.release();

	// Reset terminal.
	std::cout << "\n";
	system("tput reset");

	exit(0);
}