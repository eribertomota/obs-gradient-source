#include <obs-module.h>
#include "gradient-source.h"

#include <stdio.h>

#include "version.h"

#define MAX_GRADIENT_STEPS 9
#define PROPERTY_NAME_LEN 24

struct gradient_info {
	obs_source_t *source;
	uint32_t cx;
	uint32_t cy;
	gs_texrender_t *render;
	bool rendered;
};

static const char *gradient_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Gradient");
}

static void gradient_update(void *data, obs_data_t *settings)
{
	struct gradient_info *context = data;
	context->cx = (uint32_t)obs_data_get_int(settings, "width");
	context->cy = (uint32_t)obs_data_get_int(settings, "height");
	struct vec4 from_color;
	vec4_from_rgba(&from_color,
		       (uint32_t)obs_data_get_int(settings, "from_color"));
	from_color.w =
		(float)(obs_data_get_double(settings, "from_opacity") / 100.0);

	struct vec4 from_color_srgb;
	vec4_copy(&from_color_srgb, &from_color);
	gs_float3_srgb_nonlinear_to_linear(from_color_srgb.ptr);

	bool srgb = obs_data_get_bool(settings, "srgb");
	double rotation =
		fmod(obs_data_get_double(settings, "rotation"), 360.0);

	obs_enter_graphics();
	if (!context->render) {
		context->render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	} else {
		gs_texrender_reset(context->render);
	}
	if (!gs_texrender_begin(context->render, context->cx, context->cy)) {
		obs_leave_graphics();
		return;
	}

	gs_blend_state_push();
	gs_matrix_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	gs_ortho(0.0f, (float)context->cx, 0.0f, (float)context->cy, -100.0f,
		 100.0f);
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);
	const double line_length_moving_x = context->cy * cos(RAD(rotation));
	const double line_length_moving_y = context->cx * sin(RAD(rotation));
	double start_x = 0.0;
	double start_y = 0.0;
	double scan_x = 0.0;
	double scan_y = 0.0;
	double cd = 0.0;

	if (obs_data_has_user_value(settings, "midpoint")) {
		obs_data_set_double(settings, "midpoint_1",
				    obs_data_get_double(settings, "midpoint"));
		obs_data_unset_user_value(settings, "midpoint");
	}

	if (obs_data_has_user_value(settings, "to_color")) {
		obs_data_set_double(settings, "to_color_1",
				    obs_data_get_double(settings, "to_color"));
		obs_data_unset_user_value(settings, "to_color");
	}

	if (obs_data_has_user_value(settings, "to_opacity")) {
		obs_data_set_double(settings, "to_opacity_1",
				    obs_data_get_double(settings,
							"to_opacity"));
		obs_data_unset_user_value(settings, "to_opacity");
	}

	int steps = (int)obs_data_get_int(settings, "steps");
	if (steps < 1)
		steps = 1;

	double midpoint =
		steps == 1 ? obs_data_get_double(settings, "midpoint_1") / 100.0
			   : 0.5;

	char property_name[PROPERTY_NAME_LEN];
	snprintf(property_name, PROPERTY_NAME_LEN, "to_color_%i", steps);
	struct vec4 to_color;
	vec4_from_rgba(&to_color,
		       (uint32_t)obs_data_get_int(settings, property_name));
	snprintf(property_name, PROPERTY_NAME_LEN, "to_opacity_%i", steps);
	to_color.w =
		(float)(obs_data_get_double(settings, property_name) / 100.0);

	struct vec4 to_color_srgb;
	vec4_copy(&to_color_srgb, &to_color);
	gs_float3_srgb_nonlinear_to_linear(to_color_srgb.ptr);

	gs_matrix_push();

	if (fabs(line_length_moving_x) > fabs(line_length_moving_y)) {
		// move y direction
		if (line_length_moving_x > 0.0) {
			// move down
			const double t = tan(RAD(rotation));
			scan_y = context->cy + (double)context->cx * fabs(t);
			if (line_length_moving_y > 0.0) {
				start_y = (double)context->cx * fabs(t);
			}
			gs_effect_set_vec4(color, &from_color);
			gs_draw_sprite(0, 0, context->cx,
				       (uint32_t)(context->cy * midpoint));
			gs_matrix_translate3f(
				0.0f, (float)(context->cy * midpoint), 0.0f);
			gs_effect_set_vec4(color, &to_color);
			gs_draw_sprite(
				0, 0, context->cx,
				(uint32_t)(context->cy * (1.0 - midpoint)));
		} else {
			// move up
			const double t = tan(RAD(rotation + 180));
			scan_y = -1.0 * (context->cy + context->cx * fabs(t));
			if (line_length_moving_y < 0.0) {
				start_y = scan_y;
			} else {
				start_y = (double)context->cy * -1.0;
			}
			start_x = (double)context->cx * -1.0;
			gs_effect_set_vec4(color, &to_color);
			gs_draw_sprite(
				0, 0, context->cx,
				(uint32_t)(context->cy * (1.0 - midpoint)));
			gs_matrix_translate3f(
				0.0f, (float)(context->cy * (1.0 - midpoint)),
				0.0f);
			gs_effect_set_vec4(color, &from_color);
			gs_draw_sprite(0, 0, context->cx,
				       (uint32_t)(context->cy / midpoint));
		}
		cd = ceil(sqrt((context->cx * context->cx) +
			       (fabs(scan_y) * fabs(scan_y))));
	} else {
		// move x direction
		if (line_length_moving_y < 0.0) {
			// move right
			const double t = tan(RAD(rotation + 270.0));
			scan_x = context->cx + context->cy * fabs(t);
			if (line_length_moving_x > 0.0) {
				start_x = context->cy * fabs(t);
			}
			start_y = (double)context->cy * -1.0;
			gs_effect_set_vec4(color, &from_color);
			gs_draw_sprite(0, 0, (uint32_t)(context->cx * midpoint),
				       context->cy);
			gs_matrix_translate3f((float)(context->cx * midpoint),
					      0.0f, 0.0f);
			gs_effect_set_vec4(color, &to_color);
			gs_draw_sprite(
				0, 0,
				(uint32_t)(context->cx * (1.0 - midpoint)),
				context->cy);
		} else {
			// move left
			const double t = tan(RAD(rotation + 90.0));
			scan_x = -1.0 * (context->cx + context->cy * fabs(t));
			if (line_length_moving_x < 0.0) {
				start_x = scan_x;
			} else {
				start_x = (double)context->cx * -1.0;
			}
			gs_effect_set_vec4(color, &to_color);
			gs_draw_sprite(
				0, 0,
				(uint32_t)(context->cx * (1.0 - midpoint)),
				context->cy);
			gs_matrix_translate3f(
				(float)(context->cx * (1.0 - midpoint)), 0.0f,
				0.0f);
			gs_effect_set_vec4(color, &from_color);
			gs_draw_sprite(0, 0, (uint32_t)(context->cx * midpoint),
				       context->cy);
		}
		cd = ceil(sqrt((context->cy * context->cy) +
			       (fabs(scan_x) * fabs(scan_x))));
	}
	gs_matrix_pop();

	float len;
	if (fabs(scan_x) > fabs(scan_y)) {
		len = (float)(fabs(scan_x) / steps);
	} else {
		len = (float)(fabs(scan_y) / steps);
	}

	for (int step = 1; step <= steps; step++) {
		snprintf(property_name, PROPERTY_NAME_LEN, "to_color_%i", step);
		vec4_from_rgba(&to_color, (uint32_t)obs_data_get_int(
						  settings, property_name));
		snprintf(property_name, PROPERTY_NAME_LEN, "to_opacity_%i",
			 step);
		to_color.w =
			(float)(obs_data_get_double(settings, property_name) /
				100.0);

		struct vec4 to_color_srgb;
		vec4_copy(&to_color_srgb, &to_color);
		gs_float3_srgb_nonlinear_to_linear(to_color_srgb.ptr);

		snprintf(property_name, PROPERTY_NAME_LEN, "midpoint_%i", step);
		float midpoint =
			(float)obs_data_get_double(settings, property_name) /
			100.0f;

		for (float i = 0.0f; i <= len; i += 1.0f) {
			gs_matrix_push();
			struct vec4 cur_color;
			float factor = i / len;
			gs_matrix_translate3f(
				(float)(((step - 1.0f + factor) / steps) *
						scan_x -
					start_x),
				(float)(((step - 1.0f + factor) / steps) *
						scan_y -
					start_y),
				0.0f);
			if (factor > midpoint) {
				factor = 0.5f + (factor - midpoint) /
							(1.0f - midpoint) / 2;
			} else {
				factor = 0.5f -
					 (midpoint - factor) / midpoint / 2;
			}

			gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f,
					  (float)RAD(rotation));
			gs_matrix_translate3f((float)(-1.0 * cd), 0.0f, 0.0f);

			if (srgb) {
				cur_color.x =
					from_color_srgb.x * (1.0f - factor) +
					to_color_srgb.x * factor;
				cur_color.y =
					from_color_srgb.y * (1.0f - factor) +
					to_color_srgb.y * factor;
				cur_color.z =
					from_color_srgb.z * (1.0f - factor) +
					to_color_srgb.z * factor;
				cur_color.w =
					from_color_srgb.w * (1.0f - factor) +
					to_color_srgb.w * factor;

				gs_float3_srgb_linear_to_nonlinear(
					cur_color.ptr);
			} else {
				cur_color.x = from_color.x * (1.0f - factor) +
					      to_color.x * factor;
				cur_color.y = from_color.y * (1.0f - factor) +
					      to_color.y * factor;
				cur_color.z = from_color.z * (1.0f - factor) +
					      to_color.z * factor;
				cur_color.w = from_color.w * (1.0f - factor) +
					      to_color.w * factor;
			}

			gs_effect_set_vec4(color, &cur_color);
			gs_draw_sprite(0, 0, (uint32_t)(cd * 2), 2);
			gs_matrix_pop();
		}
		from_color = to_color;
		from_color_srgb = to_color_srgb;
	}
	gs_technique_end_pass(tech);
	gs_technique_end(tech);
	gs_texrender_end(context->render);
	gs_matrix_pop();
	gs_blend_state_pop();

	obs_leave_graphics();
}

static void *gradient_create(obs_data_t *settings, obs_source_t *source)
{
	struct gradient_info *context = bzalloc(sizeof(struct gradient_info));
	context->source = source;
	gradient_update(context, settings);
	return context;
}

static void gradient_destroy(void *data)
{
	struct gradient_info *context = data;
	bfree(context);
}

static void gradient_video_render(void *data, gs_effect_t *effect)
{
	struct gradient_info *context = data;

	if (!context->render)
		return;
	gs_texture_t *tex = gs_texrender_get_texture(context->render);
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"),
			      tex);
	gs_draw_sprite(tex, 0, context->cx, context->cy);
}

bool gradient_steps_modified(void *priv, obs_properties_t *props,
			     obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(priv);
	UNUSED_PARAMETER(property);
	bool changed = false;
	int steps = (int)obs_data_get_int(settings, "steps");
	char property_name[PROPERTY_NAME_LEN];
	for (int i = 1; i <= steps; i++) {
		snprintf(property_name, PROPERTY_NAME_LEN, "midpoint_%i", i);
		obs_property_t *p = obs_properties_get(props, property_name);
		if (p)
			continue;

		changed = true;
		obs_properties_remove_by_name(props, "plugin_info");

		p = obs_properties_add_float_slider(props, property_name,
						    obs_module_text("Midpoint"),
						    0.0, 100.0, 1.0);
		obs_property_float_set_suffix(p, "%");

		snprintf(property_name, PROPERTY_NAME_LEN, "to_color_%i", i);
		p = obs_properties_add_color(props, property_name,
					     obs_module_text("ToColor"));

		snprintf(property_name, PROPERTY_NAME_LEN, "to_opacity_%i", i);
		p = obs_properties_add_float_slider(props, property_name,
						    obs_module_text("Opacity"),
						    0.0, 100.0, 1.0);
		obs_property_float_set_suffix(p, "%");
	}

	for (int i = steps + 1; i <= MAX_GRADIENT_STEPS; i++) {
		snprintf(property_name, PROPERTY_NAME_LEN, "midpoint_%i", i);
		obs_properties_remove_by_name(props, property_name);
		snprintf(property_name, PROPERTY_NAME_LEN, "to_color_%i", i);
		obs_properties_remove_by_name(props, property_name);
		snprintf(property_name, PROPERTY_NAME_LEN, "to_opacity_%i", i);
		obs_properties_remove_by_name(props, property_name);
	}
	if (changed) {
		obs_properties_add_text(
			props, "plugin_info",
			"<a href=\"https://obsproject.com/forum/resources/gradient-source.1172/\">Gradient Source</a> (" PROJECT_VERSION
			") by <a href=\"https://www.exeldro.com\">Exeldro</a>",
			OBS_TEXT_INFO);
	}
	return changed;
}

static obs_properties_t *gradient_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();
	obs_properties_add_int(ppts, "width", obs_module_text("Width"), 0, 4096,
			       1);

	obs_properties_add_int(ppts, "height", obs_module_text("Height"), 0,
			       4096, 1);

	obs_property_t *p = obs_properties_add_float_slider(
		ppts, "rotation", obs_module_text("Rotation"), 0.0, 360.0, 1.0);
	obs_property_float_set_suffix(p, obs_module_text("Degrees"));

	obs_properties_add_bool(ppts, "srgb", obs_module_text("sRGB"));

	obs_property_t *steps =
		obs_properties_add_int(ppts, "steps", obs_module_text("Steps"),
				       1, MAX_GRADIENT_STEPS, 1);

	p = obs_properties_add_color(ppts, "from_color",
				     obs_module_text("FromColor"));

	p = obs_properties_add_float_slider(ppts, "from_opacity",
					    obs_module_text("Opacity"), 0.0,
					    100.0, 1.0);
	obs_property_float_set_suffix(p, "%");

	obs_property_set_modified_callback2(steps, gradient_steps_modified,
					    data);
	return ppts;
}

void gradient_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
	obs_data_set_default_double(settings, "rotation", 270.0);

	obs_data_set_default_int(settings, "from_color", 0xFFD1D1D1);
	obs_data_set_default_double(settings, "from_opacity", 100.0);
	obs_data_set_default_int(settings, "steps", 1);
	char property_name[PROPERTY_NAME_LEN];
	for (int i = 1; i <= MAX_GRADIENT_STEPS; i++) {
		snprintf(property_name, PROPERTY_NAME_LEN, "midpoint_%i", i);
		obs_data_set_default_double(settings, property_name, 50.0);
		snprintf(property_name, PROPERTY_NAME_LEN, "to_color_%i", i);
		obs_data_set_default_int(settings, property_name, 0xFF000000);
		snprintf(property_name, PROPERTY_NAME_LEN, "to_opacity_%i", i);
		obs_data_set_default_double(settings, property_name, 100.0);
	}
}
static uint32_t gradient_width(void *data)
{
	struct gradient_info *context = data;
	return context->cx;
}

static uint32_t gradient_height(void *data)
{
	struct gradient_info *context = data;
	return context->cy;
}

struct obs_source_info gradient_source = {
	.id = "gradient_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_OUTPUT_VIDEO,
	.get_name = gradient_get_name,
	.create = gradient_create,
	.destroy = gradient_destroy,
	.load = gradient_update,
	.update = gradient_update,
	.get_width = gradient_width,
	.get_height = gradient_height,
	.video_render = gradient_video_render,
	.get_properties = gradient_properties,
	.get_defaults = gradient_defaults,
	.icon_type = OBS_ICON_TYPE_COLOR,
};

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("gradient-source", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("GradientSource");
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[Gradient Source] loaded version %s", PROJECT_VERSION);
	obs_register_source(&gradient_source);
	return true;
}
