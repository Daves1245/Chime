#include <unistd.h>
#include <ncurses.h>

int h, w;

void draw_border(WINDOW *scr, int x, int y) {
  for (int i = 0; i < x; i++) {
    for (int j = 0; j < y; j++) {
      int wall = i == 0 || i == x - 1;
      int ceil = j == 0 || j == y - 1;
      wmove(scr, i, j);
      if (wall && ceil) {
        waddch(scr, '+');
      } else if (wall) {
        waddch(scr, '|');
      } else if (ceil) {
        waddch(scr, '-');
      }
    }
  }
}

int main() {
  initscr();
  getmaxyx(stdscr, h, w);
  wprintw(stdscr, "hello");
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  draw_border(stdscr, h, w);
  sleep(5);
  endwin();
}
