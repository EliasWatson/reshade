/*
 * Copyright (C) 2021 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/reshade#license
 */

#include "render_d3d11.hpp"
#include "render_d3d11_utils.hpp"

using namespace reshade::api;

static void convert_memory_heap_to_d3d_usage(memory_heap heap, D3D11_USAGE &usage, UINT &cpu_access_flags)
{
	switch (heap)
	{
	case memory_heap::gpu_only:
		usage = D3D11_USAGE_DEFAULT;
		break;
	case memory_heap::cpu_to_gpu:
		usage = D3D11_USAGE_DYNAMIC;
		cpu_access_flags |= D3D11_CPU_ACCESS_WRITE;
		break;
	case memory_heap::gpu_to_cpu:
		usage = D3D11_USAGE_STAGING;
		cpu_access_flags |= D3D11_CPU_ACCESS_READ;
		break;
	case memory_heap::cpu_only:
		usage = D3D11_USAGE_STAGING;
		cpu_access_flags |= D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
		break;
	}
}
static void convert_d3d_usage_to_memory_heap(D3D11_USAGE usage, memory_heap &heap)
{
	switch (usage)
	{
	case D3D11_USAGE_DEFAULT:
	case D3D11_USAGE_IMMUTABLE:
		heap = memory_heap::gpu_only;
		break;
	case D3D11_USAGE_DYNAMIC:
		heap = memory_heap::cpu_to_gpu;
		break;
	case D3D11_USAGE_STAGING:
		heap = memory_heap::gpu_to_cpu;
		break;
	}
}

static void convert_resource_usage_to_bind_flags(resource_usage usage, UINT &bind_flags)
{
	if ((usage & resource_usage::render_target) != 0)
		bind_flags |= D3D11_BIND_RENDER_TARGET;
	else
		bind_flags &= ~D3D11_BIND_RENDER_TARGET;

	if ((usage & resource_usage::depth_stencil) != 0)
		bind_flags |= D3D11_BIND_DEPTH_STENCIL;
	else
		bind_flags &= ~D3D11_BIND_DEPTH_STENCIL;

	if ((usage & resource_usage::shader_resource) != 0)
		bind_flags |= D3D11_BIND_SHADER_RESOURCE;
	else
		bind_flags &= ~D3D11_BIND_SHADER_RESOURCE;

	if ((usage & resource_usage::unordered_access) != 0)
		bind_flags |= D3D11_BIND_UNORDERED_ACCESS;
	else
		bind_flags &= ~D3D11_BIND_UNORDERED_ACCESS;

	if ((usage & resource_usage::index_buffer) != 0)
		bind_flags |= D3D11_BIND_INDEX_BUFFER;
	else
		bind_flags &= ~D3D11_BIND_INDEX_BUFFER;

	if ((usage & resource_usage::vertex_buffer) != 0)
		bind_flags |= D3D11_BIND_VERTEX_BUFFER;
	else
		bind_flags &= ~D3D11_BIND_VERTEX_BUFFER;

	if ((usage & resource_usage::constant_buffer) != 0)
		bind_flags |= D3D11_BIND_CONSTANT_BUFFER;
	else
		bind_flags &= ~D3D11_BIND_CONSTANT_BUFFER;
}
static void convert_bind_flags_to_resource_usage(UINT bind_flags, resource_usage &usage)
{
	// Resources are generally copyable in D3D11
	usage |= resource_usage::copy_dest | resource_usage::copy_source;

	if ((bind_flags & D3D11_BIND_RENDER_TARGET) != 0)
		usage |= resource_usage::render_target;
	if ((bind_flags & D3D11_BIND_DEPTH_STENCIL) != 0)
		usage |= resource_usage::depth_stencil;
	if ((bind_flags & D3D11_BIND_SHADER_RESOURCE) != 0)
		usage |= resource_usage::shader_resource;
	if ((bind_flags & D3D11_BIND_UNORDERED_ACCESS) != 0)
		usage |= resource_usage::unordered_access;

	if ((bind_flags & D3D11_BIND_INDEX_BUFFER) != 0)
		usage |= resource_usage::index_buffer;
	if ((bind_flags & D3D11_BIND_VERTEX_BUFFER) != 0)
		usage |= resource_usage::vertex_buffer;
	if ((bind_flags & D3D11_BIND_CONSTANT_BUFFER) != 0)
		usage |= resource_usage::constant_buffer;
}

void reshade::d3d11::convert_blend_op(blend_op value, D3D11_BLEND_OP &internal_value)
{
	internal_value = static_cast<D3D11_BLEND_OP>(static_cast<uint32_t>(value) + 1);
}
void reshade::d3d11::convert_blend_factor(blend_factor value, D3D11_BLEND &internal_value)
{
	switch (value)
	{
	case blend_factor::zero:
		internal_value = D3D11_BLEND_ZERO;
		break;
	case blend_factor::one:
		internal_value = D3D11_BLEND_ONE;
		break;
	case blend_factor::src_color:
		internal_value = D3D11_BLEND_SRC_COLOR;
		break;
	case blend_factor::inv_src_color:
		internal_value = D3D11_BLEND_INV_SRC_COLOR;
		break;
	case blend_factor::dst_color:
		internal_value = D3D11_BLEND_DEST_COLOR;
		break;
	case blend_factor::inv_dst_color:
		internal_value = D3D11_BLEND_INV_DEST_COLOR;
		break;
	case blend_factor::src_alpha:
		internal_value = D3D11_BLEND_SRC_ALPHA;
		break;
	case blend_factor::inv_src_alpha:
		internal_value = D3D11_BLEND_INV_SRC_ALPHA;
		break;
	case blend_factor::dst_alpha:
		internal_value = D3D11_BLEND_DEST_ALPHA;
		break;
	case blend_factor::inv_dst_alpha:
		internal_value = D3D11_BLEND_INV_DEST_ALPHA;
		break;
	case blend_factor::constant_color:
	case blend_factor::constant_alpha:
		internal_value = D3D11_BLEND_BLEND_FACTOR;
		break;
	case blend_factor::inv_constant_color:
	case blend_factor::inv_constant_alpha:
		internal_value = D3D11_BLEND_INV_BLEND_FACTOR;
		break;
	case blend_factor::src_alpha_sat:
		internal_value = D3D11_BLEND_SRC_ALPHA_SAT;
		break;
	case blend_factor::src1_color:
		internal_value = D3D11_BLEND_SRC1_COLOR;
		break;
	case blend_factor::inv_src1_color:
		internal_value = D3D11_BLEND_INV_SRC1_COLOR;
		break;
	case blend_factor::src1_alpha:
		internal_value = D3D11_BLEND_SRC1_ALPHA;
		break;
	case blend_factor::inv_src1_alpha:
		internal_value = D3D11_BLEND_INV_SRC1_ALPHA;
		break;
	}
}
void reshade::d3d11::convert_fill_mode(fill_mode value, D3D11_FILL_MODE &internal_value)
{
	switch (value)
	{
	case fill_mode::solid:
		internal_value = D3D11_FILL_SOLID;
		break;
	case fill_mode::wireframe:
		internal_value = D3D11_FILL_WIREFRAME;
		break;
	case fill_mode::point:
		assert(false);
		break;
	}
}
void reshade::d3d11::convert_cull_mode(cull_mode value, D3D11_CULL_MODE &internal_value)
{
	assert(value != cull_mode::front_and_back);
	internal_value = static_cast<D3D11_CULL_MODE>(static_cast<uint32_t>(value) + 1);
}
void reshade::d3d11::convert_compare_op(compare_op value, D3D11_COMPARISON_FUNC &internal_value)
{
	internal_value = static_cast<D3D11_COMPARISON_FUNC>(static_cast<uint32_t>(value) + 1);
}
void reshade::d3d11::convert_stencil_op(stencil_op value, D3D11_STENCIL_OP &internal_value)
{
	internal_value = static_cast<D3D11_STENCIL_OP>(static_cast<uint32_t>(value) + 1);
}
auto reshade::d3d11::convert_primitive_topology(api::primitive_topology value) -> D3D11_PRIMITIVE_TOPOLOGY
{
	return static_cast<D3D11_PRIMITIVE_TOPOLOGY>(value);
}

void reshade::d3d11::convert_sampler_desc(const sampler_desc &desc, D3D11_SAMPLER_DESC &internal_desc)
{
	internal_desc.Filter = static_cast<D3D11_FILTER>(desc.filter);
	internal_desc.AddressU = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(desc.address_u);
	internal_desc.AddressV = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(desc.address_v);
	internal_desc.AddressW = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(desc.address_w);
	internal_desc.MipLODBias = desc.mip_lod_bias;
	internal_desc.MaxAnisotropy = static_cast<UINT>(desc.max_anisotropy);
	internal_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	internal_desc.BorderColor[0] = 0.0f;
	internal_desc.BorderColor[1] = 0.0f;
	internal_desc.BorderColor[2] = 0.0f;
	internal_desc.BorderColor[3] = 0.0f;
	internal_desc.MinLOD = desc.min_lod;
	internal_desc.MaxLOD = desc.max_lod;
}
sampler_desc reshade::d3d11::convert_sampler_desc(const D3D11_SAMPLER_DESC &internal_desc)
{
	sampler_desc desc = {};
	desc.filter = static_cast<texture_filter>(internal_desc.Filter);
	desc.address_u = static_cast<texture_address_mode>(internal_desc.AddressU);
	desc.address_v = static_cast<texture_address_mode>(internal_desc.AddressV);
	desc.address_w = static_cast<texture_address_mode>(internal_desc.AddressW);
	desc.mip_lod_bias = internal_desc.MipLODBias;
	desc.max_anisotropy = static_cast<float>(internal_desc.MaxAnisotropy);
	desc.min_lod = internal_desc.MinLOD;
	desc.max_lod = internal_desc.MaxLOD;
	return desc;
}

void reshade::d3d11::convert_resource_desc(const resource_desc &desc, D3D11_BUFFER_DESC &internal_desc)
{
	assert(desc.type == resource_type::buffer);
	assert(desc.buffer.size <= std::numeric_limits<UINT>::max());
	internal_desc.ByteWidth = static_cast<UINT>(desc.buffer.size);
	convert_memory_heap_to_d3d_usage(desc.heap, internal_desc.Usage, internal_desc.CPUAccessFlags);
	convert_resource_usage_to_bind_flags(desc.usage, internal_desc.BindFlags);
}
void reshade::d3d11::convert_resource_desc(const resource_desc &desc, D3D11_TEXTURE1D_DESC &internal_desc)
{
	assert(desc.type == resource_type::texture_1d);
	internal_desc.Width = desc.texture.width;
	assert(desc.texture.height == 1);
	internal_desc.MipLevels = desc.texture.levels;
	internal_desc.ArraySize = desc.texture.depth_or_layers;
	internal_desc.Format = static_cast<DXGI_FORMAT>(desc.texture.format);
	assert(desc.texture.samples == 1);
	convert_memory_heap_to_d3d_usage(desc.heap, internal_desc.Usage, internal_desc.CPUAccessFlags);
	convert_resource_usage_to_bind_flags(desc.usage, internal_desc.BindFlags);
}
void reshade::d3d11::convert_resource_desc(const resource_desc &desc, D3D11_TEXTURE2D_DESC &internal_desc)
{
	assert(desc.type == resource_type::texture_2d);
	internal_desc.Width = desc.texture.width;
	internal_desc.Height = desc.texture.height;
	internal_desc.MipLevels = desc.texture.levels;
	internal_desc.ArraySize = desc.texture.depth_or_layers;
	internal_desc.Format = static_cast<DXGI_FORMAT>(desc.texture.format);
	internal_desc.SampleDesc.Count = desc.texture.samples;
	convert_memory_heap_to_d3d_usage(desc.heap, internal_desc.Usage, internal_desc.CPUAccessFlags);
	convert_resource_usage_to_bind_flags(desc.usage, internal_desc.BindFlags);
}
void reshade::d3d11::convert_resource_desc(const resource_desc &desc, D3D11_TEXTURE3D_DESC &internal_desc)
{
	assert(desc.type == resource_type::texture_3d);
	internal_desc.Width = desc.texture.width;
	internal_desc.Height = desc.texture.height;
	internal_desc.Depth = desc.texture.depth_or_layers;
	internal_desc.MipLevels = desc.texture.levels;
	internal_desc.Format = static_cast<DXGI_FORMAT>(desc.texture.format);
	assert(desc.texture.samples == 1);
	convert_memory_heap_to_d3d_usage(desc.heap, internal_desc.Usage, internal_desc.CPUAccessFlags);
	convert_resource_usage_to_bind_flags(desc.usage, internal_desc.BindFlags);
}
resource_desc reshade::d3d11::convert_resource_desc(const D3D11_BUFFER_DESC &internal_desc)
{
	resource_desc desc = {};
	desc.type = resource_type::buffer;
	desc.buffer.size = internal_desc.ByteWidth;
	convert_d3d_usage_to_memory_heap(internal_desc.Usage, desc.heap);
	convert_bind_flags_to_resource_usage(internal_desc.BindFlags, desc.usage);
	return desc;
}
resource_desc reshade::d3d11::convert_resource_desc(const D3D11_TEXTURE1D_DESC &internal_desc)
{
	resource_desc desc = {};
	desc.type = resource_type::texture_1d;
	desc.texture.width = internal_desc.Width;
	desc.texture.height = 1;
	assert(internal_desc.ArraySize <= std::numeric_limits<uint16_t>::max());
	desc.texture.depth_or_layers = static_cast<uint16_t>(internal_desc.ArraySize);
	assert(internal_desc.MipLevels <= std::numeric_limits<uint16_t>::max());
	desc.texture.levels = static_cast<uint16_t>(internal_desc.MipLevels);
	desc.texture.format = static_cast<format>(internal_desc.Format);
	desc.texture.samples = 1;
	convert_d3d_usage_to_memory_heap(internal_desc.Usage, desc.heap);
	convert_bind_flags_to_resource_usage(internal_desc.BindFlags, desc.usage);
	return desc;
}
resource_desc reshade::d3d11::convert_resource_desc(const D3D11_TEXTURE2D_DESC &internal_desc)
{
	resource_desc desc = {};
	desc.type = resource_type::texture_2d;
	desc.texture.width = internal_desc.Width;
	desc.texture.height = internal_desc.Height;
	assert(internal_desc.ArraySize <= std::numeric_limits<uint16_t>::max());
	desc.texture.depth_or_layers = static_cast<uint16_t>(internal_desc.ArraySize);
	assert(internal_desc.MipLevels <= std::numeric_limits<uint16_t>::max());
	desc.texture.levels = static_cast<uint16_t>(internal_desc.MipLevels);
	desc.texture.format = static_cast<format>(internal_desc.Format);
	desc.texture.samples = static_cast<uint16_t>(internal_desc.SampleDesc.Count);
	convert_d3d_usage_to_memory_heap(internal_desc.Usage, desc.heap);
	convert_bind_flags_to_resource_usage(internal_desc.BindFlags, desc.usage);
	desc.usage |= desc.texture.samples > 1 ? resource_usage::resolve_source : resource_usage::resolve_dest;
	return desc;
}
resource_desc reshade::d3d11::convert_resource_desc(const D3D11_TEXTURE3D_DESC &internal_desc)
{
	resource_desc desc = {};
	desc.type = resource_type::texture_3d;
	desc.texture.width = internal_desc.Width;
	desc.texture.height = internal_desc.Height;
	assert(internal_desc.Depth <= std::numeric_limits<uint16_t>::max());
	desc.texture.depth_or_layers = static_cast<uint16_t>(internal_desc.Depth);
	assert(internal_desc.MipLevels <= std::numeric_limits<uint16_t>::max());
	desc.texture.levels = static_cast<uint16_t>(internal_desc.MipLevels);
	desc.texture.format = static_cast<format>(internal_desc.Format);
	desc.texture.samples = 1;
	convert_d3d_usage_to_memory_heap(internal_desc.Usage, desc.heap);
	convert_bind_flags_to_resource_usage(internal_desc.BindFlags, desc.usage);
	return desc;
}

void reshade::d3d11::convert_resource_view_desc(const resource_view_desc &desc, D3D11_DEPTH_STENCIL_VIEW_DESC &internal_desc)
{
	// Missing fields: D3D11_DEPTH_STENCIL_VIEW_DESC::Flags
	internal_desc.Format = static_cast<DXGI_FORMAT>(desc.format);
	assert(desc.type != resource_view_type::buffer && desc.texture.levels == 1);
	switch (desc.type) // Do not modifiy description in case type is 'resource_view_type::unknown'
	{
	case resource_view_type::texture_1d:
		internal_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1D;
		internal_desc.Texture1D.MipSlice = desc.texture.first_level;
		break;
	case resource_view_type::texture_1d_array:
		internal_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1DARRAY;
		internal_desc.Texture1DArray.MipSlice = desc.texture.first_level;
		internal_desc.Texture1DArray.FirstArraySlice = desc.texture.first_layer;
		internal_desc.Texture1DArray.ArraySize = desc.texture.layers;
		break;
	case resource_view_type::texture_2d:
		internal_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		internal_desc.Texture2D.MipSlice = desc.texture.first_level;
		break;
	case resource_view_type::texture_2d_array:
		internal_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		internal_desc.Texture2DArray.MipSlice = desc.texture.first_level;
		internal_desc.Texture2DArray.FirstArraySlice = desc.texture.first_layer;
		internal_desc.Texture2DArray.ArraySize = desc.texture.layers;
		break;
	case resource_view_type::texture_2d_multisample:
		internal_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
		break;
	case resource_view_type::texture_2d_multisample_array:
		internal_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
		internal_desc.Texture2DMSArray.FirstArraySlice = desc.texture.first_layer;
		internal_desc.Texture2DMSArray.ArraySize = desc.texture.layers;
		break;
	}
}
void reshade::d3d11::convert_resource_view_desc(const resource_view_desc &desc, D3D11_RENDER_TARGET_VIEW_DESC &internal_desc)
{
	internal_desc.Format = static_cast<DXGI_FORMAT>(desc.format);
	assert(desc.type != resource_view_type::buffer && desc.texture.levels == 1);
	switch (desc.type) // Do not modifiy description in case type is 'resource_view_type::unknown'
	{
	case resource_view_type::texture_1d:
		internal_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1D;
		internal_desc.Texture1D.MipSlice = desc.texture.first_level;
		break;
	case resource_view_type::texture_1d_array:
		internal_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1DARRAY;
		internal_desc.Texture1DArray.MipSlice = desc.texture.first_level;
		internal_desc.Texture1DArray.FirstArraySlice = desc.texture.first_layer;
		internal_desc.Texture1DArray.ArraySize = desc.texture.layers;
		break;
	case resource_view_type::texture_2d:
		internal_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		internal_desc.Texture2D.MipSlice = desc.texture.first_level;
		break;
	case resource_view_type::texture_2d_array:
		internal_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		internal_desc.Texture2DArray.MipSlice = desc.texture.first_level;
		internal_desc.Texture2DArray.FirstArraySlice = desc.texture.first_layer;
		internal_desc.Texture2DArray.ArraySize = desc.texture.layers;
		break;
	case resource_view_type::texture_2d_multisample:
		internal_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
		break;
	case resource_view_type::texture_2d_multisample_array:
		internal_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
		internal_desc.Texture2DMSArray.FirstArraySlice = desc.texture.first_layer;
		internal_desc.Texture2DMSArray.ArraySize = desc.texture.layers;
		break;
	case resource_view_type::texture_3d:
		internal_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
		internal_desc.Texture3D.MipSlice = desc.texture.first_level;
		internal_desc.Texture3D.FirstWSlice = desc.texture.first_layer;
		internal_desc.Texture3D.WSize = desc.texture.layers;
		break;
	}
}
void reshade::d3d11::convert_resource_view_desc(const resource_view_desc &desc, D3D11_RENDER_TARGET_VIEW_DESC1 &internal_desc)
{
	if (desc.type == resource_view_type::texture_2d || desc.type == resource_view_type::texture_2d_array)
	{
		internal_desc.Format = static_cast<DXGI_FORMAT>(desc.format);
		assert(desc.type != resource_view_type::buffer && desc.texture.levels == 1);
		switch (desc.type)
		{
		case resource_view_type::texture_2d:
			internal_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			internal_desc.Texture2D.MipSlice = desc.texture.first_level;
			// Missing fields: D3D11_TEX2D_RTV1::PlaneSlice
			break;
		case resource_view_type::texture_2d_array:
			internal_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			internal_desc.Texture2DArray.MipSlice = desc.texture.first_level;
			internal_desc.Texture2DArray.FirstArraySlice = desc.texture.first_layer;
			internal_desc.Texture2DArray.ArraySize = desc.texture.layers;
			break;
			// Missing fields: D3D11_TEX2D_ARRAY_RTV1::PlaneSlice
		}
	}
	else
	{
		convert_resource_view_desc(desc, reinterpret_cast<D3D11_RENDER_TARGET_VIEW_DESC &>(internal_desc));
	}
}
void reshade::d3d11::convert_resource_view_desc(const resource_view_desc &desc, D3D11_SHADER_RESOURCE_VIEW_DESC &internal_desc)
{
	internal_desc.Format = static_cast<DXGI_FORMAT>(desc.format);
	switch (desc.type) // Do not modifiy description in case type is 'resource_view_type::unknown'
	{
	case resource_view_type::buffer:
		internal_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		assert(desc.buffer.offset <= std::numeric_limits<UINT>::max());
		internal_desc.Buffer.FirstElement = static_cast<UINT>(desc.buffer.offset);
		assert(desc.buffer.size <= std::numeric_limits<UINT>::max());
		internal_desc.Buffer.NumElements = static_cast<UINT>(desc.buffer.size);
		break;
	case resource_view_type::texture_1d:
		internal_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
		internal_desc.Texture1D.MostDetailedMip = desc.texture.first_level;
		internal_desc.Texture1D.MipLevels = desc.texture.levels;
		break;
	case resource_view_type::texture_1d_array:
		internal_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
		internal_desc.Texture1DArray.MostDetailedMip = desc.texture.first_level;
		internal_desc.Texture1DArray.MipLevels = desc.texture.levels;
		internal_desc.Texture1DArray.FirstArraySlice = desc.texture.first_layer;
		internal_desc.Texture1DArray.ArraySize = desc.texture.layers;
		break;
	case resource_view_type::texture_2d:
		internal_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		internal_desc.Texture2D.MostDetailedMip = desc.texture.first_level;
		internal_desc.Texture2D.MipLevels = desc.texture.levels;
		break;
	case resource_view_type::texture_2d_array:
		internal_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		internal_desc.Texture2DArray.MostDetailedMip = desc.texture.first_level;
		internal_desc.Texture2DArray.MipLevels = desc.texture.levels;
		internal_desc.Texture2DArray.FirstArraySlice = desc.texture.first_layer;
		internal_desc.Texture2DArray.ArraySize = desc.texture.layers;
		break;
	case resource_view_type::texture_2d_multisample:
		internal_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
		break;
	case resource_view_type::texture_2d_multisample_array:
		internal_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
		internal_desc.Texture2DMSArray.FirstArraySlice = desc.texture.first_layer;
		internal_desc.Texture2DMSArray.ArraySize = desc.texture.layers;
		break;
	case resource_view_type::texture_3d:
		internal_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		internal_desc.Texture3D.MostDetailedMip = desc.texture.first_level;
		internal_desc.Texture3D.MipLevels = desc.texture.levels;
		break;
	case resource_view_type::texture_cube:
		internal_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
		internal_desc.TextureCube.MostDetailedMip = desc.texture.first_level;
		internal_desc.TextureCube.MipLevels = desc.texture.levels;
		break;
	case resource_view_type::texture_cube_array:
		internal_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
		internal_desc.TextureCubeArray.MostDetailedMip = desc.texture.first_level;
		internal_desc.TextureCubeArray.MipLevels = desc.texture.levels;
		internal_desc.TextureCubeArray.First2DArrayFace = desc.texture.first_layer;
		if (desc.texture.layers == 0xFFFFFFFF)
			internal_desc.TextureCubeArray.NumCubes = 0xFFFFFFFF;
		else
			internal_desc.TextureCubeArray.NumCubes = desc.texture.layers / 6;
		break;
	}
}
void reshade::d3d11::convert_resource_view_desc(const resource_view_desc &desc, D3D11_SHADER_RESOURCE_VIEW_DESC1 &internal_desc)
{
	if (desc.type == resource_view_type::texture_2d || desc.type == resource_view_type::texture_2d_array)
	{
		internal_desc.Format = static_cast<DXGI_FORMAT>(desc.format);
		switch (desc.type)
		{
		case resource_view_type::texture_2d:
			internal_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			internal_desc.Texture2D.MostDetailedMip = desc.texture.first_level;
			internal_desc.Texture2D.MipLevels = desc.texture.levels;
			// Missing fields: D3D11_TEX2D_SRV1::PlaneSlice
			break;
		case resource_view_type::texture_2d_array:
			internal_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			internal_desc.Texture2DArray.MostDetailedMip = desc.texture.first_level;
			internal_desc.Texture2DArray.MipLevels = desc.texture.levels;
			internal_desc.Texture2DArray.FirstArraySlice = desc.texture.first_layer;
			internal_desc.Texture2DArray.ArraySize = desc.texture.layers;
			break;
			// Missing fields: D3D11_TEX2D_ARRAY_SRV1::PlaneSlice
		}
	}
	else
	{
		convert_resource_view_desc(desc, reinterpret_cast<D3D11_SHADER_RESOURCE_VIEW_DESC &>(internal_desc));
	}
}
void reshade::d3d11::convert_resource_view_desc(const resource_view_desc &desc, D3D11_UNORDERED_ACCESS_VIEW_DESC &internal_desc)
{
	internal_desc.Format = static_cast<DXGI_FORMAT>(desc.format);
	assert(desc.type == resource_view_type::buffer || desc.texture.levels == 1);
	switch (desc.type) // Do not modifiy description in case type is 'resource_view_type::unknown'
	{
	case resource_view_type::buffer:
		internal_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		assert(desc.buffer.offset <= std::numeric_limits<UINT>::max());
		internal_desc.Buffer.FirstElement = static_cast<UINT>(desc.buffer.offset);
		assert(desc.buffer.size <= std::numeric_limits<UINT>::max());
		internal_desc.Buffer.NumElements = static_cast<UINT>(desc.buffer.size);
		break;
	case resource_view_type::texture_1d:
		internal_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1D;
		internal_desc.Texture1D.MipSlice = desc.texture.first_level;
		break;
	case resource_view_type::texture_1d_array:
		internal_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1DARRAY;
		internal_desc.Texture1DArray.MipSlice = desc.texture.first_level;
		internal_desc.Texture1DArray.FirstArraySlice = desc.texture.first_layer;
		internal_desc.Texture1DArray.ArraySize = desc.texture.layers;
		break;
	case resource_view_type::texture_2d:
		internal_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		internal_desc.Texture2D.MipSlice = desc.texture.first_level;
		break;
	case resource_view_type::texture_2d_array:
		internal_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		internal_desc.Texture2DArray.MipSlice = desc.texture.first_level;
		internal_desc.Texture2DArray.FirstArraySlice = desc.texture.first_layer;
		internal_desc.Texture2DArray.ArraySize = desc.texture.layers;
		break;
	case resource_view_type::texture_3d:
		internal_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
		internal_desc.Texture3D.MipSlice = desc.texture.first_level;
		internal_desc.Texture3D.FirstWSlice = desc.texture.first_layer;
		internal_desc.Texture3D.WSize = desc.texture.layers;
		break;
	}
}
void reshade::d3d11::convert_resource_view_desc(const resource_view_desc &desc, D3D11_UNORDERED_ACCESS_VIEW_DESC1 &internal_desc)
{
	if (desc.type == resource_view_type::texture_2d || desc.type == resource_view_type::texture_2d_array)
	{
		internal_desc.Format = static_cast<DXGI_FORMAT>(desc.format);
		assert(desc.type == resource_view_type::buffer || desc.texture.levels == 1);
		switch (desc.type)
		{
		case resource_view_type::texture_2d:
			internal_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			internal_desc.Texture2D.MipSlice = desc.texture.first_level;
			// Missing fields: D3D11_TEX2D_UAV1::PlaneSlice
			break;
		case resource_view_type::texture_2d_array:
			internal_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			internal_desc.Texture2DArray.MipSlice = desc.texture.first_level;
			internal_desc.Texture2DArray.FirstArraySlice = desc.texture.first_layer;
			internal_desc.Texture2DArray.ArraySize = desc.texture.layers;
			// Missing fields: D3D11_TEX2D_ARRAY_UAV1::PlaneSlice
			break;
		}
	}
	else
	{
		convert_resource_view_desc(desc, reinterpret_cast<D3D11_UNORDERED_ACCESS_VIEW_DESC &>(internal_desc));
	}
}
resource_view_desc reshade::d3d11::convert_resource_view_desc(const D3D11_DEPTH_STENCIL_VIEW_DESC &internal_desc)
{
	// Missing fields: D3D11_DEPTH_STENCIL_VIEW_DESC::Flags
	resource_view_desc desc = {};
	desc.format = static_cast<format>(internal_desc.Format);
	desc.texture.levels = 1;
	switch (internal_desc.ViewDimension)
	{
	case D3D11_DSV_DIMENSION_TEXTURE1D:
		desc.type = resource_view_type::texture_1d;
		desc.texture.first_level = internal_desc.Texture1D.MipSlice;
		break;
	case D3D11_DSV_DIMENSION_TEXTURE1DARRAY:
		desc.type = resource_view_type::texture_1d_array;
		desc.texture.first_level = internal_desc.Texture1DArray.MipSlice;
		desc.texture.first_layer = internal_desc.Texture1DArray.FirstArraySlice;
		desc.texture.layers = internal_desc.Texture1DArray.ArraySize;
		break;
	case D3D11_DSV_DIMENSION_TEXTURE2D:
		desc.type = resource_view_type::texture_2d;
		desc.texture.first_level = internal_desc.Texture2D.MipSlice;
		break;
	case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
		desc.type = resource_view_type::texture_2d_array;
		desc.texture.first_level = internal_desc.Texture2DArray.MipSlice;
		desc.texture.first_layer = internal_desc.Texture2DArray.FirstArraySlice;
		desc.texture.layers = internal_desc.Texture2DArray.ArraySize;
		break;
	case D3D11_DSV_DIMENSION_TEXTURE2DMS:
		desc.type = resource_view_type::texture_2d_multisample;
		break;
	case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
		desc.type = resource_view_type::texture_2d_multisample_array;
		desc.texture.first_layer = internal_desc.Texture2DMSArray.FirstArraySlice;
		desc.texture.layers = internal_desc.Texture2DMSArray.ArraySize;
		break;
	}
	return desc;
}
resource_view_desc reshade::d3d11::convert_resource_view_desc(const D3D11_RENDER_TARGET_VIEW_DESC &internal_desc)
{
	resource_view_desc desc = {};
	desc.format = static_cast<format>(internal_desc.Format);
	desc.texture.levels = 1;
	switch (internal_desc.ViewDimension)
	{
	case D3D11_RTV_DIMENSION_TEXTURE1D:
		desc.type = resource_view_type::texture_1d;
		desc.texture.first_level = internal_desc.Texture1D.MipSlice;
		break;
	case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
		desc.type = resource_view_type::texture_1d_array;
		desc.texture.first_level = internal_desc.Texture1DArray.MipSlice;
		desc.texture.first_layer = internal_desc.Texture1DArray.FirstArraySlice;
		desc.texture.layers = internal_desc.Texture1DArray.ArraySize;
		break;
	case D3D11_RTV_DIMENSION_TEXTURE2D:
		desc.type = resource_view_type::texture_2d;
		desc.texture.first_level = internal_desc.Texture2D.MipSlice;
		break;
	case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
		desc.type = resource_view_type::texture_2d_array;
		desc.texture.first_level = internal_desc.Texture2DArray.MipSlice;
		desc.texture.first_layer = internal_desc.Texture2DArray.FirstArraySlice;
		desc.texture.layers = internal_desc.Texture2DArray.ArraySize;
		break;
	case D3D11_RTV_DIMENSION_TEXTURE2DMS:
		desc.type = resource_view_type::texture_2d_multisample;
		break;
	case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
		desc.type = resource_view_type::texture_2d_multisample_array;
		desc.texture.first_layer = internal_desc.Texture2DMSArray.FirstArraySlice;
		desc.texture.layers = internal_desc.Texture2DMSArray.ArraySize;
		break;
	case D3D11_RTV_DIMENSION_TEXTURE3D:
		desc.type = resource_view_type::texture_3d;
		desc.texture.first_level = internal_desc.Texture3D.MipSlice;
		desc.texture.first_layer = internal_desc.Texture3D.FirstWSlice;
		desc.texture.layers = internal_desc.Texture3D.WSize;
		break;
	}
	return desc;
}
resource_view_desc reshade::d3d11::convert_resource_view_desc(const D3D11_RENDER_TARGET_VIEW_DESC1 &internal_desc)
{
	if (internal_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D || internal_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY)
	{
		resource_view_desc desc = {};
		desc.format = static_cast<format>(internal_desc.Format);
		desc.texture.levels = 1;
		switch (internal_desc.ViewDimension)
		{
		case D3D11_RTV_DIMENSION_TEXTURE2D:
			desc.type = resource_view_type::texture_2d;
			desc.texture.first_level = internal_desc.Texture2D.MipSlice;
			// Missing fields: D3D11_TEX2D_RTV1::PlaneSlice
			break;
		case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
			desc.type = resource_view_type::texture_2d_array;
			desc.texture.first_level = internal_desc.Texture2DArray.MipSlice;
			desc.texture.first_layer = internal_desc.Texture2DArray.FirstArraySlice;
			desc.texture.layers = internal_desc.Texture2DArray.ArraySize;
			// Missing fields: D3D11_TEX2D_ARRAY_RTV1::PlaneSlice
			break;
		}
		return desc;
	}
	else
	{
		return convert_resource_view_desc(reinterpret_cast<const D3D11_RENDER_TARGET_VIEW_DESC &>(internal_desc));
	}
}
resource_view_desc reshade::d3d11::convert_resource_view_desc(const D3D11_SHADER_RESOURCE_VIEW_DESC &internal_desc)
{
	resource_view_desc desc = {};
	desc.format = static_cast<format>(internal_desc.Format);
	switch (internal_desc.ViewDimension)
	{
	case D3D11_SRV_DIMENSION_BUFFER:
		desc.type = resource_view_type::buffer;
		desc.buffer.offset = internal_desc.Buffer.FirstElement;
		desc.buffer.size = internal_desc.Buffer.NumElements;
		break;
	case D3D11_SRV_DIMENSION_TEXTURE1D:
		desc.type = resource_view_type::texture_1d;
		desc.texture.first_level = internal_desc.Texture1D.MostDetailedMip;
		desc.texture.levels = internal_desc.Texture1D.MipLevels;
		break;
	case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
		desc.type = resource_view_type::texture_1d_array;
		desc.texture.first_level = internal_desc.Texture1DArray.MostDetailedMip;
		desc.texture.levels = internal_desc.Texture1DArray.MipLevels;
		desc.texture.first_layer = internal_desc.Texture1DArray.FirstArraySlice;
		desc.texture.layers = internal_desc.Texture1DArray.ArraySize;
		break;
	case D3D11_SRV_DIMENSION_TEXTURE2D:
		desc.type = resource_view_type::texture_2d;
		desc.texture.first_level = internal_desc.Texture2D.MostDetailedMip;
		desc.texture.levels = internal_desc.Texture2D.MipLevels;
		break;
	case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
		desc.type = resource_view_type::texture_2d_array;
		desc.texture.first_level = internal_desc.Texture2DArray.MostDetailedMip;
		desc.texture.levels = internal_desc.Texture2DArray.MipLevels;
		desc.texture.first_layer = internal_desc.Texture2DArray.FirstArraySlice;
		desc.texture.layers = internal_desc.Texture2DArray.ArraySize;
		break;
	case D3D11_SRV_DIMENSION_TEXTURE2DMS:
		desc.type = resource_view_type::texture_2d_multisample;
		break;
	case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
		desc.type = resource_view_type::texture_2d_multisample_array;
		desc.texture.first_layer = internal_desc.Texture2DMSArray.FirstArraySlice;
		desc.texture.layers = internal_desc.Texture2DMSArray.ArraySize;
		break;
	case D3D11_SRV_DIMENSION_TEXTURE3D:
		desc.type = resource_view_type::texture_3d;
		desc.texture.first_level = internal_desc.Texture3D.MostDetailedMip;
		desc.texture.levels = internal_desc.Texture3D.MipLevels;
		break;
	case D3D11_SRV_DIMENSION_TEXTURECUBE:
		desc.type = resource_view_type::texture_cube;
		desc.texture.first_level = internal_desc.TextureCube.MostDetailedMip;
		desc.texture.levels = internal_desc.TextureCube.MipLevels;
		break;
	case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
		desc.type = resource_view_type::texture_cube_array;
		desc.texture.first_level = internal_desc.TextureCubeArray.MostDetailedMip;
		desc.texture.levels = internal_desc.TextureCubeArray.MipLevels;
		desc.texture.first_layer = internal_desc.TextureCubeArray.First2DArrayFace;
		if (internal_desc.TextureCubeArray.NumCubes == 0xFFFFFFFF)
			desc.texture.layers = 0xFFFFFFFF;
		else
			desc.texture.layers = internal_desc.TextureCubeArray.NumCubes * 6;
		break;
	case D3D11_SRV_DIMENSION_BUFFEREX:
		// Do not set type to 'resource_view_type::buffer', since that would translate to D3D11_SRV_DIMENSION_BUFFER on the conversion back
		desc.buffer.offset = internal_desc.BufferEx.FirstElement;
		desc.buffer.size = internal_desc.BufferEx.NumElements;
		// Missing fields: D3D11_BUFFEREX_SRV::Flags
		break;
	}
	return desc;
}
resource_view_desc reshade::d3d11::convert_resource_view_desc(const D3D11_SHADER_RESOURCE_VIEW_DESC1 &internal_desc)
{
	if (internal_desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D || internal_desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DARRAY)
	{
		resource_view_desc desc = {};
		desc.format = static_cast<format>(internal_desc.Format);
		switch (internal_desc.ViewDimension)
		{
		case D3D11_SRV_DIMENSION_TEXTURE2D:
			desc.type = resource_view_type::texture_2d;
			desc.texture.first_level = internal_desc.Texture2D.MostDetailedMip;
			desc.texture.levels = internal_desc.Texture2D.MipLevels;
			// Missing fields: D3D11_TEX2D_SRV1::PlaneSlice
			break;
		case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
			desc.type = resource_view_type::texture_2d_array;
			desc.texture.first_level = internal_desc.Texture2DArray.MostDetailedMip;
			desc.texture.levels = internal_desc.Texture2DArray.MipLevels;
			desc.texture.first_layer = internal_desc.Texture2DArray.FirstArraySlice;
			desc.texture.layers = internal_desc.Texture2DArray.ArraySize;
			// Missing fields: D3D11_TEX2D_ARRAY_SRV1::PlaneSlice
			break;
		}
		return desc;
	}
	else
	{
		return convert_resource_view_desc(reinterpret_cast<const D3D11_SHADER_RESOURCE_VIEW_DESC &>(internal_desc));
	}
}
resource_view_desc reshade::d3d11::convert_resource_view_desc(const D3D11_UNORDERED_ACCESS_VIEW_DESC &internal_desc)
{
	resource_view_desc desc = {};
	desc.format = static_cast<format>(internal_desc.Format);
	desc.texture.levels = 1;
	switch (internal_desc.ViewDimension)
	{
	case D3D11_UAV_DIMENSION_BUFFER:
		desc.type = resource_view_type::buffer;
		desc.buffer.offset = internal_desc.Buffer.FirstElement;
		desc.buffer.size = internal_desc.Buffer.NumElements;
		// Missing fields: D3D11_BUFFER_UAV::Flags
		break;
	case D3D11_UAV_DIMENSION_TEXTURE1D:
		desc.type = resource_view_type::texture_1d;
		desc.texture.first_level = internal_desc.Texture1D.MipSlice;
		break;
	case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
		desc.type = resource_view_type::texture_1d_array;
		desc.texture.first_level = internal_desc.Texture1DArray.MipSlice;
		desc.texture.first_layer = internal_desc.Texture1DArray.FirstArraySlice;
		desc.texture.layers = internal_desc.Texture1DArray.ArraySize;
		break;
	case D3D11_UAV_DIMENSION_TEXTURE2D:
		desc.type = resource_view_type::texture_2d;
		desc.texture.first_level = internal_desc.Texture2D.MipSlice;
		break;
	case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
		desc.type = resource_view_type::texture_2d_array;
		desc.texture.first_level = internal_desc.Texture2DArray.MipSlice;
		desc.texture.first_layer = internal_desc.Texture2DArray.FirstArraySlice;
		desc.texture.layers = internal_desc.Texture2DArray.ArraySize;
		break;
	case D3D11_UAV_DIMENSION_TEXTURE3D:
		desc.type = resource_view_type::texture_3d;
		desc.texture.first_level = internal_desc.Texture3D.MipSlice;
		desc.texture.first_layer = internal_desc.Texture3D.FirstWSlice;
		desc.texture.layers = internal_desc.Texture3D.WSize;
		break;
	}
	return desc;
}
resource_view_desc reshade::d3d11::convert_resource_view_desc(const D3D11_UNORDERED_ACCESS_VIEW_DESC1 &internal_desc)
{
	if (internal_desc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2D || internal_desc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2DARRAY)
	{
		resource_view_desc desc = {};
		desc.format = static_cast<format>(internal_desc.Format);
		desc.texture.levels = 1;
		switch (internal_desc.ViewDimension)
		{
		case D3D11_UAV_DIMENSION_TEXTURE2D:
			desc.type = resource_view_type::texture_2d;
			desc.texture.first_level = internal_desc.Texture2D.MipSlice;
			// Missing fields: D3D11_TEX2D_UAV1::PlaneSlice
			break;
		case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
			desc.type = resource_view_type::texture_2d_array;
			desc.texture.first_level = internal_desc.Texture2DArray.MipSlice;
			desc.texture.first_layer = internal_desc.Texture2DArray.FirstArraySlice;
			desc.texture.layers = internal_desc.Texture2DArray.ArraySize;
			// Missing fields: D3D11_TEX2D_ARRAY_UAV1::PlaneSlice
			break;
		}
		return desc;
	}
	else
	{
		return convert_resource_view_desc(reinterpret_cast<const D3D11_UNORDERED_ACCESS_VIEW_DESC &>(internal_desc));
	}
}