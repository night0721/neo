#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define MAX_LINES 10000
#define MAX_LINE 4096
#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"

char **lines;
int count;
int selected;
char query[MAX_LINE];
int query_len;
struct termios orig_termios;
int rows = 24, cols = 80;
int tty_fd;

void get_window_size(void)
{
	struct winsize ws;
	if (ioctl(tty_fd, TIOCGWINSZ, &ws) != -1) {
		rows = ws.ws_row;
		cols = ws.ws_col;
	}
}

void handle_sigwinch(int sig)
{
	get_window_size();
}

int fuzzy_match(const char *pattern, const char *str)
{
	if (!*pattern) return 1;

	while (*str) {
		if (tolower(*pattern) == tolower(*str)) {
			const char *p = pattern + 1;
			const char *s = str + 1;
			while (*p && *s) {
				if (tolower(*p) == tolower(*s)) p++;
				s++;
			}
			if (!*p) return 1;
		}
		str++;
	}
	return 0;
}

void highlight_matches(const char *str, const char *pattern, int selected)
{
	if (!*pattern) {
		printf("%s" RESET, str);
		return;
	}

	const char *s = str;
	const char *p = pattern;

	while (*s) {
		if (p && tolower(*p) == tolower(*s)) {
			printf(YELLOW "%c" RESET, *s);
			p++;
		} else {
			printf("%s%c", selected ? BLUE: RESET, *s);
		}
		s++;
	}
}

int count_matches(void)
{
	int matches = 0;
	for (int i = 0; i < count; i++) {
		if (fuzzy_match(query, lines[i])) matches++;
	}
	return matches;
}

void draw(void)
{
	printf("\033[H\033[J" GREEN "> " RESET "%s\n", query);

	/* 1 for prompt, 1 off by one */
	int visible_items = rows - 2;

    int start = 0;
    int end = count;

    if (selected >= visible_items) {
        start = selected - visible_items + 1;
    }
    
    if (end > start + visible_items) {
        end = start + visible_items;
    }

	for (int i = start; i < end; i++) {
		if (fuzzy_match(query, lines[i])) {
			if (i == selected) {
				printf(RED "> " RESET BLUE);
			}
			if (i != selected) {
				printf("  ");
			}
			highlight_matches(lines[i], query, i == selected);
			printf("\n");
		}
	}
	/* Move cursor to query */
	printf("\033[%d;%dH", 1, query_len + 3);
	fflush(stdout);
}

int main(void)
{
	/* stdin already occupied by pipe, so we need /dev/tty */
	tty_fd = open("/dev/tty", O_RDWR);
	if (tty_fd == -1) {
		perror("open");
		return 1;
	}

	if (isatty(STDIN_FILENO)) {
		fprintf(stderr, "neo: No input from pipe\n");
		close(tty_fd);
		return 1;
	}

	lines = malloc(sizeof(char *) * MAX_LINES);
	if (!lines) {
		perror("malloc");
		return 1;
	}

	/* Read from stdin (pipe) */
	char buf[MAX_LINE];
	while (count < MAX_LINES && fgets(buf, sizeof(buf), stdin)) {
		size_t len = strlen(buf);
		if (len && buf[len - 1] == '\n') buf[len - 1] = '\0';
		lines[count++] = strdup(buf);
	}
	printf("count: %d\n", count);

	if (!count) {
		fprintf(stderr, "neo: No input received\n");
		free(lines);
		close(tty_fd);
		return 1;
	}

	signal(SIGWINCH, handle_sigwinch);
	get_window_size();

	/* Raw mode */
	tcgetattr(tty_fd, &orig_termios);
	struct termios raw = orig_termios;
	raw.c_iflag &= ~(ICRNL | IXON);
	raw.c_lflag &= ~(ECHO | ICANON);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(tty_fd, TCSAFLUSH, &raw);

	draw();
	printf("\033[?25h");

	while (1) {
		char c;
		if (read(tty_fd, &c, 1) == 1) {
			if (c == 'q') break;
			else if (c == 27) {
				char seq[3];
				if (read(tty_fd, &seq[0], 1) != 1) continue;
				if (read(tty_fd, &seq[1], 1) != 1) continue;

				if (seq[0] == '[') {
					switch (seq[1]) {
						/* Up */
						case 'A':
							if (selected > 0) selected--;
							break;
						/* Down */
						case 'B':
							if (selected < count_matches() - 1)
								selected++;
							break;
					}
				}
			} else if (c == 127 || c == '\b') {
				if (query_len > 0) {
					query_len--;
					query[query_len] = '\0';
					selected = 0;
				}
			} else if (c == '\r' || c == '\n') {
				int matches = 0;
				for (int i = 0; i < count; i++) {
					if (fuzzy_match(query, lines[i])) {
						if (matches == selected) {
							printf("\033[H\033[J%s\n", lines[i]);
							goto cleanup;
						}
						matches++;
					}
				}
			} else if (query_len < sizeof(query) - 1 && isprint(c)) {
				query[query_len++] = c;
				query[query_len] = '\0';
				selected = 0;
			}

			draw();
		}
	}

cleanup:
	tcsetattr(tty_fd, TCSAFLUSH, &orig_termios);
	close(tty_fd);
	for (int i = 0; i < count; i++)
		free(lines[i]);
	free(lines);
	return 0;
}
