#include "../stereokit.h"
#include "../shaders_builtin/shader_builtin.h"
#include "../systems/platform/platform_utils.h"
#include "../libraries/ferr_hash.h"
#include "../libraries/qoi.h"
#include "../sk_math.h"
#include "../sk_memory.h"
#include "../spherical_harmonics.h"
#include "texture.h"

#pragma warning(push)
#pragma warning(disable : 26451 6011 6262 6308 6387 28182 26819 )
#include "../libraries/stb_image.h"
#pragma warning(pop)

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <string.h>
#include <limits.h>

namespace sk {

///////////////////////////////////////////

tex_t tex_create(tex_type_ type, tex_format_ format) {
	tex_t result = (tex_t)assets_allocate(asset_type_texture);
	result->type   = type;
	result->format = format;
	result->address_mode = tex_address_wrap;
	result->sample_mode  = tex_sample_linear;
	result->anisotropy   = 4;
	return result;
}

///////////////////////////////////////////

tex_t tex_add_zbuffer(tex_t texture, tex_format_ format) {
	if (!(texture->type & tex_type_rendertarget)) {
		log_err("tex_add_zbuffer can't add a zbuffer to a non-rendertarget texture!");
		return nullptr;
	}

	char id[64];
	assets_unique_name(asset_type_texture, "zbuffer/", id, sizeof(id));
	texture->depth_buffer = tex_create(tex_type_depth, format);
	tex_set_id       (texture->depth_buffer, id);
	tex_set_color_arr(texture->depth_buffer, texture->tex.width, texture->tex.height, nullptr, texture->tex.array_count, nullptr, texture->tex.multisample);
	skg_tex_attach_depth(&texture->tex, &texture->depth_buffer->tex);
	
	return texture->depth_buffer;
}

///////////////////////////////////////////

void tex_set_zbuffer(tex_t texture, tex_t depth_texture) {
	if (!(texture->type & tex_type_rendertarget)) {
		log_err("tex_set_zbuffer can't add a zbuffer to a non-rendertarget texture!");
		return;
	}
	if (!(depth_texture->type & tex_type_depth)) {
		log_err("tex_set_zbuffer can't add a non-depth texture as a zbuffer!");
		return;
	}
	tex_addref(depth_texture);
	if (texture->depth_buffer != nullptr)
		tex_release(texture->depth_buffer);
	texture->depth_buffer = depth_texture;

	skg_tex_attach_depth(&texture->tex, &depth_texture->tex);
}

///////////////////////////////////////////

void tex_set_surface(tex_t texture, void *native_surface, tex_type_ type, int64_t native_fmt, int32_t width, int32_t height, int32_t surface_count) {
	if (skg_tex_is_valid(&texture->tex))
		skg_tex_destroy (&texture->tex);

	skg_tex_type_ skg_type = skg_tex_type_image;
	if      (type & tex_type_cubemap     ) skg_type = skg_tex_type_cubemap;
	else if (type & tex_type_depth       ) skg_type = skg_tex_type_depth;
	else if (type & tex_type_rendertarget) skg_type = skg_tex_type_rendertarget;

	texture->type   = type;
	texture->format = tex_get_tex_format(native_fmt);
	texture->tex    = skg_tex_create_from_existing(native_surface, skg_type, skg_tex_fmt_from_native(native_fmt), width, height, surface_count);
}

///////////////////////////////////////////

void tex_set_surface_layer(tex_t texture, void *native_surface, tex_type_ type, int64_t native_fmt, int32_t width, int32_t height, int32_t surface_index) {
	if (skg_tex_is_valid(&texture->tex))
		skg_tex_destroy (&texture->tex);

	skg_tex_type_ skg_type = skg_tex_type_image;
	if      (type & tex_type_cubemap     ) skg_type = skg_tex_type_cubemap;
	else if (type & tex_type_depth       ) skg_type = skg_tex_type_depth;
	else if (type & tex_type_rendertarget) skg_type = skg_tex_type_rendertarget;

	texture->type   = type;
	texture->format = tex_get_tex_format(native_fmt);
	texture->tex    = skg_tex_create_from_layer(native_surface, skg_type, skg_tex_fmt_from_native(native_fmt), width, height, surface_index);
}

///////////////////////////////////////////

tex_t tex_find(const char *id) {
	tex_t result = (tex_t)assets_find(id, asset_type_texture);
	if (result != nullptr) {
		tex_addref(result);
		return result;
	}
	return nullptr;
}

///////////////////////////////////////////

void tex_set_id(tex_t tex, const char *id) {
	assets_set_id(tex->header, id);
}

///////////////////////////////////////////

void *tex_load_image_data(void *data, size_t data_size, bool32_t srgb_data, tex_format_ *out_format, int32_t *out_width, int32_t *out_height) {
	int32_t channels = 0;
	*out_format = srgb_data ? tex_format_rgba32 : tex_format_rgba32_linear;

	// Check for an stbi HDR image
	if (stbi_is_hdr_from_memory((stbi_uc*)data, (int)data_size)) {
		*out_format = tex_format_rgba128;
		return (uint8_t *)stbi_loadf_from_memory((stbi_uc *)data, (int)data_size, out_width, out_height, &channels, 4);
	}

	// Check through stbi's list of image formats
	void *result = stbi_load_from_memory ((stbi_uc*)data, (int)data_size, out_width, out_height, &channels, 4);
	if (result != nullptr)
		return result;

	// Check for qoi images
	qoi_desc q_desc = {};
	result = qoi_decode(data, (int)data_size, &q_desc, 4);
	if (result != nullptr) {
		*out_width    = q_desc.width;
		*out_height   = q_desc.height;
		// If QOI claims it's linear, then we'll go with that!
		if (q_desc.colorspace == QOI_LINEAR)
			*out_format = tex_format_rgba32_linear;
		return result;
	}

	return nullptr;
}

///////////////////////////////////////////

tex_t tex_create_mem_type(tex_type_ type, void *data, size_t data_size, bool32_t srgb_data) {
	tex_format_ format;
	int32_t     width, height;
	void       *col_data = tex_load_image_data(data, data_size, srgb_data, &format, &width, &height);
	if (col_data == nullptr) {
		log_warn("No compatible image format found!");
		return nullptr;
	}

	tex_t result = tex_create(type, format);
	tex_set_colors(result, width, height, col_data);

	free(col_data);
	return result;
}

///////////////////////////////////////////

tex_t tex_create_mem(void *data, size_t data_size, bool32_t srgb_data) {
	return tex_create_mem_type(tex_type_image, data, data_size, srgb_data);
}

///////////////////////////////////////////

tex_t tex_create_color32(color32 *data, int32_t width, int32_t height, bool32_t srgb_data) {
	tex_t result = tex_create(tex_type_image, srgb_data ? tex_format_rgba32 : tex_format_rgba32_linear);
	tex_set_colors(result, width, height, data);
	return result;
}

///////////////////////////////////////////

tex_t tex_create_color128(color128 *data, int32_t width, int32_t height, bool32_t srgb_data) {
	tex_t    result = tex_create(tex_type_image, srgb_data ? tex_format_rgba32 : tex_format_rgba32_linear);
	color32 *color  = sk_malloc_t(color32, width * height);
	for (size_t i = 0; i < width*height; i++)
		color[i] = color_to_32(data[i]);
	tex_set_colors(result, width, height, color);
	free(color);
	return result;
}

///////////////////////////////////////////

tex_t tex_create_file_type(const char *file, tex_type_ type, bool32_t srgb_data) {
	tex_t result = tex_find(file);
	if (result != nullptr)
		return result;

	void  *file_data;
	size_t file_size;
	if (!platform_read_file(assets_file(file), &file_data, &file_size)) {
		log_warnf("Texture file failed to load: %s", file);
		return nullptr;
	}

	result = tex_create_mem_type(type, file_data, file_size, srgb_data);
	free(file_data);

	if (result == nullptr) {
		log_warnf("Texture file failed to load: %s", file);
		return nullptr;
	}
	tex_set_id(result, file);
	
	return result;
}

///////////////////////////////////////////

tex_t tex_create_file(const char *file, bool32_t srgb_data) {
	return tex_create_file_type(file, tex_type_image, srgb_data);
}

///////////////////////////////////////////

tex_t _tex_create_file_arr(tex_type_ type, const char **files, int32_t file_count, bool32_t srgb_data, spherical_harmonics_t *out_sh_lighting_info) {
	// Hash the names of all of the files together
	uint64_t hash = HASH_FNV64_START;
	for (size_t i = 0; i < file_count; i++) {
		hash = hash_fnv64_string(files[i], hash);
	}
	char file_id[64];
	snprintf(file_id, sizeof(file_id), "sk_arr::%" PRIu64, hash);

	// And see if it's already been loaded
	tex_t result = tex_find(file_id);
	if (result != nullptr) {
		if (out_sh_lighting_info != nullptr)
			*out_sh_lighting_info = tex_get_cubemap_lighting(result);
		return result;
	}

	// Load all files
	void **data = sk_malloc_t(void*, file_count);
	int  final_width  = 0;
	int  final_height = 0;
	tex_format_ final_format = tex_format_none;
	bool loaded       = true;
	for (size_t i = 0; i < file_count; i++) {
		int width    = 0;
		int height   = 0;
		tex_format_ format = tex_format_none;

		// TODO: this will fail on weird file systems! Also support HDRs
		void  *file_data;
		size_t file_size;
		if (!platform_read_file(assets_file(files[i]), &file_data, &file_size)) {
			log_warnf("Texture file failed to load: %s", files[i]);
			loaded = false;
			break;
		}
		data[i] = tex_load_image_data(file_data, file_size, srgb_data, &format, &width, &height);

		// Check if there were issues, or one of the images is the wrong size!
		if (data[i] == nullptr || 
			(final_width  != 0 && final_width  != width ) ||
			(final_height != 0 && final_height != height) ||
			(final_format != tex_format_none && final_format != format)) {
			loaded = false;
			log_errf("Issue loading image array '%s', incompatible format, or images are of mismatching formats or sizes.", files[i]);
			break;
		}
		final_width  = width;
		final_height = height;
		final_format = format;
	}

	// free memory if we failed
	if (!loaded) {
		for (size_t i = 0; i < file_count; i++) {
			free(data[i]);
		}
		free(data);
		return nullptr;
	}

	// Create with the data we have
	result = tex_create(type, srgb_data ? tex_format_rgba32 : tex_format_rgba32_linear);
	tex_set_color_arr(result, final_width, final_height, data, file_count, out_sh_lighting_info);
	tex_set_id       (result, file_id);

	if (out_sh_lighting_info != nullptr)
		*out_sh_lighting_info = tex_get_cubemap_lighting(result);

	for (size_t i = 0; i < file_count; i++) {
		free(data[i]);
	}
	free(data);

	return result;
}

///////////////////////////////////////////

tex_t tex_create_file_arr(const char **files, int32_t file_count, bool32_t srgb_data) {
	return _tex_create_file_arr(tex_type_image, files, file_count, srgb_data, nullptr);
}

///////////////////////////////////////////

tex_t tex_create_cubemap_files(const char **cube_face_file_xxyyzz, bool32_t srgb_data, spherical_harmonics_t *out_sh_lighting_info) {
	return _tex_create_file_arr(tex_type_image | tex_type_cubemap, cube_face_file_xxyyzz, 6, srgb_data, out_sh_lighting_info);
}

///////////////////////////////////////////

tex_t tex_create_cubemap_file(const char *equirectangular_file, bool32_t srgb_data, spherical_harmonics_t *out_sh_lighting_info) {
	char equirect_id[64];
	snprintf(equirect_id, sizeof(equirect_id), "sk_equi::%" PRIu64, hash_fnv64_string(equirectangular_file));

	tex_t result = tex_find(equirect_id);
	if (result != nullptr) {
		if (out_sh_lighting_info != nullptr)
			*out_sh_lighting_info = tex_get_cubemap_lighting(result);
		return result;
	}

	const vec3 up   [6] = { vec3_up, vec3_up, -vec3_forward, vec3_forward, vec3_up, vec3_up };
	const vec3 fwd  [6] = { {1,0,0}, {-1,0,0}, {0,-1,0}, {0,1,0}, {0,0,1}, {0,0,-1} };
	const vec3 right[6] = { {0,0,-1}, {0,0,1}, {1,0,0}, {1,0,0}, {1,0,0}, {-1,0,0} };

	tex_t equirect = tex_create_file_type(equirectangular_file, tex_type_image_nomips, srgb_data ? tex_format_rgba32 : tex_format_rgba32_linear);
	tex_set_address(equirect, tex_address_clamp);
	if (equirect == nullptr)
		return nullptr;
	equirect->header.id = hash_fnv64_string("temp/equirectid");
	material_t convert_material = material_find(default_id_material_equirect);

	material_set_texture( convert_material, "source", equirect );

	tex_t    face    = tex_create(tex_type_rendertarget, equirect->format);
	void    *data[6] = {};
	int      width   = equirect->tex.height / 2;
	int      height  = width;
	size_t   size    = (size_t)width*(size_t)height*tex_format_size(equirect->format);
	tex_set_colors(face, width, height, nullptr);
	for (size_t i = 0; i < 6; i++) {
		material_set_vector(convert_material, "up",      { up   [i].x, up   [i].y, up   [i].z, 0 });
		material_set_vector(convert_material, "right",   { right[i].x, right[i].y, right[i].z, 0 });
		material_set_vector(convert_material, "forward", { fwd  [i].x, fwd  [i].y, fwd  [i].z, 0 });

		render_blit (face, convert_material);
		data[i] = sk_malloc(size);
		tex_get_data(face, data[i], size);
#if defined(SKG_OPENGL)
		int32_t line_size = tex_format_size(equirect->format) * width;
		void   *tmp       = sk_malloc(line_size);
		for (int32_t y = 0; y < height/2; y++) {
			void *top_line = ((uint8_t*)data[i]) + line_size * y;
			void *bot_line = ((uint8_t*)data[i]) + line_size * ((height-1) - y);
			memcpy(tmp,      top_line, line_size);
			memcpy(top_line, bot_line, line_size);
			memcpy(bot_line, tmp,      line_size);
		}
		free(tmp);
#endif
	}

	result = tex_create(tex_type_image | tex_type_cubemap, equirect->format);
	tex_set_color_arr(result, width, height, (void**)&data, 6, out_sh_lighting_info);
	tex_set_id       (result, equirect_id);

	material_release(convert_material);
	tex_release(equirect);
	tex_release(face);

	for (size_t i = 0; i < 6; i++) {
		free(data[i]);
	}

	return result;
}

///////////////////////////////////////////

void tex_addref(tex_t texture) {
	assets_addref(texture->header);
}

///////////////////////////////////////////

void tex_release(tex_t texture) {
	if (texture == nullptr)
		return;
	assets_releaseref(texture->header);
}

///////////////////////////////////////////

void tex_destroy(tex_t tex) {
	free(tex->light_info);
	skg_tex_destroy(&tex->tex);
	if (tex->depth_buffer != nullptr) tex_release(tex->depth_buffer);
	
	*tex = {};
}

///////////////////////////////////////////

void tex_set_color_arr(tex_t texture, int32_t width, int32_t height, void **data, int32_t data_count, spherical_harmonics_t *sh_lighting_info, int32_t multisample) {
	bool dynamic        = texture->type & tex_type_dynamic;
	bool different_size = texture->tex.width != width || texture->tex.height != height || texture->tex.array_count != data_count;
	if (!different_size && (data == nullptr || *data == nullptr))
		return;
	if (!skg_tex_is_valid(&texture->tex) || different_size || (!different_size && !dynamic)) {
		skg_tex_destroy(&texture->tex);

		if (!different_size && !dynamic)
			texture->type &= tex_type_dynamic;

		skg_tex_fmt_  format   = (skg_tex_fmt_)texture->format;
		skg_use_      use      = texture->type & tex_type_dynamic ? skg_use_dynamic  : skg_use_static;
		skg_mip_      use_mips = texture->type & tex_type_mips    ? skg_mip_generate : skg_mip_none;
		skg_tex_type_ type     = skg_tex_type_image;
		if      (texture->type & tex_type_cubemap)      type = skg_tex_type_cubemap;
		else if (texture->type & tex_type_depth)        type = skg_tex_type_depth;
		else if (texture->type & tex_type_rendertarget) type = skg_tex_type_rendertarget;
		texture->tex = skg_tex_create(type, use, format, use_mips);
		tex_set_options(texture, texture->sample_mode, texture->address_mode, texture->anisotropy);

		skg_tex_set_contents_arr(&texture->tex, (const void**)data, data_count, width, height, multisample);
		if (texture->depth_buffer != nullptr) {
			tex_set_color_arr(texture->depth_buffer, width, height, nullptr, texture->tex.array_count, nullptr, multisample);
			tex_set_zbuffer(texture, texture->depth_buffer);
		}
	} else if (dynamic) {
		skg_tex_set_contents_arr(&texture->tex, (const void**)data, data_count, width, height, multisample);
	} else {
		log_warn("Attempting additional writes to a non-dynamic texture!");
	}

	if (sh_lighting_info != nullptr)
		*sh_lighting_info = tex_get_cubemap_lighting(texture);
}

///////////////////////////////////////////

spherical_harmonics_t tex_get_cubemap_lighting(tex_t cubemap_texture) {
	skg_tex_t *tex = &cubemap_texture->tex;
	
	if (cubemap_texture->light_info != nullptr)
		return *cubemap_texture->light_info;

	// If they want spherical harmonics, lets calculate it for them, or give
	// them a good error message!
	if (tex->width != tex->height || tex->array_count != 6) {
		log_warn("Invalid texture size for calculating spherical harmonics. Must be an equirect image, or have 6 images all same width and height.");
		return {};
	} else if (!(tex->format == tex_format_rgba32 || tex->format == tex_format_rgba32_linear || tex->format == tex_format_rgba128)) {
		log_warn("Invalid texture format for calculating spherical harmonics, must be rgba32 or rgba128.");
		return {};
	} else {
		int32_t  mip_level = maxi((int32_t)0, (int32_t)skg_mip_count(cubemap_texture->tex.width, cubemap_texture->tex.height) - 6);
		int32_t  mip_w, mip_h;
		skg_mip_dimensions(tex->width, tex->height, mip_level, &mip_w, &mip_h);
		size_t   face_size       = skg_tex_fmt_size(tex->format) * mip_w * mip_h;
		size_t   cube_size       = face_size * 6;
		uint8_t *cube_color_data = (uint8_t*)sk_malloc(cube_size);
		void    *data[6];
		for (int32_t f = 0; f < 6; f++) {
			data[f] = cube_color_data + face_size * f;
			skg_tex_get_mip_contents_arr(tex, mip_level, f, data[f], face_size);
		}

		cubemap_texture ->light_info = sk_malloc_t(spherical_harmonics_t, 1);
		*cubemap_texture->light_info = sh_calculate(data, cubemap_texture->format, mip_w);
		free(cube_color_data);
		return *cubemap_texture->light_info;
	}
}

///////////////////////////////////////////

void tex_set_colors(tex_t texture, int32_t width, int32_t height, void *data) {
	void *data_arr[1] = { data };
	tex_set_color_arr(texture, width, height, data_arr, 1);
}

///////////////////////////////////////////

void tex_set_options(tex_t texture, tex_sample_ sample, tex_address_ address_mode, int32_t anisotropy_level) {
	texture->address_mode = address_mode;
	texture->anisotropy   = anisotropy_level;
	texture->sample_mode  = sample;

	skg_tex_address_ skg_addr;
	switch (address_mode) {
	case tex_address_clamp:  skg_addr = skg_tex_address_clamp;  break;
	case tex_address_wrap:   skg_addr = skg_tex_address_repeat; break;
	case tex_address_mirror: skg_addr = skg_tex_address_mirror; break;
	default: skg_addr = skg_tex_address_repeat;
	}

	skg_tex_sample_ skg_sample;
	switch (sample) {
	case tex_sample_linear:     skg_sample = skg_tex_sample_linear;      break; // Technically trilinear
	case tex_sample_point:      skg_sample = skg_tex_sample_point;       break;
	case tex_sample_anisotropic:skg_sample = skg_tex_sample_anisotropic; break;
	default: skg_sample = skg_tex_sample_linear;
	}

	skg_tex_settings(&texture->tex, skg_addr, skg_sample, anisotropy_level);
}

///////////////////////////////////////////

tex_format_ tex_get_format(tex_t texture) {
	return texture->format;
}

///////////////////////////////////////////

int32_t tex_get_width(tex_t texture) {
	return texture->tex.width;
}

///////////////////////////////////////////

int32_t tex_get_height(tex_t texture) {
	return texture->tex.height;
}

///////////////////////////////////////////

void tex_set_sample(tex_t texture, tex_sample_ sample) {
	texture->sample_mode = sample;
	tex_set_options(texture, texture->sample_mode, texture->address_mode, texture->anisotropy);
}

///////////////////////////////////////////

tex_sample_ tex_get_sample(tex_t texture) {
	return texture->sample_mode;
}

///////////////////////////////////////////

void tex_set_address(tex_t texture, tex_address_ address_mode) {
	texture->address_mode = address_mode;
	tex_set_options(texture, texture->sample_mode, texture->address_mode, texture->anisotropy);
}

///////////////////////////////////////////

tex_address_ tex_get_address(tex_t texture) {
	return texture->address_mode;
}

///////////////////////////////////////////

void tex_set_anisotropy(tex_t texture, int32_t anisotropy_level) {
	texture->anisotropy = anisotropy_level;
	tex_set_options(texture, texture->sample_mode, texture->address_mode, texture->anisotropy);
}

///////////////////////////////////////////

int32_t tex_get_anisotropy(tex_t texture) {
	return texture->anisotropy;
}

///////////////////////////////////////////

size_t tex_format_size(tex_format_ format) {
	switch (format) {
	case tex_format_depth32:
	case tex_format_depthstencil:
	case tex_format_r32:
	case tex_format_rgba32:
	case tex_format_rgba32_linear: return sizeof(color32);
	case tex_format_rgba64:        return sizeof(uint16_t)*4;
	case tex_format_rgba128:       return sizeof(color128);
	case tex_format_r16:
	case tex_format_depth16:       return sizeof(uint16_t);
	case tex_format_r8:            return sizeof(uint8_t);
	default: return sizeof(color32);
	}
}

///////////////////////////////////////////

tex_format_ tex_get_tex_format(int64_t native_fmt) {
	skg_tex_fmt_ skg_fmt = skg_tex_fmt_from_native(native_fmt);

	// tex_format_ should be kept to match skg_tex_fmt_, so this should 
	// always be valid.
	return (tex_format_)skg_fmt;
}

///////////////////////////////////////////

void tex_get_data(tex_t texture, void *out_data, size_t out_data_size) {
	memset(out_data, 0, out_data_size);
	if (!skg_tex_get_contents(&texture->tex, out_data, out_data_size))
		log_warn("Couldn't get texture contents!");
}

///////////////////////////////////////////

tex_t tex_gen_color(color128 color, int32_t width, int32_t height, tex_type_ type, tex_format_ format) {
	uint8_t data[sizeof(color128)] = {};
	size_t  data_step = 0;
	switch (format) {
	case tex_format_rgba32:
	case tex_format_rgba32_linear: { color32  c = color_to_32(color);                             memcpy(data, &c,     sizeof(c)); data_step = sizeof(c); } break;
	case tex_format_bgra32_linear:
	case tex_format_bgra32:        { color32  c = color_to_32({color.b,color.g,color.r,color.a}); memcpy(data, &c,     sizeof(c)); data_step = sizeof(c);} break;
	case tex_format_rgba128:       {                                                              memcpy(data, &color, sizeof(color)); data_step = sizeof(color); } break;
	case tex_format_r32:           { float    c = color.r;                                        memcpy(data, &c,     sizeof(c)); data_step = sizeof(c); } break;
	case tex_format_r16:           { uint16_t c = (uint16_t)(color.r*USHRT_MAX);                  memcpy(data, &c,     sizeof(c)); data_step = sizeof(c); } break;
	case tex_format_r8:            { uint8_t  c = (uint8_t )(color.r*255.0f   );                  memcpy(data, &c,     sizeof(c)); data_step = sizeof(c); } break;
	default: log_err("tex_gen_color doesn't support the provided color format."); return nullptr;
	}

	// Create an array of color values the size of our texture
	uint8_t *color_data = (uint8_t *)sk_malloc(data_step * width * height);
	uint8_t *color_curr = color_data;
	for (size_t i = 0; i < width*height; i++) {
		memcpy(color_curr, data, data_step);
		color_curr += data_step;
	}

	// And upload it to the GPU
	tex_t result = tex_create(type, format);
	tex_set_colors(result, width, height, color_data);

	free(color_data);

	return result;
}

///////////////////////////////////////////

tex_t tex_gen_cubemap(const gradient_t gradient_bot_to_top, vec3 gradient_dir, int32_t resolution, spherical_harmonics_t *out_sh_lighting_info) {
	tex_t result = tex_create(tex_type_image | tex_type_cubemap, tex_format_rgba128);
	if (result == nullptr) {
		return nullptr;
	}
	gradient_dir = vec3_normalize(gradient_dir);

	int32_t size  = resolution;
	// make size a power of two
	int32_t power = (int32_t)logf((float)size);
	if (pow(2, power) < size)
		power += 1;
	size = (int32_t)pow(2, power);

	float    half_px = 0.5f / size;
	int32_t  size2 = size * size;
	color128*data[6];
	for (int32_t i = 0; i < 6; i++) {
		data[i] = sk_malloc_t(color128, size2);
		vec3 p1 = math_cubemap_corner(i * 4);
		vec3 p2 = math_cubemap_corner(i * 4+1);
		vec3 p3 = math_cubemap_corner(i * 4+2);
		vec3 p4 = math_cubemap_corner(i * 4+3); 

		for (int32_t y = 0; y < size; y++) {
			float py = 1 - (y / (float)size + half_px);

			// Top face is flipped on both axes
			if (i == 2) {
				py = 1 - py;
			}
		for (int32_t x = 0; x < size; x++) {
			float px = x / (float)size + half_px;

			// Top face is flipped on both axes
			if (i == 2) {
				px = 1 - px;
			}

			vec3 pl = vec3_lerp(p1, p4, py);
			vec3 pr = vec3_lerp(p2, p3, py);
			vec3 pt = vec3_lerp(pl, pr, px);
			pt = vec3_normalize(pt);

			float pct = (vec3_dot(pt, gradient_dir)+1)*0.5f;
			data[i][x + y * size] = gradient_get(gradient_bot_to_top, pct);
		}
		}
	}

	tex_set_color_arr(result, (int32_t)size, (int32_t)size, (void**)data, 6, out_sh_lighting_info);
	for (int32_t i = 0; i < 6; i++) {
		free(data[i]);
	}

	return result;
}

///////////////////////////////////////////

tex_t tex_gen_cubemap_sh(const spherical_harmonics_t& lookup, int32_t face_size, float light_spot_size_pct, float light_spot_intensity) {
	tex_t result = tex_create(tex_type_image | tex_type_cubemap, tex_format_rgba128);
	if (result == nullptr) {
		return nullptr;
	}

	// Calculate information used to create the light spot
	vec3     light_dir = sh_dominant_dir(lookup);
	color128 light_col = sh_lookup      (lookup, -light_dir) * light_spot_intensity;
	vec3     light_pt  = { 100000,100000,100000 };
	for (int32_t i = 0; i < 6; i++) {
		vec3 p1 = math_cubemap_corner(i * 4);
		vec3 p2 = math_cubemap_corner(i * 4 + 1);
		vec3 p3 = math_cubemap_corner(i * 4 + 2);
		plane_t plane = plane_from_points(p1, p2, p3);
		vec3    pt;
		if (!plane_ray_intersect(plane, { vec3_zero, light_dir }, &pt) && vec3_magnitude_sq(pt) < vec3_magnitude_sq(light_pt))
			light_pt = pt;
	}

	int32_t size  = face_size;
	// make size a power of two
	int32_t power = (int32_t)logf((float)size);
	if (pow(2, power) < size)
		power += 1;
	size = (int32_t)pow(2, power);

	float     half_px = 0.5f / size;
	int32_t   size2 = size * size;
	color128 *data[6];
	for (int32_t i = 0; i < 6; i++) {
		data[i] = sk_malloc_t(color128, size2);
		vec3 p1 = math_cubemap_corner(i * 4);
		vec3 p2 = math_cubemap_corner(i * 4+1);
		vec3 p3 = math_cubemap_corner(i * 4+2);
		vec3 p4 = math_cubemap_corner(i * 4+3);

		for (int32_t y = 0; y < size; y++) {
			float py = 1 - (y / (float)size + half_px);

			// Top face is flipped on both axes
			if (i == 2) {
				py = 1 - py;
			}
			for (int32_t x = 0; x < size; x++) {
				float px = x / (float)size + half_px;

				// Top face is flipped on both axes
				if (i == 2) {
					px = 1 - px;
				}

				vec3 pl = vec3_lerp(p1, p4, py);
				vec3 pr = vec3_lerp(p2, p3, py);
				vec3 pt = vec3_lerp(pl, pr, px);
				float dist = fmaxf(fmaxf(fabsf(pt.x-light_pt.x), fabsf(pt.y-light_pt.y)), fabsf(pt.z-light_pt.z));
				pt = vec3_normalize(pt);

				if (dist < light_spot_size_pct) {
					data[i][x + y * size] = light_col;
				} else {
					data[i][x + y * size] = sh_lookup(lookup, pt);
				}
			}
		}
	}

	tex_set_color_arr(result, (int32_t)size, (int32_t)size, (void**)data, 6);
	for (int32_t i = 0; i < 6; i++) {
		free(data[i]);
	}

	return result;
}

} // namespace sk