// simple status bar
// minimizes opening new processes
// minimizes heap allocations
// likely will segfault on different devices when checking for brightness
#include <stdio.h>
#include <time.h>
#include <unistd.h>

// write the current time as a string into the given buffer
// (at least 23 bytes long)
void get_time(char *buffer) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  strftime(buffer, 23, "%a %Y-%m-%d %I:%M %p", t);
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
  int skip_bytes = 8;
  char discard[skip_bytes];
  fread(discard, 1, skip_bytes, f);
  fscanf(f, "%s", buffer);
  int fd = fileno(f);

  char next_char = '\0';
  fread(&next_char, 1, 1, f);
  pclose(f);
  return next_char != '\n';
}

int get_notifs() {
  FILE *f = popen("swaync-client -c", "r");
  int n;
  fscanf(f, "%d", &n);
  pclose(f);
  return n;
}

int main() {
  char vol_buf[5];
  char datetime_buffer[24];

  while (1) {

    FILE *battery_percent = NULL;
    get_file(&battery_percent, "/sys/class/power_supply/BAT0/capacity");
    FILE *energy_now = NULL;
    get_file(&energy_now, "/sys/class/power_supply/BAT0/energy_now");
    FILE *power_now = NULL;
    get_file(&power_now, "/sys/class/power_supply/BAT0/power_now");
    int percent_i = read_file_to_int(battery_percent);

    float remaining_battery_time;
    int is_charging_b = is_charging();
    if (!is_charging_b)
      remaining_battery_time =
          (float)read_file_to_int(energy_now) / read_file_to_int(power_now);
    else {

      FILE *energy_full = NULL;
      get_file(&energy_full, "/sys/class/power_supply/BAT0/energy_full");

      remaining_battery_time = ((float)read_file_to_int(energy_full) -
                                (float)read_file_to_int(energy_now)) /
                               (float)read_file_to_int(power_now);
    }
    fclose(battery_percent);
    fclose(energy_now);
    fclose(power_now);

    char *battery_emoji;
    if (is_charging_b)
      battery_emoji = "📈";
    else if (percent_i > 20)
      battery_emoji = "🔋";
    else
      battery_emoji = "🪫";

    // cant just reuse the same file descriptor for brightness ig
    FILE *actual_brightness = NULL;
    get_file(&actual_brightness,
             "/sys/class/backlight/intel_backlight/actual_brightness");
    FILE *max_brightness = NULL;
    get_file(&max_brightness,
             "/sys/class/backlight/intel_backlight/max_brightness");
    int actual_brightness_i = read_file_to_int(actual_brightness);
    int max_brightness_i = read_file_to_int(max_brightness);
    float brightness = (float)actual_brightness_i / max_brightness_i * 100;
    fclose(actual_brightness);
    fclose(max_brightness);

    int muted = get_volume(vol_buf);
    char *vol_emoji;
    if (!muted)
      vol_emoji = "🗣️";
    else
      vol_emoji = "🙊";

    int notifs = get_notifs();

    get_time(datetime_buffer);

    if (notifs == 0)
      printf("%d%% / %.2f hrs %s | %.1f%% ☀️ | %s%% %s | %s\n", percent_i,

             remaining_battery_time, battery_emoji, brightness, vol_buf,
             vol_emoji, datetime_buffer);
    else
      printf("%d 🔔 | %d%% / %.2f hrs %s | %.1f%% ☀️ | %s%% %s | %s\n", notifs,
             percent_i, remaining_battery_time, battery_emoji, brightness,
             vol_buf, vol_emoji, datetime_buffer);
    fflush(stdout);
    sleep(1);
  }
}
