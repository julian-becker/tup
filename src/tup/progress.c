#include "progress.h"
#include "colors.h"
#include "db.h"
#include "entry.h"
#include <stdio.h>
#include <unistd.h>

static int cur_phase = -1;
static int sum;
static int total;
static int is_active = 0;
static int stdout_isatty;

void progress_init(void)
{
	stdout_isatty = isatty(STDOUT_FILENO);
}

void tup_show_message(const char *s)
{
	const char *tup = " tup ";
	color_set(stdout);
	/* If we get to the end, show a green bar instead of grey. */
	if(cur_phase == 5)
		printf("[%s%s%s] %s", color_final(), tup, color_end(), s);
	else
		printf("[%s%.*s%s%.*s] %s", color_reverse(), cur_phase, tup, color_end(), 5-cur_phase, tup+cur_phase, s);
}

void tup_main_progress(const char *s)
{
	cur_phase++;
	tup_show_message(s);
}

void start_progress(int new_total)
{
	sum = 0;
	total = new_total;
}

static void show_bar(FILE *f, int node_type, int show_percent)
{
	if(total) {
		const int max = 11;
		int fill;
		char buf[12];

		if(is_active) {
			printf("\r                             \r");
			is_active = 0;
			if(f == stderr)
				fflush(stdout);
		}

		/* If it's a good enough limit for Final Fantasy VII, it's good
		 * enough for me.
		 */
		if(total > 9999 || show_percent) {
			snprintf(buf, sizeof(buf), "   %3i%%     ", sum*100/total);
		} else {
			snprintf(buf, sizeof(buf), " %4i/%-4i ", sum, total);
		}
		/* TUP_NODE_ROOT means an error - fill the whole bar so it's
		 * obvious.
		 */
		if(node_type == TUP_NODE_ROOT)
			fill = max;
		else
			fill = max * sum / total;

		color_set(f);
		fprintf(f, "[%s%s%.*s%s%.*s] ", color_type(node_type), color_append_reverse(), fill, buf, color_end(), max-fill, buf+fill);
	}
}

void show_progress(struct tup_entry *tent, int is_error)
{
	FILE *f;

	sum++;
	if(is_error) {
		f = stderr;
		show_bar(f, TUP_NODE_ROOT, 0);
	} else {
		f = stdout;
		show_bar(f, tent->type, 0);
	}
	print_tup_entry(f, tent);
	fprintf(f, "\n");
}

void show_active(int active)
{
	if(total && stdout_isatty) {
		/* First time through we should 0/N for the progress bar, then
		 * after that we just show the percentage complete, since the
		 * previous line will have a 1/N line for the last completed
		 * job.
		 */
		show_bar(stdout, TUP_NODE_CMD, sum != 0);
		printf("Active: %i", active);
		fflush(stdout);
		is_active = 1;
	}
}