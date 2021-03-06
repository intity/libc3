/*
	c3object.c

	Copyright 2008-2012 Michel Pollet <buserror@gmail.com>

 	This file is part of libc3.

	libc3 is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	libc3 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with libc3.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "c3object.h"
#include "c3context.h"
#include "c3driver_object.h"

void
_c3object_clear(
		c3object_p o,
		const c3driver_object_t * d)
{
	for (int oi = 0; oi < o->transform.count; oi++) {
		o->transform.e[oi]->object = NULL;
		c3transform_dispose(o->transform.e[oi]);
	}
	for (int oi = 0; oi < o->geometry.count; oi++) {
		o->geometry.e[oi]->object = NULL;	// don't try to detach
		c3geometry_dispose(o->geometry.e[oi]);
	}
	for (int oi = 0; oi < o->objects.count; oi++) {
		o->objects.e[oi]->parent = NULL;	// don't try to detach
		c3object_dispose(o->objects.e[oi]);
	}
	c3object_array_free(&o->objects);
	c3geometry_array_free(&o->geometry);
	c3transform_array_free(&o->transform);
}

void
_c3object_dispose(
		c3object_p o,
		const c3driver_object_t * d)
{
	if (o->parent) {
		for (int oi = 0; oi < o->parent->objects.count; oi++)
			if (o->parent->objects.e[oi] == o) {
				c3object_array_delete(&o->parent->objects, oi, 1);
				c3object_set_dirty(o->parent, true);
				break;
			}
		o->parent = NULL;
	}
	C3_DRIVER_INHERITED(o, d, dispose);
	free(o);
}

void
_c3object_get_geometry(
		c3object_p o,
		const c3driver_object_t * d,
		c3geometry_array_p out)
{
	// if this object is not visible in this view, exit
	// there will be no geometry, so no drawing
	uint16_t viewmask = (1 << o->context->current);
	if (o->hidden & viewmask) {
		return;
	}
	for (int oi = 0; oi < o->geometry.count; oi++) {
		c3geometry_p g = o->geometry.e[oi];
		if (!(g->hidden & viewmask))
			c3geometry_array_add(out, g);
	}
	for (int oi = 0; oi < o->objects.count; oi++)
		c3object_get_geometry(o->objects.e[oi], out);
	C3_DRIVER_INHERITED(o, d, get_geometry, out);
}

void
_c3object_project(
		c3object_p o,
		const c3driver_object_t * d,
		c3mat4p m)
{
	if (!o->dirty)
		return;
	c3mat4 p = *m;
	for (int pi = 0; pi < o->transform.count; pi++) {
		c3mat4 t = p;
		p = c3mat4_mul(&t, &o->transform.e[pi]->matrix);
	}
	o->world = p;

	for (int gi = 0; gi < o->geometry.count; gi++) {
		c3geometry_p g = o->geometry.e[gi];
		c3geometry_project(g, &p);
	}
	for (int oi = 0; oi < o->objects.count; oi++)
		c3object_project(o->objects.e[oi], &p);
	o->dirty = false;
	C3_DRIVER_INHERITED(o, d, project, m);
}

const c3driver_object_t c3object_driver = {
	.clear = _c3object_clear,
	.dispose = _c3object_dispose,
	.get_geometry = _c3object_get_geometry,
	.project = _c3object_project,
};


c3object_p
c3object_init(
		c3object_p o /* = NULL */,
		c3object_p parent)
{
	memset(o, 0, sizeof(*o));
	o->parent = parent;
	static const c3driver_object_t * list[] =
			{ &c3object_driver, NULL };
	o->driver = list;
	if (parent) {
		c3object_array_add(&parent->objects, o);
		o->context = parent->context;
	}
	return o;
}

c3object_p
c3object_new(
		c3object_p o /* = NULL */)
{
	c3object_p res = malloc(sizeof(*o));
	return c3object_init(res, o);
}

void
c3object_clear(
		c3object_p o)
{
	C3_DRIVER(o, clear);
}

void
c3object_dispose(
		c3object_p o)
{
	c3object_clear(o);
	C3_DRIVER(o, dispose);
}

void
c3object_set_dirty(
		c3object_p o,
		bool dirty)
{
	if (dirty) {
		// also mark all our geometry dirty
		for (int oi = 0; oi < o->geometry.count; oi++)
			if (o->geometry.e[oi])
				o->geometry.e[oi]->dirty = 1;
		while (o) {
			o->dirty = true;
			o = o->parent;
		}
	} else {
		for (int oi = 0; oi < o->objects.count; oi++)
			if (o->objects.e[oi]->dirty)
				c3object_set_dirty(o->objects.e[oi], false);
		o->dirty = false;
	}
}

void
c3object_add_object(
		c3object_p o,
		c3object_p sub)
{
	if (sub->parent == o)
		return;
	if (sub->parent) {
		for (int oi = 0; oi < sub->parent->objects.count; oi++) {
			if (sub->parent->objects.e[oi] == sub) {
				c3object_array_delete(&sub->parent->objects, oi, 1);
				c3object_set_dirty(sub->parent, true);
				break;
			}
		}
		sub->parent = NULL;
	}
	sub->parent = o;
	if (o) {
		c3object_array_add(&o->objects, sub);
		c3object_set_dirty(o, true);
	}
}

void
c3object_add_geometry(
		c3object_p o,
		c3geometry_p g)
{
	if (g->object == o)
		return;
	if (g->object) {
		for (int oi = 0; oi < g->object->geometry.count; oi++) {
			if (g->object->geometry.e[oi] == g) {
				c3geometry_array_delete(&g->object->geometry, oi, 1);
				c3object_set_dirty(g->object, true);
				break;
			}
		}
		g->object = NULL;
	}
	g->object = o;
	if (o) {
		c3geometry_array_add(&o->geometry, g);
		c3object_set_dirty(o, true);
	}
}

void
c3object_get_geometry(
		c3object_p o,
		c3geometry_array_p array )
{
	C3_DRIVER(o, get_geometry, array);
}

void
c3object_project(
		c3object_p o,
		const c3mat4p m)
{
	C3_DRIVER(o, project, m);
}
