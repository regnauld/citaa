#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>

#include <cairo.h>

#include "citaa.h"
#include "bsdqueue.h"

#define PI 3.141592653589793238462643383

struct schema {
	int xcell;
	int ycell;
	int border_left;
	int border_right;
	int border_top;
	int border_bottom;
	double fuzz_x;
	double fuzz_y;
	double dash_spec[2];
	double point_marker_radius;
} default_paint_schema = {
	.xcell = 10,
	.ycell = 14,
	.border_left = 20,
	.border_right = 20,
	.border_top = 28,
	.border_bottom = 28,
	.fuzz_x = 0.5,
	.fuzz_y = 0.5,
	.dash_spec = { 6.0, 4.0 },
	.point_marker_radius = 3.0,
};

struct paint_context {
	cairo_surface_t *surface;
	cairo_t *cr;
	struct schema *s;

	int i_width;
	int i_height;

	int o_width;
	int o_height;
	int o_x;
	int o_y;

	struct image *point_markers;
};

#define pcx(x) pc->o_x + (x) * pc->s->xcell + pc->s->fuzz_x
#define pcy(y) pc->o_y + (y) * pc->s->ycell + pc->s->fuzz_y

void
paint_text(struct paint_context *pc, struct text_head *head, int white_text)
{
	struct text *t;

	TAILQ_FOREACH(t, head, list) {
		if (white_text)
			cairo_set_source_rgb(pc->cr, 1, 1, 1);
		else
			cairo_set_source_rgb(pc->cr, 0, 0, 0);
		cairo_select_font_face(pc->cr, "DejaVu", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(pc->cr, 12);
		cairo_move_to(pc->cr, pcx(t->x), pcy(t->y));  
		cairo_show_text(pc->cr, t->t);
		cairo_new_path(pc->cr);
	}
}

void
paint_box(struct paint_context *pc, struct component *c)
{
	struct vertex *v0, *start, *v1;
	int i, dir, new_dir;
	int min_x = INT_MAX, min_y = INT_MAX;

	cairo_set_line_width(pc->cr, 1);
	cairo_set_line_cap(pc->cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_source_rgb(pc->cr, 0, 0, 0);

	if (c->dashed)
		cairo_set_dash(pc->cr, pc->s->dash_spec, 2, 0);
	else
		cairo_set_dash(pc->cr, NULL, 0, 0);

	start = TAILQ_FIRST(&c->vertices);
	cairo_move_to(pc->cr, pcx(start->x), pcy(start->y));

	v0 = start;
	for (dir = 0; dir < N_DIRECTIONS; dir++)
		if (v0->e[dir])	break;

	while (1) {
		if (v0->x < min_x) min_x = v0->x;
		if (v0->y < min_y) min_y = v0->y;

		if (v0->c == '*')
			pc->point_markers->d[v0->y][v0->x] = '*';

		v1 = v0->e[dir];

		cairo_line_to(pc->cr, pcx(v1->x), pcy(v1->y));
		if (v1 == start)
			break;

		for (i = 1; i >= -1; i--) {
			new_dir = (dir + i + 4) % N_DIRECTIONS;
			if (v1->e[new_dir]) {
				dir = new_dir;
				v0 = v1;
				break;
			}
		}
	}

	cairo_close_path(pc->cr);
	if (c->has_custom_background)
		cairo_set_source_rgb(pc->cr,
							 c->custom_background.r / 15.0,
							 c->custom_background.g / 15.0,
							 c->custom_background.b / 15.0);
	else
		cairo_set_source_rgb(pc->cr, 1, 1, 1);
	cairo_fill_preserve(pc->cr);

	cairo_set_source_rgb(pc->cr, 0, 0, 0);
	cairo_stroke(pc->cr);

	paint_text(pc, &c->text, c->has_custom_background && c->white_text);
}

void
paint_arrow(struct paint_context *pc, struct vertex *v)
{
	int x, y, cx, cy;

	if (!strchr("<>Vv^", v->c)) return;

	cairo_set_line_width(pc->cr, 1);
	cairo_set_line_cap(pc->cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_source_rgb(pc->cr, 0, 0, 0);
	cairo_set_dash(pc->cr, NULL, 0, 0);

	x = pcx(v->x);
	y = pcy(v->y);
	cx = pc->s->xcell / 2;
	cy = pc->s->ycell / 2;

	switch (v->c) {
	case '^':
		cairo_move_to(pc->cr, x, y - cx);
		cairo_line_to(pc->cr, x - cx, y + cy);
		cairo_line_to(pc->cr, x + cx, y + cy);
		break;
	case 'V':
	case 'v':
		cairo_move_to(pc->cr, x, y + cy);
		cairo_line_to(pc->cr, x - cx, y - cy);
		cairo_line_to(pc->cr, x + cx, y - cy);
		break;
	case '<':
		cairo_move_to(pc->cr, x - cx, y);
		cairo_line_to(pc->cr, x + cx, y - cy);
		cairo_line_to(pc->cr, x + cx, y + cy);
		break;
	case '>':
		cairo_move_to(pc->cr, x + cx, y);
		cairo_line_to(pc->cr, x - cx, y - cy);
		cairo_line_to(pc->cr, x - cx, y + cy);
		break;
	}

	cairo_close_path(pc->cr);
	cairo_fill(pc->cr);
}

void
paint_line(struct paint_context *pc, struct component *c)
{
	struct vertex *v0, *start = NULL, *v1, *v;
	int i, dir, new_dir;

	cairo_set_line_width(pc->cr, 1);
	cairo_set_line_cap(pc->cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_source_rgb(pc->cr, 0, 0, 0);

	if (c->dashed)
		cairo_set_dash(pc->cr, pc->s->dash_spec, 2, 0);
	else
		cairo_set_dash(pc->cr, NULL, 0, 0);

	TAILQ_FOREACH(v, &c->vertices, list) {
		int cnt = 0;

		for (dir = 0; dir < N_DIRECTIONS; dir++) {
			if (v->e[dir]) {
				cnt++;
				start = v;
			}
		}
		if (cnt == 1)
			break;
	}
	if (!start) return;

	cairo_move_to(pc->cr, pcx(start->x), pcy(start->y));

	v0 = start;
	for (dir = 0; dir < N_DIRECTIONS; dir++)
		if (v0->e[dir])	break;

	if (v0->c == '*')
		pc->point_markers->d[v0->y][v0->x] = '*';

	while (v0) {
		v1 = v0->e[dir];

		cairo_line_to(pc->cr, pcx(v1->x), pcy(v1->y));

		if (v1->c == '*')
			pc->point_markers->d[v1->y][v1->x] = '*';

		v0 = NULL;
		for (i = 1; i >= -1; i--) {
			new_dir = (dir + i + 4) % N_DIRECTIONS;
			if (v1->e[new_dir]) {
				dir = new_dir;
				v0 = v1;
				break;
			}
		}
	}

	cairo_stroke(pc->cr);

	paint_arrow(pc, start);
	paint_arrow(pc, v1);
}

void
paint_component(struct paint_context *pc, struct component *c)
{
	if (c->type == CT_BOX)
		paint_box(pc, c);
	else if (c->type == CT_LINE)
		paint_line(pc, c);
}

void
paint_point_markers(struct paint_context *pc)
{
	int y, x;

	cairo_set_line_width(pc->cr, 1);
	cairo_set_line_cap(pc->cr, CAIRO_LINE_CAP_ROUND);

	for (y = 0; y < pc->i_height; y++)
		for (x = 0; x < pc->i_width; x++)
			if (pc->point_markers->d[y][x] != ' ') {
				cairo_arc(pc->cr, pcx(x), pcy(y),
						  pc->s->point_marker_radius,
						  0.0, 2*PI);
				cairo_set_source_rgb(pc->cr, 1, 1, 1);
				cairo_fill_preserve(pc->cr);
				cairo_set_source_rgb(pc->cr, 0, 0, 0);
				cairo_stroke(pc->cr);
			}
}

void
paint(int i_height, int i_width)
{
	struct paint_context ctx;
	struct paint_context *pc = &ctx;
	struct component *c;

	pc->s = &default_paint_schema;
	pc->i_width = i_width;
	pc->i_height = i_height;
	pc->o_width = pc->s->border_left + pc->i_width * pc->s->xcell + pc->s->border_right;
	pc->o_height = pc->s->border_top + pc->i_height * pc->s->ycell + pc->s->border_bottom;
	pc->o_x = pc->s->border_left + pc->s->xcell / 2;
	pc->o_y = pc->s->border_top + pc->s->ycell / 2;

	pc->point_markers = create_image(i_height, i_width, ' ');

	pc->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pc->o_width, pc->o_height);
	pc->cr = cairo_create(pc->surface);

	cairo_set_source_rgb(pc->cr, 1, 1, 1);
	cairo_rectangle(pc->cr, 0, 0, pc->o_width, pc->o_height);
	cairo_fill(pc->cr);

	TAILQ_FOREACH(c, &components, list) {
		paint_component(pc, c);
	}
	paint_text(pc, &free_text, 0);
	paint_point_markers(pc);

	cairo_surface_write_to_png(pc->surface, "o.png");

	cairo_destroy(pc->cr);
	cairo_surface_destroy(pc->surface);
}

