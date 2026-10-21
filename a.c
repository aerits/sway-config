//  simple status bar
//  Copyright (C) 2026 aerits
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// minimizes opening new processes
// minimizes heap allocations
// likely will segfault on non intel laptops when checking for brightness

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/inotify.h>

// write the current time as a string into the given buffer
// (at least 23 bytes long)
void get_time(char *buffer) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  strftime(buffer, 24, "%a %Y-%m-%d %I:%M %p", t);
}

int get_file(FILE **fp, char *file) {
  *fp = fopen(file, "r");
  return 0;
  /* if (fp == NULL) { */
  /*   perror("fopen"); */
  /*   return 1; */
  /* } */
  /* return 0; */
}

int read_file_to_int(FILE *fp) {
  // Read multiple times without closing
  int out;
  fscanf(fp, "%d", &out);

  // Seek back to the start for the next read
  /* fseek(fp, 0, SEEK_SET); */
  // turns out you cant jsut reread the file, you have to reopen it
  return out;
}

int is_charging() {
  FILE *fp = fopen("/sys/class/power_supply/BAT0/status", "r");
  char c;
  fread(&c, 1, 1, fp);
  fclose(fp);
  /* printf("charging: %c", c); */
  return c == 'C';
}

// returns true if muted
int get_volume(char *buffer) {
  FILE *f = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@", "r");
  int skip_bytes = 10;
  char discard[skip_bytes];
  fread(discard, 1, skip_bytes, f);
  fscanf(f, "%s", buffer);

  char next_char = '\0';
  fread(&next_char, 1, 1, f);
  pclose(f);
  return next_char != '\n';
}

/* int get_notifs() { */
/*   FILE *f = popen("swaync-client -c", "r"); */
/*   int n; */
/*   fscanf(f, "%d", &n); */
/*   pclose(f); */
/*   return n; */
/* } */

float calculate_average_power(float *powers, int len) {
  float total = 0;
  for (int i = len - 1; i >= 0; --i) {
    total += powers[i];
  }
  return total / len;
}

int create_inotifywatch(char *file) {
  int fd = inotify_init1(0);
  if (fd == -1) {
    perror("inotify_init1");
    exit(1);
  }
  int wd = inotify_add_watch(fd, file, IN_MODIFY);
  if (wd == -1) {
    perror("inotify_add_watch");
    exit(1);
  }
  return fd;
}

int main() {
  char vol_buf[5];
  char datetime_buffer[24];
  int num_powers = 3;
  float powers[num_powers];
  int powers_index = 0;
  {
    FILE *power_now = NULL;
    get_file(&power_now, "/sys/class/power_supply/BAT0/power_now");
    float power = read_file_to_int(power_now);
    fclose(power_now);
    for (int i = num_powers - 1; i > 0; --i)
      powers[i] = power;
  }

  int fd_num = 3;
  struct pollfd poll_fds[fd_num];
  {
    int fds[fd_num];
    FILE *f = popen("pactl subscribe", "r");
    fds[0] = fileno(f);
    int flags = fcntl(fds[0], F_GETFL);
    fcntl(fds[0], F_SETFL, flags & O_NONBLOCK);

    fds[1] =
        create_inotifywatch("/sys/class/backlight/intel_backlight/brightness");
    flags = fcntl(fds[1], F_GETFL);
    if (fcntl(fds[1], F_SETFL, flags & O_NONBLOCK) == -1) {
      perror("fcntl");
      exit(1);
    }

    f = popen("swaync-client -s", "r");
    fds[2] = fileno(f);
    flags = fcntl(fds[2], F_GETFL);
    fcntl(fds[2], F_SETFL, flags & O_NONBLOCK);

    for (int i = 0; i < fd_num; i++) {
      poll_fds[i] = (struct pollfd){fds[i], POLLIN, 0};
    }
  }

  FILE *energy_full = NULL;
  get_file(&energy_full, "/sys/class/power_supply/BAT0/energy_full");
  float energy_full_f = (float)read_file_to_int(energy_full);
  fclose(energy_full);

  FILE *max_brightness = NULL;
  get_file(&max_brightness,
           "/sys/class/backlight/intel_backlight/max_brightness");
  float max_brightness_f = (float)read_file_to_int(max_brightness);
  fclose(max_brightness);

  int notifs = 0;

  int percent_i;
  float remaining_battery_time;
  char *battery_emoji;
  float brightness;
  char *vol_emoji;

  char poll_buffer[4096];
  goto recalculate;
  while (1) {
  restart:;
    int fds = poll(poll_fds, fd_num, 5 * 1000);
    if (fds > 0) {
      for (int i = 0; fds > 0 && i < fd_num; i++) {
        struct pollfd *cur = &poll_fds[i];
        if (cur->revents & POLLIN) {
          int len = read(cur->fd, poll_buffer, sizeof(poll_buffer));
          if (len == -1) {
            perror("poll");
            exit(1);
          }
          if (i == 0) {
            // pactl
            if (strstr(poll_buffer, "sink") == NULL) {
              goto restart;
            }
            goto get_vol_or_bright;
          } else if (i == 1) {
            // file watch brightness
            goto get_vol_or_bright;
          } else if (i == 2) {
            // notif
            char *num = poll_buffer + 11;
            notifs = atof(num);
          }
          break;
        }
      }
    } else if (fds == -1) {
      continue;
    } else { // recalculate
    recalculate:;
      FILE *battery_percent = NULL;
      get_file(&battery_percent, "/sys/class/power_supply/BAT0/capacity");
      FILE *energy_now = NULL;
      get_file(&energy_now, "/sys/class/power_supply/BAT0/energy_now");
      FILE *power_now = NULL;
      get_file(&power_now, "/sys/class/power_supply/BAT0/power_now");
      percent_i = read_file_to_int(battery_percent);

      float power_now_f = read_file_to_int(power_now);
      powers[powers_index] = power_now_f;
      powers_index = (powers_index + 1) % (num_powers - 1);
      power_now_f = calculate_average_power(powers, num_powers);

      int is_charging_b = is_charging();
      if (!is_charging_b)
        remaining_battery_time =
            (float)read_file_to_int(energy_now) / power_now_f;
      else {
        remaining_battery_time =
            (energy_full_f - (float)read_file_to_int(energy_now)) / power_now_f;
      }
      fclose(battery_percent);
      fclose(energy_now);
      fclose(power_now);

      if (is_charging_b)
        battery_emoji = "📈";
      else if (percent_i > 20)
        battery_emoji = "🔋";
      else
        battery_emoji = "🪫";

      get_time(datetime_buffer);

      goto draw;
    get_vol_or_bright:;
      FILE *actual_brightness = NULL;
      get_file(&actual_brightness,
               "/sys/class/backlight/intel_backlight/brightness");
      int actual_brightness_i = read_file_to_int(actual_brightness);
      brightness = (float)actual_brightness_i / max_brightness_f * 100;
      fclose(actual_brightness);

      int muted = get_volume(vol_buf);
      if (!muted)
        vol_emoji = "🗣️";
      else
        vol_emoji = "🙊";
    }
  draw:

    if (notifs == 0)
      printf("%d%% / %.2f hrs %s | %.1f%% ☀️ | %s%% %s | %s\n", percent_i,

             remaining_battery_time, battery_emoji, brightness, vol_buf,
             vol_emoji, datetime_buffer);
    else
      printf("%d 🔔 | %d%% / %.2f hrs %s | %.0f%% ☀️ | %s%% %s | %s\n", notifs,
             percent_i, remaining_battery_time, battery_emoji, brightness,
             vol_buf, vol_emoji, datetime_buffer);
    fflush(stdout);
  }
}
