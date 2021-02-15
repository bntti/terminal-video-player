# Terminal video player
Plays various video formats inside a terminal. Can also be used to play video from a webcam or mobile phone. Can be combined with [mpsyt](https://github.com/mps-youtube/mps-youtube) to play videos straight from youtube.

## Dependencies:
* opencv
* sfml

## Usage
To compile the program run: `make`

Usage: `./videoPlayer <args> <filename>`

A hardware accelerated terminal (for example [alacritty](https://github.com/alacritty/alacritty)) is recommended for better performance. 

#### Runtime flags:
| Flag                   | Description                                                                                               |
| ---------------------- | --------------------------------------------------------------------------------------------------------- |
| `-a`                   | Disable audio.                                                                                            |
| `-c <color threshold>` | Threshold for changing color. Bigger values result in better performance but lower quality. 0 by default. |
| `-h`                   | Show this menu and exit.                                                                                  |
| `-l`                   | Loop video.                                                                                               |
| `-s`                   | Disable status text.                                                                                      |

#### Player controls
| Control | Description                |
| ------- | -------------------------- |
| `j`     | Skip backward by 5 seconds.|
| `k`     | Pause.                     |
| `l`     | Skip forward by 5 seconds. |
| `q`     | Exit.                      |
| `r`     | Restart video.             |
