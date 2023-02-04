#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <SFML/Audio.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <opencv4/opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <vector>
namespace chr = std::chrono;

// Global clock.
auto video_start_time = chr::steady_clock::now();

// Communication between DrawFrames and CreateFrames threads.
std::string buffer[2];
bool frame_done[2];
bool print_done[2] = {true, true};

// Communication between all threads.
bool stop_program = false;
bool video_paused = false;
bool restart = false;
bool clear = false;

// Global variables set by flags.
bool show_status_text = false;
int max_color_diff = 0;
bool play_audio = true;
bool loop = false;
bool center = true;

// CreateFrames variable.
int previous_frame[4096][2160];

void ExitAndClear(int return_code, std::string exit_message = "") {
  remove("audio.wav");
  std::cout << "\n";
  system("tput reset");
  if (exit_message != "") std::cout << exit_message << std::endl;
  exit(return_code);
}

void CreateFrames(cv::VideoCapture cap) {
  int current_buffer = 0;

  // Create variables.
  auto frame_start = chr::steady_clock::now();
  std::vector<double> frame_times;
  int frame_count = 0;
  double fps = cap.get(cv::CAP_PROP_FPS);
  cv::Mat frame;
  int prev_r = 0;
  int prev_g = 0;
  int prev_b = 0;
  int prev_y_pixels = 0;
  int prev_x_pixels = 0;
  int prev_len = 0;
  while (1) {
    auto current_time = chr::steady_clock::now();
    double video_time_s = chr::duration_cast<chr::milliseconds>(current_time - video_start_time).count() / 1000.0;
    int target_frame_count = video_time_s * fps;

    // Skip or go back multiple frames.
    if (restart || abs(target_frame_count - frame_count) >= fps) {
      restart = false;
      cap.set(cv::CAP_PROP_POS_FRAMES, (int)target_frame_count);
      frame_count = target_frame_count;
      continue;
    }

    // Skip frames because we are behind the target.
    while (target_frame_count > frame_count && target_frame_count - frame_count < fps) {
      cap >> frame;
      ++frame_count;
    }
    // Wait because we are ahead of the target.
    if (target_frame_count < frame_count) {
      double seconds = (frame_count - target_frame_count) / fps;
      std::this_thread::sleep_for(chr::milliseconds((int)(seconds * 1000)));
    }

    // Read frame.
    cap >> frame;

    if (frame.empty()) {
      if (loop) {
        restart = true;
        video_start_time = chr::steady_clock::now();
        continue;
      }
      stop_program = true;
      return;
    }

    // Calculate terminal size.
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    double terminal_y_pixels = w.ws_row;
    double terminal_x_pixels = w.ws_col;
    double terminal_height = w.ws_ypixel;
    double terminal_width = w.ws_xpixel;

    // If terminal size is unavailable assume that it is 16:9.
    if (terminal_width == 0 || terminal_height == 0) {
      terminal_height = 9;
      terminal_width = 16;
    }

    // Calculate font aspect ratio.
    double aspect_ratio_scale = (terminal_x_pixels * terminal_height) / (terminal_y_pixels * terminal_width);

    // Calculate new width and height for frame by scaling to terminal resolution.
    double full_frame_width = frame.size().width;
    double full_frame_height = frame.size().height;
    double width_scale = full_frame_width / terminal_x_pixels;
    double height_scale = full_frame_height / (terminal_y_pixels * aspect_ratio_scale);
    double scale = std::max(width_scale, height_scale);

    int frame_width = std::min(terminal_x_pixels, std::round(std::min(full_frame_width, full_frame_width / scale)));
    int frame_height = std::min(
        terminal_y_pixels, std::round(std::min(full_frame_height, full_frame_height / scale) / aspect_ratio_scale));

    // Resize frame.
    cv::resize(frame, frame, cv::Size(frame_width, frame_height));

    std::string status_text = "";
    if (show_status_text) {
      status_text += "Pixels: " + std::to_string(frame_width) + "x" + std::to_string(frame_height);
      status_text += "|res: " + std::to_string(frame_width) + "x" +
                     std::to_string((int)std::round((frame_height * aspect_ratio_scale)));

      // Calculate framerate.
      current_time = chr::steady_clock::now();
      double milliseconds = chr::duration_cast<chr::milliseconds>(current_time - frame_start).count();
      frame_start = current_time;
      frame_times.push_back(milliseconds);

      int amount = 0;
      double sum = 0;
      int size = frame_times.size() - 1;
      for (int i = size; i > 0 && amount < 20; --i) {
        ++amount;
        sum += frame_times[i];
      }
      status_text += "|fps: " + std::to_string((int)(1000 / (sum / amount)));
    }

    bool force_redraw = false;
    buffer[current_buffer] = "";
    if (clear || (int)terminal_y_pixels != prev_y_pixels || (int)terminal_x_pixels != prev_x_pixels) {
      force_redraw = true;
      buffer[current_buffer] += "\33[0m\33[3J\33[2J";
      prev_y_pixels = (int)terminal_y_pixels;
      prev_x_pixels = (int)terminal_x_pixels;
      clear = false;
    }
    int current_line = (terminal_y_pixels - frame_height) / 2 + 1;
    int start_column = (terminal_x_pixels - frame_width) / 2 + 1;
    buffer[current_buffer] += "\33[1;1H";

    int status_text_index = 0;
    int status_text_len = status_text.length();
    bool skip = false;
    // Create frame.
    for (int y = 0; y < frame_height; ++y) {
      if (center)
        buffer[current_buffer] += "\33[" + std::to_string(current_line) + ";" + std::to_string(start_column) + "H";
      ++current_line;

      for (int x = 0; x < frame_width; ++x) {
        int b = frame.at<cv::Vec3b>(y, x)[0];
        int g = frame.at<cv::Vec3b>(y, x)[1];
        int r = frame.at<cv::Vec3b>(y, x)[2];

        // Compare to previous frame.
        int color = (r << 16 | g << 8 | b);
        int prev_frame_r = (previous_frame[x][y] >> 16) & 0xFF;
        int prev_frame_g = (previous_frame[x][y] >> 8) & 0xFF;
        int prev_frame_b = (previous_frame[x][y]) & 0xFF;
        if (status_text_index >= std::max(prev_len, status_text_len) && !force_redraw &&
            abs(r - prev_frame_r) + abs(g - prev_frame_g) + abs(b - prev_frame_b) < max_color_diff) {
          // Don't draw pixel if pixel from previous frame is similar enough.
          skip = true;
          continue;
        } else if (skip) {
          skip = false;
          // Set cursor to correct place
          if (center)
            buffer[current_buffer] +=
                "\33[" + std::to_string(current_line - 1) + ";" + std::to_string(start_column + x) + "H";
          else buffer[current_buffer] += "\33[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H";
        }
        previous_frame[x][y] = color;

        std::string c = " ";
        if (status_text_index < status_text_len) {
          // Invert color.
          int inv_color = 0xFFFFFF - (r << 16 | g << 8 | b);
          int inv_r = (inv_color >> 16) & 0xFF;
          int inv_g = (inv_color >> 8) & 0xFF;
          int inv_b = (inv_color)&0xFF;
          c = "\33[38;2;" + std::to_string(inv_r) + ";" + std::to_string(inv_g) + ";" + std::to_string(inv_b) + "m" +
              status_text[status_text_index];
          ++status_text_index;
        }

        // Compare pixel to previous pixel.
        if (force_redraw || abs(r - prev_r) + abs(g - prev_g) + abs(b - prev_b) > max_color_diff) {
          prev_r = r;
          prev_g = g;
          prev_b = b;
          buffer[current_buffer] +=
              "\x1b[48;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
        }
        buffer[current_buffer] += c;
      }
      if (!center && y != frame_height - 1) buffer[current_buffer] += "\n";
    }
    ++frame_count;
    prev_len = status_text_len;

    frame_done[current_buffer] = true;
    print_done[current_buffer] = false;
    current_buffer = (current_buffer + 1) % 2;
    while (1) {
      if (stop_program) return;
      if (!video_paused && print_done[current_buffer]) break;
      std::this_thread::sleep_for(chr::milliseconds(1));
    }
  }
}

void DrawFrames() {
  int current_buffer = 0;

  while (1) {
    while (1) {
      if (stop_program) return;
      if (!video_paused && frame_done[current_buffer]) break;
      std::this_thread::sleep_for(chr::milliseconds(1));
    }

    // Print frame.
    printf(buffer[current_buffer].c_str());
    print_done[current_buffer] = true;
    frame_done[current_buffer] = false;
    current_buffer = (current_buffer + 1) % 2;
  }
}

void GetInputs() {
  // Change time in video by changing video start time and CreateFrames handles the rest.
  auto pause_time = chr::steady_clock::now();
  while (1) {
    char c = getc(stdin);
    auto current_time = chr::steady_clock::now();
    switch (c) {
      case 'c':
        center = !center;
        clear = true;
        break;
      case 'j':
        video_start_time += chr::seconds(5);
        if (chr::operator>(video_start_time, current_time)) {
          restart = true;
          video_start_time = current_time;
        }
        break;
      case 'k':
        if (video_paused) {
          int milliseconds = chr::duration_cast<chr::milliseconds>(current_time - pause_time).count();
          video_start_time += chr::milliseconds(milliseconds);
        } else pause_time = current_time;
        video_paused = !video_paused;
        break;
      case 'l':
        video_start_time -= chr::seconds(5);
        break;
      case 'q':
        stop_program = true;
        return;
      case 'r':
        restart = true;
        video_start_time = current_time;
        break;
      case 's':
        show_status_text = !show_status_text;
        break;
      default:
        break;
    }
  }
}

void ExtractAudio(std::string file_name) {
  if (!play_audio) return;
  // mp4 -> wav conversion.
  std::string command = "ffmpeg -y -i \"" + file_name + "\" audio.wav &> /dev/null";
  int return_code = system(command.c_str());
  if (return_code != 0) {
    std::string exit_message = "";
    if (return_code != 65280) exit_message = "ffmpeg returned: " + std::to_string(return_code) + ". Exiting...";
    ExitAndClear(1, exit_message);
  }
}

void AudioPlayer() {
  if (!play_audio) return;
  sf::Music music;

  // Try to open audio and wait for audio extraction.
  clear = true;
  video_paused = true;
  int sleep_count = 0;
  while (!music.openFromFile("audio.wav")) {
    if (sleep_count > 100) ExitAndClear(1, "Error opening audio file");
    std::this_thread::sleep_for(chr::milliseconds(10));
    ++sleep_count;
  }
  video_paused = false;

  music.play();
  while (1) {
    if (stop_program) return;
    if (music.getStatus() == music.Stopped) music.play();
    if (video_paused && music.getStatus() == music.Playing) music.pause();
    if (!video_paused && music.getStatus() == music.Paused) music.play();
    int current_position = music.getPlayingOffset().asMilliseconds();
    auto current_time = chr::steady_clock::now();
    int video_time_ms = chr::duration_cast<chr::milliseconds>(current_time - video_start_time).count();
    while (abs(current_position - video_time_ms) > 1000) {
      sf::Time current = sf::milliseconds(video_time_ms);
      music.setPlayingOffset(current);
      current_position = music.getPlayingOffset().asMilliseconds();
      video_time_ms = chr::duration_cast<chr::milliseconds>(current_time - video_start_time).count();
    }

    std::this_thread::sleep_for(chr::milliseconds(1));
  }
}

void SignalCallbackHandler(int signum) {
  ExitAndClear(signum);
}

int main(int argc, char** argv) {
  signal(SIGINT, SignalCallbackHandler);

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
      switch (argv[i][j]) {
        case 'a':
          play_audio = false;
          break;
        case 'd':
          max_color_diff = atoi(argv[i + 1]);
          skip = true;
          break;
        case 'h':
          help = true;
          break;
        case 'l':
          loop = true;
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
    std::cout << "\n";
    std::cout << "Runtime flags:\n";
    std::cout << "\t'-a' | Disable audio.\n";
    std::cout << "\t'-d <max color diff>' | Maximum difference between correct color and displayed "
                 "color. Bigger values result in better performance but lower quality. 0 By default.\n";
    std::cout << "\t'-h' | Show this menu and exit.\n";
    std::cout << "\t'-l' | Loop video.\n";
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
  video_start_time = chr::steady_clock::now();

  // Start threads.
  std::thread extractAudioThread(ExtractAudio, file_name);
  std::thread audioThread(AudioPlayer);
  std::thread printThread(DrawFrames);
  std::thread bufferThread(CreateFrames, cap);
  std::thread inputThread(GetInputs);

  // Join threads.
  bufferThread.join();
  printThread.join();
  audioThread.join();

  // Detach threads.
  inputThread.detach();
  extractAudioThread.detach();

  cap.release();
  ExitAndClear(0);
}
