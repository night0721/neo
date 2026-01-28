#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

#define MAX_LINES 100000
#define MAX_LEN 4096

#define RESET  "\033[0m"
#define RED    "\033[38;2;243;139;168m"
#define GREEN  "\033[38;2;116;199;236m"
#define WHITE  "\033[37m"
#define ORANGE "\033[38;2;250;179;135m"

typedef struct {
	int idx, score, pos[128];
} match_t;

struct {
	char **lines;
	match_t *matches;
	char query[MAX_LEN];
	int nlines, nmatches, qlen, sel, rows, cols, tty_fd;
	struct termios orig;
} e;

void cleanup(int sig)
{
	(void)sig;
	write(e.tty_fd, "\033[?25h\033[?7h\033[2J\033[H", 15);
	tcsetattr(e.tty_fd, TCSAFLUSH, &e.orig);
	if (sig == 0) {
		return;
	}
	exit(sig ? 1 : 0);
}

/*
 * Fuzzy match scoring function
 */
int fzy_score(const char *n, const char *h, int *pos)
{
	int ni = 0, hi = 0, score = 0, last_hi = -1;
	int nlen = strlen(n), hlen = strlen(h);
	while (ni < nlen && hi < hlen) {
		if (tolower(h[hi]) == tolower(n[ni])) {
			int s = 10;
			if (hi == 0 || strchr("/._- ", h[hi-1])) s += 30;
			if (last_hi != -1 && hi == last_hi + 1) s += 50;
			else if (last_hi != -1) s -= (hi - last_hi) * 2;
			score += s;
			pos[ni] = hi;
			last_hi = hi;
			ni++;
		}
		hi++;
	}
	if (ni == nlen && nlen > 0) return score - pos[0];
	return -1000000;
}

int cmp_match(const void *a, const void *b)
{
	return ((match_t *)b)->score - ((match_t *)a)->score;
}

void update()
{
	e.nmatches = 0;
	for (int i = 0; i < e.nlines; i++) {
		int p[128], s = (e.qlen == 0) ? 0 : fzy_score(e.query, e.lines[i], p);
		if (s > -1000000) {
			e.matches[e.nmatches].idx = i;
			e.matches[e.nmatches].score = s;
			if (e.qlen > 0) memcpy(e.matches[e.nmatches].pos, p, sizeof(int) * (e.qlen < 128 ? e.qlen : 128));
			e.nmatches++;
		}
	}
	if (e.qlen > 0) qsort(e.matches, e.nmatches, sizeof(match_t), cmp_match);
}

void draw()
{
	char buf[65536];
	int n = 0, vis_rows = e.rows - 2; /* 1 for query, 1 for padding/status */
	int start = 0;

	/* Simple scrolling: keep selection in view */
	if (e.sel >= vis_rows) start = e.sel - vis_rows + 1;

	/* Clear and draw prompt */
	n += sprintf(buf + n, "\033[H\033[J\033[?7l\033[?25l" GREEN ">" RESET " %s\r\n", e.query);

	for (int i = start; i < start + vis_rows && i < e.nmatches; i++) {
		match_t *m = &e.matches[i];
		char *l = e.lines[m->idx];
		n += sprintf(buf + n, "%s", (i == e.sel) ? RED ">" RESET " " : "  ");

		int pi = 0;
		for (int j = 0; l[j] && j < e.cols - 5; j++) {
			if (e.qlen > 0 && pi < e.qlen && m->pos[pi] == j) {
				n += sprintf(buf + n, ORANGE "%c" RESET, l[j]);
				pi++;
			} else buf[n++] = l[j];
		}
		n += sprintf(buf + n, "\033[K\r\n");
	}
	/* Fix cursor pos: always on the first line */
	n += sprintf(buf + n, "\033[1;%dH\033[?25h", e.qlen + 3);
	write(e.tty_fd, buf, n);
}

int main()
{
	char line[MAX_LEN];
	e.lines = malloc(sizeof(char *) * MAX_LINES);
	while (e.nlines < MAX_LINES && fgets(line, sizeof(line), stdin)) {
		line[strcspn(line, "\n")] = 0;
		e.lines[e.nlines++] = strdup(line);
	}

	e.tty_fd = open("/dev/tty", O_RDWR);
	tcgetattr(e.tty_fd, &e.orig);
	struct termios raw = e.orig;
	raw.c_lflag &= ~(ECHO | ICANON);
	tcsetattr(e.tty_fd, TCSAFLUSH, &raw);

	struct winsize ws;
	ioctl(e.tty_fd, TIOCGWINSZ, &ws);
	e.rows = ws.ws_row; e.cols = ws.ws_col;
	e.matches = malloc(sizeof(match_t) * e.nlines);

	signal(SIGINT, cleanup);
	update(); draw();

	char c;
	while (read(e.tty_fd, &c, 1) == 1) {
		if (c == 27) {
			char seq[2];
			if (read(e.tty_fd, seq, 2) < 2) continue;
			if (seq[1] == 'A') { if (e.sel > 0) e.sel--; }
			else if (seq[1] == 'B') { if (e.sel < e.nmatches - 1) e.sel++; }
		} else if (c == 127 || c == 8) {
			if (e.qlen > 0) e.query[--e.qlen] = 0;
			e.sel = 0; update();
		} else if (c == '\r' || c == '\n') {
			char *res = (e.nmatches > 0) ? strdup(e.lines[e.matches[e.sel].idx]) : NULL;
			cleanup(0);
			if (res) printf("%s\n", res);
			return 0;
		} else if (isprint(c)) {
			e.query[e.qlen++] = c;
			e.sel = 0; update();
		}
		draw();
	}
	cleanup(0);
	return 0;
}
