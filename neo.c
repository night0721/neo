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

#define RESET  "\033[0m"
#define RED    "\033[38;2;243;139;168m"
#define GREEN  "\033[38;2;116;199;236m"
#define WHITE  "\033[37m"
#define ORANGE "\033[38;2;250;179;135m"

typedef struct {
	int index;
	int score;
	int pos[64];
	int pos_count;
} match;

struct engine {
	char **lines;
	int line_count;

	match *matches;
	int match_count;

	char query[MAX_LINE];
	int query_len;

	int selected;
};

static struct termios orig_termios;
static int rows = 24;
static int cols = 80;
static int tty_fd;


void cleanup(int sig)
{
	(void)sig;
	printf("\033[?1049l\033[?25h");
	tcsetattr(tty_fd, TCSAFLUSH, &orig_termios);
	exit(1);
}

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
	(void)sig;
	get_window_size();
}

/* Scoring function */
#define SCORE_MATCH     10
#define SCORE_CONSEC    15
#define SCORE_BOUNDARY  8
#define SCORE_GAP       -1

static int is_boundary(char prev, char curr)
{
	if (prev == '/' || prev == '_' || prev == '-' || prev == ' ')
		return 1;
	if (islower(prev) && isupper(curr))
		return 1;
	return 0;
}

int fuzzy_score(const char *text, const char *pattern, int *pos, int *pos_count)
{
	int score = 0;
	int last = -1;
	*pos_count = 0;

	for (int pi = 0; pattern[pi]; pi++) {
		char p = tolower(pattern[pi]);
		int found = 0;

		for (int ti = last + 1; text[ti]; ti++) {
			if (tolower(text[ti]) == p) {
				score += SCORE_MATCH;	

				if (ti == last + 1)
					score += SCORE_CONSEC;

				if (ti == 0 || is_boundary(text[ti - 1], text[ti]))
					score += SCORE_BOUNDARY;

				if (*pos_count < 64)
					pos[(*pos_count)++] = ti;

				last = ti;
				found = 1;
				break;
			}
			score += SCORE_GAP;
		}

		if (!found)
			return -1;
	}

	return score;
}

int match_cmp(const void *a, const void *b)
{
	const match *ma = a;
	const match *mb = b;

	if (ma->score != mb->score)
		return mb->score - ma->score;

	return ma->index - mb->index;
}

void engine_update(struct engine *e)
{
	e->match_count = 0;

	for (int i = 0; i < e->line_count; i++) {
		int s = fuzzy_score(e->lines[i], e->query,
				e->matches[e->match_count].pos,
				&e->matches[e->match_count].pos_count);
		if (s >= 0) {
			e->matches[e->match_count].index = i;
			e->matches[e->match_count].score = s;
			e->match_count++;
		}
	}

	qsort(e->matches, e->match_count, sizeof(match), match_cmp);

	if (e->selected >= e->match_count)
		e->selected = e->match_count - 1;
	if (e->selected < 0)
		e->selected = 0;
}

void draw_highlight(const char *text, match *m, int max_cols, int selected)
{
	int pi = 0;
	int col = 0;

	for (int i = 0; text[i] && col < max_cols; i++) {
		if (pi < m->pos_count && m->pos[pi] == i) {
			if (selected)
				printf("\033[1m" ORANGE "%c" RESET "\033[22m", text[i]);
			else
				printf(ORANGE "%c" RESET, text[i]);
			pi++;
		} else {
			putchar(text[i]);
		}
		col++;
	}
}

void draw(struct engine *e)
{
	printf("\033[H\033[J");
	printf(GREEN "> " RESET "%s\n", e->query);

	int text_cols = cols - 2;
	if (text_cols < 0)
		text_cols = 0;

	int visible = rows - 2;
	int start = 0;

	if (e->selected >= visible)
		start = e->selected - visible + 1;

	for (int i = start; i < e->match_count && i < start + visible; i++) {
		int idx = e->matches[i].index;

		/* clear line */
		printf("\033[K");

		if (i == e->selected)
			printf(RED "> " RESET WHITE);
		else
			printf("  ");

		draw_highlight(e->lines[idx], &e->matches[i], text_cols, i == e->selected);
		putchar('\n');
	}

	printf("\033[1;%dH", e->query_len + 3);
	fflush(stdout);
}

int main(void)
{
	printf("\033[?1049h\033[?25l");
	signal(SIGINT, cleanup);
	signal(SIGTERM, cleanup);
	signal(SIGQUIT, cleanup);
	tty_fd = open("/dev/tty", O_RDWR);
	if (tty_fd == -1)
		return 1;

	if (isatty(STDIN_FILENO))
		return 1;

	char **lines = malloc(sizeof(char *) * MAX_LINES);
	int count = 0;

	char buf[MAX_LINE];
	while (count < MAX_LINES && fgets(buf, sizeof(buf), stdin)) {
		size_t len = strlen(buf);
		if (len && buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		lines[count++] = strdup(buf);
	}

	struct engine e = {0};
	e.lines = lines;
	e.line_count = count;
	e.matches = malloc(sizeof(match) * count);

	signal(SIGWINCH, handle_sigwinch);
	get_window_size();

	tcgetattr(tty_fd, &orig_termios);
	struct termios raw = orig_termios;
	raw.c_iflag &= ~(ICRNL | IXON);
	raw.c_lflag &= ~(ECHO | ICANON);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(tty_fd, TCSAFLUSH, &raw);

	engine_update(&e);
	draw(&e);

	while (1) {
		char c;
		if (read(tty_fd, &c, 1) != 1)
			continue;

		if (c == 27) {
			char seq[2];
			if (read(tty_fd, &seq[0], 1) != 1) continue;
			if (read(tty_fd, &seq[1], 1) != 1) continue;

			if (seq[0] == '[') {
				if (seq[1] == 'A' && e.selected > 0)
					e.selected--;
				if (seq[1] == 'B' && e.selected + 1 < e.match_count)
					e.selected++;
			}
		} else if (c == 127 || c == '\b') {
			if (e.query_len > 0) {
				e.query[--e.query_len] = '\0';
				e.selected = 0;
				engine_update(&e);
			}
		} else if (c == '\n' || c == '\r') {
			if (e.match_count > 0) {
				int idx = e.matches[e.selected].index;
				printf("\033[?1049l\033[?25h");
				printf("\033[H\033[J%s\n", e.lines[idx]);
				break;
			}
		} else if (isprint(c) && e.query_len < MAX_LINE - 1) {
			e.query[e.query_len++] = c;
			e.query[e.query_len] = '\0';
			e.selected = 0;
			engine_update(&e);
		}

		draw(&e);
	}

	tcsetattr(tty_fd, TCSAFLUSH, &orig_termios);
	close(tty_fd);

	for (int i = 0; i < count; i++)
		free(lines[i]);
	free(lines);
	free(e.matches);
	return 0;
}
