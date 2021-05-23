
#include "stdafx.h"
#include "D2DView.h"
#include "ContainerApp.h"
#include "Engine.h"
#include <fmt/format.h>
#include "scheme.h"
#include <dwrite.h>
#include "Utility.h"
#include <wincodec.h>
#include <wrl.h>
#include <wil/com.h>
 
using namespace Microsoft::WRL;

typedef const void* const_ptr;

#define CALL0(who) Scall0(Stop_level_value(Sstring_to_symbol(who)))
#define CALL1(who, arg) Scall1(Stop_level_value(Sstring_to_symbol(who)), arg)
#define CALL2(who, arg, arg2) Scall2(Stop_level_value(Sstring_to_symbol(who)), arg, arg2)

HWND main_window = nullptr;
HANDLE g_image_rotation_mutex=nullptr;
HANDLE g_sprite_commands_mutex = nullptr;

float prefer_width = 800.0f;
float prefer_height = 600.0f;

// represents the visible surface on the window itself
ID2D1HwndRenderTarget* window_render_target;

// stoke width 
float d2d_stroke_width = static_cast<float>(1.3);
ID2D1StrokeStyle* d2d_stroke_style = nullptr;

// colours and brushes used when drawing
ID2D1SolidColorBrush* line_colour_brush = nullptr;  // line-color
ID2D1SolidColorBrush* fill_colour_brush = nullptr;  // fill-color
ID2D1BitmapBrush* brush_pattern = nullptr;          // brush-pattern
ID2D1BitmapBrush* tile_brush = nullptr;				// tile its U/S
ID2D1LinearGradientBrush* linear_brush = nullptr;	// linear brush
ID2D1RadialGradientBrush* radial_brush = nullptr;	// gradient brush

const auto brush_size = 512;

ID2D1SolidColorBrush* line_brushes[brush_size];
ID2D1SolidColorBrush* fill_brushes[brush_size];
ID2D1LinearGradientBrush* linear_brushes[brush_size];
ID2D1RadialGradientBrush* radial_brushes[brush_size];
 


// double buffers
// this is normally pointed at bitmap 
ID2D1RenderTarget* active_render_target = nullptr;

// bitmap and bitmap2 are swapped 
// functions normally draw on this
ID2D1Bitmap* bitmap = nullptr;
ID2D1BitmapRenderTarget* bitmap_render_target = nullptr;

// this is normally what is being displayed
ID2D1Bitmap* bitmap2 = nullptr;
ID2D1BitmapRenderTarget* bitmap_render_target2 = nullptr;


const auto bank_size = 2048;
// images (sprites) bank
ID2D1Bitmap* sprite_sheets[bank_size];

struct sprite_att {
	int width;
	int height;
};

sprite_att sprite_attributes[bank_size];


ID2D1Factory* direct_2d_factory;

// hiDPI
float g_DPIScaleX = 1.0f;
float g_DPIScaleY = 1.0f;
float graphics_pen_width = static_cast<float>(1.2);

// fill 
float fill_r = 0.0f;
float fill_g = 0.0f;
float fill_b = 0.0f;
float fill_a = 0.0f;

// line
float line_r = 0.0f;
float line_g = 0.0f;
float line_b = 0.0f;
float line_a = 0.0f;

ptr d2d_image_size(int w, int h);

#pragma warning(disable : 4996)

void d2d_dpi_scale(ID2D1Factory* factory)
{
	FLOAT dpi_x, dpi_y;
	factory->GetDesktopDpi(&dpi_x, &dpi_y);
	g_DPIScaleX = dpi_x / 96.0f;
	g_DPIScaleY = dpi_y / 96.0f;
}

void d2d_set_main_window(const HWND w) {
	main_window = w;
}

ptr d2d_line_brush(const int n, const float r, const float g, const float b, const float a);
ptr d2d_color(const float r, const float g, const float b, const float a) {

	line_r = r; line_g = g; line_b = b; line_a = a;
	d2d_line_brush(0, r, g, b, a);
	line_colour_brush = line_brushes[0];
	return Strue;
}

ptr d2d_select_line(const int n) {
	if (n > brush_size - 1) {
		return Sfalse;
	}
	if (line_brushes[n] == nullptr) {
		d2d_color(0.0, 0.0, 0.0, 1.0);
		return Sfalse;
	}
	line_colour_brush = line_brushes[n];
	return Strue;
}

ptr d2d_line_brush(const int n, const float r, const float g, const float b, const float a) {

	if (n > brush_size - 1) {
		return Sfalse;
	}
	if (window_render_target == nullptr)
	{
		return Snil;
	}
	SafeRelease(&line_brushes[n]);
	HRESULT hr = window_render_target->CreateSolidColorBrush(
		D2D1::ColorF(D2D1::ColorF(r, g, b, a)),
		&line_brushes[n]
	);
	return Strue;
}

ptr d2d_select_color(const int n) {
	if (n > brush_size - 1) {
		return Sfalse;
	}
	line_colour_brush = line_brushes[n];
	return Strue;
}

ptr d2d_fill_brush(const int n,const float r, const float g, const float b, const float a) {

	if (n > brush_size - 1) {
		return Sfalse;
	}
	if (window_render_target == nullptr)
	{
		return Snil;
	}
	SafeRelease(&fill_brushes[n]);
	HRESULT hr = window_render_target->CreateSolidColorBrush(
		D2D1::ColorF(D2D1::ColorF(r, g, b, a)),
		&fill_brushes[n]
	);
	return Strue;
}

ptr d2d_fill_color(const float r, const float g, const float b, const float a) {

	fill_r = r; fill_g = g; fill_b = b; fill_a = a;
	d2d_fill_brush(0, r, g, b, a);
	fill_colour_brush = fill_brushes[0];
	return Strue;
}

ptr d2d_select_fill_color(const int n) {
	if (n > brush_size - 1) {
		return Sfalse;
	}
	fill_colour_brush = fill_brushes[n];
	return Strue;
}

ptr d2d_linear_gradient_brush(const int n, ptr l) {

	if (n > brush_size - 1) {
		return Sfalse;
	}

	if (window_render_target == nullptr)
	{
		return Snil;
	}

	if (l == Snil) {
		return Snil;
	}

	Slock_object(l);
	ptr item = Snil;
	const auto len = Sfixnum_value(CALL1("length", l));
	D2D1_GRADIENT_STOP stops[8];

	if (len < 2) {
		Sunlock_object(l);
		return Snil;
	}

	for (int i = 0; i < len; i++) {

		float p = 0.0f;
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 0.0f;

		stops[i].position = 0.0f;
		stops[i].color.a = 0.0f;
		stops[i].color.r = 0.0f;
		stops[i].color.g = 0.0f;
		stops[i].color.b = 0.0f;

		item = Scar(l);
		l = Scdr(l);

		p = Sflonum_value(Scar(item)); item = Scdr(item);
		r = Sflonum_value(Scar(item)); item = Scdr(item);
		g = Sflonum_value(Scar(item)); item = Scdr(item);
		b = Sflonum_value(Scar(item)); item = Scdr(item);
		a = Sflonum_value(Scar(item)); item = Scdr(item);

		stops[i].position = p;
		stops[i].color.r = r;
		stops[i].color.g = g;
		stops[i].color.b = b;
		stops[i].color.a = a;

	}

	ID2D1GradientStopCollection* pGradientStops = nullptr;
	
	HRESULT hr = window_render_target->CreateGradientStopCollection(
		stops,
		static_cast<UINT32>(len),
		D2D1_GAMMA_2_2,
		D2D1_EXTEND_MODE_CLAMP,
		&pGradientStops
	);

	const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& linearGradientBrushProperties = {};

	SafeRelease(&linear_brushes[n]);

	if (pGradientStops != nullptr) {

		hr = window_render_target->CreateLinearGradientBrush(
			linearGradientBrushProperties,
			pGradientStops,
			&linear_brushes[n]
		);
	}
	Sunlock_object(l);
	return Strue;
}

ptr d2d_linear_gradient_color_list(ptr l) {
	d2d_linear_gradient_brush(0, l);
	linear_brush = linear_brushes[0];
	return Strue;
}

ptr d2d_select_linear_brush(const int n) {
	if (n > brush_size - 1) {
		return Sfalse;
	}
	linear_brush = linear_brushes[n];
	return Strue;
}


ptr d2d_radial_gradient_brush(const int n, ptr l) {

	if (n > brush_size - 1) {
		return Sfalse;
	}

	if (window_render_target == nullptr)
	{
		return Snil;
	}

	if (l == Snil) {
		return Snil;
	}

	ptr item = Snil;
	const auto len = Sfixnum_value(CALL1("length", l));
	D2D1_GRADIENT_STOP stops[8];
	Slock_object(l);

	if (len < 2) {
		Sunlock_object(l);
		return Snil;
	}

	for (int i = 0; i < len; i++) {

		float p = 0.0f;
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 0.0f;

		stops[i].position = 0.0f;
		stops[i].color.a = 0.0f;
		stops[i].color.r = 0.0f;
		stops[i].color.g = 0.0f;
		stops[i].color.b = 0.0f;

		item = Scar(l);
		l = Scdr(l);

		p = Sflonum_value(Scar(item)); item = Scdr(item);
		r = Sflonum_value(Scar(item)); item = Scdr(item);
		g = Sflonum_value(Scar(item)); item = Scdr(item);
		b = Sflonum_value(Scar(item)); item = Scdr(item);
		a = Sflonum_value(Scar(item)); item = Scdr(item);

		stops[i].position = p;
		stops[i].color.r = r;
		stops[i].color.g = g;
		stops[i].color.b = b;
		stops[i].color.a = a;

	}

	ID2D1GradientStopCollection* pGradientStops = nullptr;
	HRESULT hr = window_render_target->CreateGradientStopCollection(
		stops,
		static_cast<UINT32>(len),
		D2D1_GAMMA_2_2,
		D2D1_EXTEND_MODE_CLAMP,
		&pGradientStops
	);

	const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& radialGradientBrushProperties = {};

	SafeRelease(&radial_brushes[n]);
	if (pGradientStops != nullptr) {
		hr = window_render_target->CreateRadialGradientBrush(
			radialGradientBrushProperties,
			pGradientStops,
			&radial_brushes[n]
		);
	}
	Sunlock_object(l);
	return Strue;
}

ptr d2d_radial_gradient_color_list(ptr l) {
	d2d_radial_gradient_brush(0, l);
	radial_brush = radial_brushes[0];
	return Strue;
}

ptr d2d_select_radial_brush(const int n) {
	if (n > brush_size - 1) {
		return Sfalse;
	}
	radial_brush = radial_brushes[n];
	return Strue;
}


void CheckFillBrush()
{
	if (fill_colour_brush == nullptr) {
		d2d_fill_color(0.0, 0.0, 0.0, 1.0);
	}
}

void CheckLineBrush()
{
	if (line_colour_brush == nullptr) {
		d2d_color(0.0, 0.0, 0.0, 1.0);
	}
}

void d2d_CreateOffscreenBitmap()
{
	if (window_render_target == nullptr)
	{
		return;
	}

	if (bitmap_render_target == nullptr) {
		window_render_target->CreateCompatibleRenderTarget(D2D1::SizeF(prefer_width, prefer_height), &bitmap_render_target);
		bitmap_render_target->GetBitmap(&bitmap);
	}
	if (bitmap_render_target2 == nullptr) {
		window_render_target->CreateCompatibleRenderTarget(D2D1::SizeF(prefer_width, prefer_height), &bitmap_render_target2);
		bitmap_render_target2->GetBitmap(&bitmap2);
	}

	active_render_target = bitmap_render_target;
}

ptr sprite_size(const int n) {
	if (n > bank_size - 1) {
		return Sfalse;
	}
	ptr l = Snil;
	l = CALL2("cons", Sflonum(static_cast<float>(sprite_attributes[n].height)), l);
	l = CALL2("cons", Sflonum(static_cast<float>(sprite_attributes[n].width)), l);
	return l;
}

ptr d2d_FreeAllSprites() {

	for (int n = 0; n < bank_size - 1; n++) {
		SafeRelease(&sprite_sheets[n]);
		sprite_attributes[n].width = 0;
		sprite_attributes[n].height = 0;
	}
	return Strue;
}

ptr d2d_FreeSpriteInBank(const int n) {
	if (n > bank_size - 1) {
		return Sfalse;
	}
	SafeRelease(&sprite_sheets[n]);
	sprite_attributes[n].width = 0;
	sprite_attributes[n].height = 0;
	return Strue;
}

ptr d2d_MakeSpriteInBank(const int n, const int w, const int h, ptr f)
{
	ID2D1BitmapRenderTarget* bitmap_render2_bank = nullptr;
	if (n > bank_size - 1) {
		return Sfalse;
	}
	SafeRelease(&sprite_sheets[n]);
	if (window_render_target == nullptr)
	{
		return Sfalse;
	}
	ID2D1RenderTarget* old_render_target = active_render_target;
	if (bitmap_render2_bank == nullptr) {
		window_render_target->CreateCompatibleRenderTarget(D2D1::SizeF(w, h), &bitmap_render2_bank);
		bitmap_render2_bank->GetBitmap(&sprite_sheets[n]);
	}
	if (bitmap_render2_bank == nullptr) {
		active_render_target = old_render_target;
		return Sfalse;
	}
	active_render_target = bitmap_render2_bank;
 
	active_render_target->BeginDraw();
	ptr result = Engine::RunNaked(f);
	const HRESULT hr=active_render_target->EndDraw();
	active_render_target = old_render_target;
	bitmap_render2_bank->Release();
 
	if (SUCCEEDED(hr) && sprite_sheets[n] != nullptr) {
		const auto size = sprite_sheets[n]->GetPixelSize();
		sprite_attributes[n].width = size.width;
		sprite_attributes[n].height = size.height;
	}
	else {
		sprite_sheets[n] = nullptr;
		sprite_attributes[n].width = 0;
		sprite_attributes[n].height = 0;
		return Sfalse;
	}
	return Strue;
}

void swap_buffers(const int n) {

	if (window_render_target == nullptr) {
		return;
	}

	d2d_CreateOffscreenBitmap();
	if (n == 1 || n == 3) {
		bitmap_render_target2->BeginDraw();
		bitmap_render_target2->DrawBitmap(bitmap, D2D1::RectF(0.0f, 0.0f, prefer_width, prefer_height));
		bitmap_render_target2->EndDraw();
	}
	ID2D1Bitmap* temp = bitmap;
	bitmap = bitmap2;
	bitmap2 = temp;
	ID2D1BitmapRenderTarget* temp_target = bitmap_render_target;
	bitmap_render_target = bitmap_render_target2;
	bitmap_render_target2 = temp_target;
	 
	active_render_target = bitmap_render_target;
	if (main_window != nullptr) {

		InvalidateRect(main_window, nullptr, FALSE);
	}
	Sleep(1);
}

ptr d2d_zclear(const  float r, const float g, const float b, const float a) {
	active_render_target->Clear((D2D1::ColorF(r, g, b, a)));
	return Strue;
}

ptr d2d_clear(const float r, const float g, const float b, const float a) {

	if (window_render_target == nullptr || active_render_target ==nullptr) {
		return Sfalse;
	}
	WaitForSingleObject(g_image_rotation_mutex, INFINITE);
	active_render_target->BeginDraw();
	active_render_target->Clear((D2D1::ColorF(r, g, b, a)));
	active_render_target->EndDraw();
	active_render_target->BeginDraw();
	active_render_target->Clear((D2D1::ColorF(r, g, b, a)));
	active_render_target->EndDraw();
	ReleaseMutex(g_image_rotation_mutex);
	return Strue;
}

// cannot save hw rendered bitmap to image :( 
ptr d2d_save(char* filename) {
	return Snil;
}

void render_sprite_commands();
ptr d2d_show(const int n)
{
	if (window_render_target == nullptr || active_render_target == nullptr) {
		d2d_image_size(static_cast<int>(prefer_width), static_cast<int>(prefer_height));
	}
	if (n == 2) render_sprite_commands();  
	swap_buffers(n);
	if (main_window != nullptr)
		PostMessageW(main_window, WM_USER + 501, static_cast<WPARAM>(0), static_cast<LPARAM>(0));
	return Strue;
}

ptr d2d_set_stroke_width(const float w) {
	d2d_stroke_width = w;
	return Strue;
}

// unable to access these new D2D features.
ptr d2d_DrawSpriteBatch() {

	return Strue;
}

ptr d2d_zfill_ellipse(const float x, const float y, const float w, const float h) {
	const auto ellipse = D2D1::Ellipse(D2D1::Point2F(x, y), w, h);
	active_render_target->FillEllipse(ellipse, fill_colour_brush);
	return Strue;
}

ptr d2d_fill_ellipse(const float x, const float y, const float w, const float h) {

	if (window_render_target == nullptr 
		|| active_render_target == nullptr
		|| fill_colour_brush == nullptr) {
		return Sfalse;
	}

	const auto ellipse = D2D1::Ellipse(D2D1::Point2F(x, y), w, h);
	active_render_target->BeginDraw();
	active_render_target->FillEllipse(ellipse, fill_colour_brush);
	active_render_target->EndDraw();
	return Strue;
}

ptr d2d_zellipse(const float x, const  float y, const  float w, const float h) {
	auto stroke_width = d2d_stroke_width;
	auto stroke_style = d2d_stroke_style;
	const auto ellipse = D2D1::Ellipse(D2D1::Point2F(x, y), w, h);
	active_render_target->DrawEllipse(ellipse, line_colour_brush, d2d_stroke_width);
	return Strue;
}

ptr d2d_ellipse(const float x, const  float y, const  float w, const float h) {

	if (window_render_target == nullptr
		|| active_render_target == nullptr
		|| line_colour_brush == nullptr) {
		return Sfalse;
	}

	const auto stroke_width = d2d_stroke_width;
	auto stroke_style = d2d_stroke_style;
	const auto ellipse = D2D1::Ellipse(D2D1::Point2F(x, y), w, h);
	active_render_target->BeginDraw();
	active_render_target->DrawEllipse(ellipse, line_colour_brush, stroke_width);
	active_render_target->EndDraw();
	return Strue;
}

ptr d2d_zline(const float x, const float y, const float x1, const float y1) {
	const auto stroke_width = d2d_stroke_width;
	const auto stroke_style = d2d_stroke_style;
	const auto p1 = D2D1::Point2F(x, y);
	const auto p2 = D2D1::Point2F(x1, y1);
	active_render_target->DrawLine(p1, p2, line_colour_brush, stroke_width, stroke_style);
	return Strue;
}

ptr d2d_line(const float x, const float y, const float x1, const float y1) {
	if (window_render_target == nullptr 
		|| active_render_target == nullptr
		|| line_colour_brush == nullptr) {
		return Sfalse;
	}

	const auto stroke_width = d2d_stroke_width;
	const auto stroke_style = d2d_stroke_style;
	const auto p1 = D2D1::Point2F(x, y);
	const auto p2 = D2D1::Point2F(x1, y1);
	active_render_target->BeginDraw();
	active_render_target->DrawLine(p1, p2, line_colour_brush, stroke_width, stroke_style);
	active_render_target->EndDraw();
	return Strue;
}

// runs lambda f inside a draw operation
ptr d2d_draw_func(ptr f) {
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	CheckLineBrush();
	CheckFillBrush();
	try {
		active_render_target->BeginDraw();
		const auto result=Engine::RunNaked(f);
		active_render_target->EndDraw();
		return result;
	}
	catch (const CException& e)
	{
		return Sfalse;
	}
}

ptr d2d_zrectangle(const float x, const float y, const float w, const float h) {
	const auto stroke_width = d2d_stroke_width;
	auto stroke_style = d2d_stroke_style;
	D2D1_RECT_F rectangle1 = D2D1::RectF(x, y, w, h);
	active_render_target->DrawRectangle(&rectangle1, line_colour_brush, stroke_width);
	return Strue;
}

ptr d2d_rectangle(const float x, const float y, const float w, const float h) {

	if (window_render_target == nullptr
		|| active_render_target == nullptr
		|| line_colour_brush == nullptr) {
		return Sfalse;
	}

	const auto stroke_width = d2d_stroke_width;
	auto stroke_style = d2d_stroke_style;
	D2D1_RECT_F rectangle1 = D2D1::RectF(x, y, w, h);
	active_render_target->BeginDraw();
	active_render_target->DrawRectangle(&rectangle1, line_colour_brush, stroke_width);
	active_render_target->EndDraw();
	return Strue;
}

ptr d2d_rounded_rectangle(const float x, const float y, const float w, const float h, const float rx, const float ry) {

	if (window_render_target == nullptr
		|| active_render_target == nullptr
		|| fill_colour_brush == nullptr) {
		return Sfalse;
	}
	const auto stroke_width = d2d_stroke_width;
	const auto stroke_style = d2d_stroke_style;
	const D2D1_ROUNDED_RECT rectangle1 = { D2D1::RectF(x, y, w, h), rx, ry };
	active_render_target->BeginDraw();
	active_render_target->DrawRoundedRectangle(rectangle1, fill_colour_brush, stroke_width, stroke_style);
	active_render_target->EndDraw();
	return Strue;
}

ptr d2d_zfill_rectangle(const float x, const float y, const float w, const float h) {
	D2D1_RECT_F rectangle1 = D2D1::RectF(x, y, w, h);
	active_render_target->FillRectangle(&rectangle1, fill_colour_brush);
	return Strue;
}

ptr d2d_fill_rectangle(const float x, const float y, const float w, const float h) {

	if (window_render_target == nullptr || main_window==nullptr
		|| active_render_target == nullptr
		|| fill_colour_brush == nullptr) {
		return Sfalse;
	}
	D2D1_RECT_F rectangle1 = D2D1::RectF(x, y, w, h);
	active_render_target->BeginDraw();
	active_render_target->FillRectangle(&rectangle1, fill_colour_brush);
	active_render_target->EndDraw();
	return Strue;
}

ptr d2d_zlinear_gradient_fill_rectangle(
	const float x, const float y, const float w, const float h,
	const float x1, const float y1, const float x2, const float y2) {
	D2D1_RECT_F rectangle1 = D2D1::RectF(x, y, w, h);
	linear_brush->SetStartPoint(D2D1_POINT_2F({ x1, y1 }));
	linear_brush->SetEndPoint(D2D1_POINT_2F({ x2, y2 }));
	active_render_target->FillRectangle(&rectangle1, linear_brush);
	return Strue;
}

ptr d2d_linear_gradient_fill_rectangle(
	const float x, const float y, const float w, const float h,
	const float x1, const float y1, const float x2, const float y2) {

	if (window_render_target == nullptr || active_render_target == nullptr || linear_brush == nullptr) {
		return Sfalse;
	}
 
	D2D1_RECT_F rectangle1 = D2D1::RectF(x, y, w, h);
	active_render_target->BeginDraw();
	linear_brush->SetStartPoint(D2D1_POINT_2F({ x1, y1 }));
	linear_brush->SetEndPoint(D2D1_POINT_2F({ x2, y2 }));
	active_render_target->FillRectangle(&rectangle1, linear_brush);
	active_render_target->EndDraw();
	return Strue;
}

ptr d2d_zradial_gradient_fill_rectangle(
	const  float x,  const float y,  float w,  float h,
	const float x1, const float y1, const float r1, const float r2) {
	D2D1_RECT_F rectangle1 = D2D1::RectF(x, y, w, h);
	radial_brush->SetCenter(D2D1_POINT_2F({ x1, y1 }));
	radial_brush->SetRadiusX(r1);
	radial_brush->SetRadiusY(r2);
	active_render_target->FillRectangle(&rectangle1, radial_brush);
	return Strue;
}

ptr d2d_radial_gradient_fill_rectangle(
	const float x, const float y, const float w, const float h,
	const float x1, const float y1, const float r1, const float r2) {
	if (window_render_target == nullptr || active_render_target == nullptr || radial_brush == nullptr) {
		return Sfalse;
	}
	D2D1_RECT_F rectangle1 = D2D1::RectF(x, y, w, h);
	active_render_target->BeginDraw();
	radial_brush->SetCenter(D2D1_POINT_2F({ x1, y1 }));
	radial_brush->SetRadiusX(r1);
	radial_brush->SetRadiusY(r2);
	active_render_target->FillRectangle(&rectangle1, radial_brush);
	active_render_target->EndDraw();
	return Strue;
}

ptr d2d_zradial_gradient_fill_ellipse(
	const float x, const float y, const float w, const float h,
	const float x1, const float y1, const float r1, const float r2) {
	const auto ellipse = D2D1::Ellipse(D2D1::Point2F(x, y), w, h);
	radial_brush->SetCenter(D2D1_POINT_2F({ x1, y1 }));
	radial_brush->SetRadiusX(r1);
	radial_brush->SetRadiusY(r2);
	active_render_target->FillEllipse(ellipse, radial_brush);
	return Strue;
}

ptr d2d_radial_gradient_fill_ellipse(
	const float x, const float y, const float w, const float h,
	const float x1, const float y1, const float r1, const float r2) {

	if (window_render_target == nullptr || active_render_target == nullptr || radial_brush == nullptr) {
		return Sfalse;
	}
	const auto ellipse = D2D1::Ellipse(D2D1::Point2F(x, y), w, h);
	active_render_target->BeginDraw();
	radial_brush->SetCenter(D2D1_POINT_2F({ x1, y1 }));
	radial_brush->SetRadiusX(r1);
	radial_brush->SetRadiusY(r2);
	active_render_target->FillEllipse(ellipse, radial_brush);
	active_render_target->EndDraw();
	return Strue;
}

ptr d2d_zlinear_gradient_fill_ellipse(
	const float x, const float y, const float w, const float h,
	const float x1, const float y1, const float x2, const float y2) {
	const auto ellipse = D2D1::Ellipse(D2D1::Point2F(x, y), w, h);
	linear_brush->SetStartPoint(D2D1_POINT_2F({ x1, y1 }));
	linear_brush->SetEndPoint(D2D1_POINT_2F({ x2, y2 }));
	active_render_target->FillEllipse(ellipse, linear_brush);
	return Strue;
}

ptr d2d_linear_gradient_fill_ellipse(
	const float x, const float y, const float w, const float h,
	const float x1, const float y1, const float x2, const float y2) {
	if (window_render_target == nullptr || active_render_target == nullptr || linear_brush == nullptr) {
		return Sfalse;
	}
	const auto ellipse = D2D1::Ellipse(D2D1::Point2F(x, y), w, h);
	active_render_target->BeginDraw();
	linear_brush->SetStartPoint(D2D1_POINT_2F({ x1, y1 }));
	linear_brush->SetEndPoint(D2D1_POINT_2F({ x2, y2 }));
	active_render_target->FillEllipse(ellipse, linear_brush);
	active_render_target->EndDraw();
	return Strue;
}

ptr d2d_zmatrix_identity() {
	active_render_target->SetTransform(D2D1::Matrix3x2F::Identity());
	return Strue;
}

ptr d2d_matrix_identity() {
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	active_render_target->SetTransform(D2D1::Matrix3x2F::Identity());
	return Strue;
}

ptr d2d_matrix_rotate(const float a, const float x, const float y) {
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	active_render_target->SetTransform(
		D2D1::Matrix3x2F::Rotation(a, D2D1::Point2F(x, y)));
	return Strue;
}

ptr d2d_zmatrix_translate(const float x, const float y) {
	active_render_target->SetTransform(
		D2D1::Matrix3x2F::Translation(x, y));
	return Strue;
}

ptr d2d_matrix_translate(const float x, const float y) {
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	active_render_target->SetTransform(
		D2D1::Matrix3x2F::Translation(40, 10));
	return Strue;
}

ptr d2d_zmatrix_rotrans(const float a, const float x, const float y, const float x1, const float y1) {
	const D2D1::Matrix3x2F rot = D2D1::Matrix3x2F::Rotation(a, D2D1::Point2F(x, y));
	const D2D1::Matrix3x2F trans = D2D1::Matrix3x2F::Translation(x1, y1);
	active_render_target->SetTransform(rot * trans);
	return Strue;
}

ptr d2d_matrix_rotrans(const float a, const float x, const float y, const float x1, const float y1) {
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	const D2D1::Matrix3x2F rot = D2D1::Matrix3x2F::Rotation(a, D2D1::Point2F(x, y));
	const D2D1::Matrix3x2F trans = D2D1::Matrix3x2F::Translation(x1, y1);
	active_render_target->SetTransform(rot * trans);
	return Strue;
}

ptr d2d_zmatrix_transrot(const float a, const float x, const float y, const float x1, const float y1) {
	const D2D1::Matrix3x2F rot = D2D1::Matrix3x2F::Rotation(a, D2D1::Point2F(x, y));
	const D2D1::Matrix3x2F trans = D2D1::Matrix3x2F::Translation(x1, y1);
	active_render_target->SetTransform(trans * rot);
	return Strue;
}

ptr d2d_matrix_transrot(const float a, const float x, const float y, const float x1, const float y1) {
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	const D2D1::Matrix3x2F rot = D2D1::Matrix3x2F::Rotation(a, D2D1::Point2F(x, y));
	const D2D1::Matrix3x2F trans = D2D1::Matrix3x2F::Translation(x1, y1);
	active_render_target->SetTransform(trans * rot);
	return Strue;
}

ptr d2d_matrix_scale(const float x, const float y) {
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	active_render_target->SetTransform(
		D2D1::Matrix3x2F::Scale(
			D2D1::Size(x, y),
			D2D1::Point2F(prefer_width / 2, prefer_height / 2))
	);
	return Strue;
}

ptr d2d_matrix_rotscale(const float a, const float x, const float y, const float x1, const float y1) {
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	const auto scale = D2D1::Matrix3x2F::Scale(
		D2D1::Size(x, y),
		D2D1::Point2F(prefer_width / 2, prefer_height / 2));
	const D2D1::Matrix3x2F rot = D2D1::Matrix3x2F::Rotation(a, D2D1::Point2F(x, y));
	active_render_target->SetTransform(rot * scale);
	return Strue;
}

ptr d2d_matrix_scalerot(const float a, const float x, const float y, const float x1, const float y1) {
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	const auto scale = D2D1::Matrix3x2F::Scale(
		D2D1::Size(x, y),
		D2D1::Point2F(prefer_width / 2, prefer_height / 2));
	const D2D1::Matrix3x2F rot = D2D1::Matrix3x2F::Rotation(a, D2D1::Point2F(x, y));
	active_render_target->SetTransform(rot * scale);
	return Strue;
}

ptr d2d_matrix_scalerottrans(const float a, const float x, const float y, const float x1, const float y1, const float x2, const float y2) {
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	const auto scale = D2D1::Matrix3x2F::Scale(
		D2D1::Size(x, y),
		D2D1::Point2F(prefer_width / 2, prefer_height / 2));
	const D2D1::Matrix3x2F rot = D2D1::Matrix3x2F::Rotation(a, D2D1::Point2F(x, y));
	const D2D1::Matrix3x2F trans = D2D1::Matrix3x2F::Translation(x1, y1);
	active_render_target->SetTransform(scale * rot * trans);
	return Strue;
}

ptr d2d_zmatrix_skew(const float x, const float y, const float w, const float h) {
	active_render_target->SetTransform(
		D2D1::Matrix3x2F::Skew(
			x, y,
			D2D1::Point2F(w, h))
	);
	return Strue;
}

ptr d2d_matrix_skew(const float x, const float y) {
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	active_render_target->SetTransform(
		D2D1::Matrix3x2F::Skew(
			x, y,
			D2D1::Point2F(prefer_width / 2, prefer_height / 2))
	);
	return Strue;
}

// display current display buffer.
ptr d2d_render(const float x, const float y) {

	swap_buffers(1);
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	active_render_target->BeginDraw();
	active_render_target->DrawBitmap(bitmap2, D2D1::RectF(x, y, prefer_width, prefer_height));
	active_render_target->EndDraw();
	return Strue;
}

int d2d_CreateGridPatternBrush(
	ID2D1RenderTarget* render_target_ptr,
	ID2D1BitmapBrush** bitmap_brush_ptr
)
{
	if (brush_pattern != nullptr) {
		return 0;
	}
	// Create a compatible render target.
	ID2D1BitmapRenderTarget* pCompatibleRenderTarget = nullptr;
	HRESULT hr = render_target_ptr->CreateCompatibleRenderTarget(
		D2D1::SizeF(10.0f, 10.0f),
		&pCompatibleRenderTarget
	);
	if (SUCCEEDED(hr))
	{
		// Draw a pattern.
		ID2D1SolidColorBrush* pGridBrush = nullptr;
		hr = pCompatibleRenderTarget->CreateSolidColorBrush(
			D2D1::ColorF(D2D1::ColorF(0.93f, 0.94f, 0.96f, 1.0f)),
			&pGridBrush
		);



		if (SUCCEEDED(hr))
		{
			pCompatibleRenderTarget->BeginDraw();
			pCompatibleRenderTarget->FillRectangle(D2D1::RectF(0.0f, 0.0f, 10.0f, 1.0f), pGridBrush);
			pCompatibleRenderTarget->FillRectangle(D2D1::RectF(0.0f, 0.1f, 1.0f, 10.0f), pGridBrush);
			pCompatibleRenderTarget->EndDraw();

			// Retrieve the bitmap from the render target.
			ID2D1Bitmap* pGridBitmap = nullptr;
			hr = pCompatibleRenderTarget->GetBitmap(&pGridBitmap);
			if (SUCCEEDED(hr))
			{
				// Choose the tiling mode for the bitmap brush.
				const D2D1_BITMAP_BRUSH_PROPERTIES brushProperties =
					D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP);

				// Create the bitmap brush.
				hr = render_target_ptr->CreateBitmapBrush(pGridBitmap, brushProperties, bitmap_brush_ptr);

				SafeRelease(&pGridBitmap);
			}

			SafeRelease(&pGridBrush);

		}

		SafeRelease(&pCompatibleRenderTarget);

	}

	return hr;
}

void d2d_make_default_stroke() {
	HRESULT r = direct_2d_factory->CreateStrokeStyle(
		D2D1::StrokeStyleProperties(
			D2D1_CAP_STYLE_FLAT,
			D2D1_CAP_STYLE_FLAT,
			D2D1_CAP_STYLE_ROUND,
			D2D1_LINE_JOIN_MITER,
			1.0f,
			D2D1_DASH_STYLE_SOLID,
			0.0f),
		nullptr, 0,
		&d2d_stroke_style
	);
}

// direct write
IDWriteFactory* direct_write_factory;
IDWriteTextFormat* current_text_font;
ID2D1SolidColorBrush* black_brush;

ptr d2d_text_mode(const int n) {

	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	const auto mode = static_cast<enum D2D1_TEXT_ANTIALIAS_MODE>(n);
	active_render_target->BeginDraw();
	active_render_target->SetTextAntialiasMode(mode);
	active_render_target->EndDraw();
	return Strue;
}

ptr d2d_zwrite_text(const float x, const float y, char* s) {
	const auto text = Utility::widen(s);
	const auto len = text.length();
	const D2D1_RECT_F layout_rect = D2D1::RectF(x, y, prefer_width - x, prefer_height - y);
	active_render_target->DrawTextW(text.c_str(), static_cast<UINT32>(len), current_text_font, layout_rect, fill_colour_brush);
	return Strue;
}

ptr d2d_write_text(const float x, const float y, char* s) {

	if (window_render_target == nullptr
		|| active_render_target == nullptr
		|| fill_colour_brush == nullptr) {
		return Sfalse;
	}
	CheckFillBrush();

	const auto text = Utility::widen(s);
	const auto len = text.length();

	const D2D1_RECT_F layout_rect = D2D1::RectF(x, y, prefer_width - x, prefer_height - y);

	active_render_target->BeginDraw();
	active_render_target->DrawTextW(text.c_str(), len, current_text_font, layout_rect, fill_colour_brush);
	active_render_target->EndDraw();
	return Strue;
}

ptr d2d_set_font(char* s, const float size) {

	SafeRelease(&current_text_font);
	const auto face = Utility::widen(s);
	const HRESULT hr = direct_write_factory->CreateTextFormat(
			face.c_str(),
			nullptr,
			DWRITE_FONT_WEIGHT_REGULAR,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			size,
			L"en-us",
			&current_text_font
		);
	if (SUCCEEDED(hr)) {
		return Strue;
	}
	return Snil;
}

// from file to bank
void d2d_sprite_loader(char* filename, const int n)
{
	if (n > bank_size - 1) {
		return;
	}

	const auto locate_file = Utility::GetFullPathFor(Utility::widen(filename).c_str());
	if ((INVALID_FILE_ATTRIBUTES == GetFileAttributes(locate_file.c_str()) &&
		GetLastError() == ERROR_FILE_NOT_FOUND)) return;

	HRESULT hr = CoInitialize(nullptr);
	if (FAILED(hr)) {
		return;
	}
	IWICImagingFactory* wicFactory = nullptr;
	hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory,
		reinterpret_cast<LPVOID*>(&wicFactory));

	if (FAILED(hr)) {
		return;
	}
	//create a decoder
	IWICBitmapDecoder* wicDecoder = nullptr;
	std::wstring fname = locate_file;
	hr = wicFactory->CreateDecoderFromFilename(
		fname.c_str(),
		nullptr,
		GENERIC_READ,
		WICDecodeMetadataCacheOnLoad,
		&wicDecoder);

	if (wicDecoder == nullptr) {
		return;
	}

	IWICBitmapFrameDecode* wicFrame = nullptr;
	hr = wicDecoder->GetFrame(0, &wicFrame);

	// create a converter
	IWICFormatConverter* wicConverter = nullptr;
	hr = wicFactory->CreateFormatConverter(&wicConverter);

	// setup the converter
	hr = wicConverter->Initialize(
		wicFrame,
		GUID_WICPixelFormat32bppPBGRA,
		WICBitmapDitherTypeNone,
		NULL,
		0.0,
		WICBitmapPaletteTypeCustom
	);
	if (SUCCEEDED(hr))
	{
		hr = window_render_target->CreateBitmapFromWicBitmap(
			wicConverter,
			nullptr,
			&sprite_sheets[n]
		);
		if (SUCCEEDED(hr)) {
			const auto size = sprite_sheets[n]->GetPixelSize();
			sprite_attributes[n].width = size.width;
			sprite_attributes[n].height = size.height;
		}
		else {
			sprite_sheets[n] = nullptr;
			sprite_attributes[n].width = 0;
			sprite_attributes[n].height = 0;
		}
	}
	SafeRelease(&wicFactory);
	SafeRelease(&wicDecoder);
	SafeRelease(&wicConverter);
	SafeRelease(&wicFrame);
}

ptr d2d_load_sprites(char* filename, int n) {
	if (n > bank_size - 1) {
		return Snil;
	}
	d2d_sprite_loader(filename, n);
	return Strue;
}

ptr d2d_zrender_sprite(const int n, const float dx, const float dy) {

	if (n > bank_size - 1) {
		return Snil;
	}

	const auto sprite_sheet = sprite_sheets[n];
	if (sprite_sheet == nullptr) {
		return Snil;
	}
	const auto size = sprite_sheet->GetPixelSize();
	const auto dest = D2D1::RectF(dx, dy, dx + size.width, dy + size.height);
	const auto opacity = 1.0f;
	active_render_target->SetTransform(
		D2D1::Matrix3x2F::Scale(
			D2D1::Size(1.0, 1.0),
			D2D1::Point2F(static_cast<float>(size.width / 2.0), static_cast<float>(size.height / 2.0))));
	active_render_target->DrawBitmap(sprite_sheet, dest);
	active_render_target->SetTransform(D2D1::Matrix3x2F::Identity());
	return Strue;
}

// from sheet n; at sx, sy to dx, dy, w,h
ptr d2d_render_sprite(const int n, const float dx, const float dy) {

	if (n > bank_size - 1) {
		return Snil;
	}
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	const auto sprite_sheet = sprite_sheets[n];
	if (sprite_sheet == nullptr) {
		return Snil;
	}
	const auto size = sprite_sheet->GetPixelSize();
	const auto dest = D2D1::RectF(dx, dy, dx + size.width, dy + size.height);
	const auto opacity = 1.0f;
	active_render_target->SetTransform(
		D2D1::Matrix3x2F::Scale(
			D2D1::Size(1.0, 1.0),
			D2D1::Point2F(size.width / 2, size.height / 2)));
	active_render_target->BeginDraw();
	active_render_target->DrawBitmap(sprite_sheet, dest);
	active_render_target->EndDraw();
	active_render_target->SetTransform(D2D1::Matrix3x2F::Identity());
	return Strue;
}

ptr d2d_zrender_sprite_rotscale(const int n, const float dx, const float dy, const float a, const float s) {

	if (n > bank_size - 1) {
		return Snil;
	}

	const auto sprite_sheet = sprite_sheets[n];
	if (sprite_sheet == nullptr) {
		return Snil;
	}
	const auto size = sprite_sheet->GetPixelSize();
	const auto dest = D2D1::RectF(dx, dy, dx + size.width, dy + size.height);
	const auto opacity = 1.0f;

	const auto scale = D2D1::Matrix3x2F::Scale(
		D2D1::Size(s, s),
		D2D1::Point2F(dx, dy));
	const D2D1::Matrix3x2F rot = D2D1::Matrix3x2F::Rotation(a, D2D1::Point2F(dx + (size.width / 2), dy + (size.height / 2)));
	active_render_target->SetTransform(rot * scale);
	active_render_target->DrawBitmap(sprite_sheet, dest);
	active_render_target->SetTransform(D2D1::Matrix3x2F::Identity());
	return Strue;
}

ptr d2d_render_sprite_rotscale(const int n, const float dx, const float dy, const float a, const float s) {

	if (n > bank_size - 1) {
		return Snil;
	}
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}
	auto sprite_sheet = sprite_sheets[n];
	if (sprite_sheet == nullptr) {
		return Snil;
	}
	const auto size = sprite_sheet->GetPixelSize();
	const auto dest = D2D1::RectF(dx, dy, dx + size.width, dy + size.height);
	const auto opacity = 1.0f;

	const auto scale = D2D1::Matrix3x2F::Scale(
		D2D1::Size(s, s),
		D2D1::Point2F(dx, dy));
	const D2D1::Matrix3x2F rot = D2D1::Matrix3x2F::Rotation(a, D2D1::Point2F(dx + (size.width / 2), dy + (size.height / 2)));

	active_render_target->BeginDraw();
	active_render_target->SetTransform(rot * scale);
	active_render_target->DrawBitmap(sprite_sheet, dest);
	active_render_target->SetTransform(D2D1::Matrix3x2F::Identity());
	active_render_target->EndDraw();
	return Strue;
}

// from sheet n; at sx, sy to dx, dy, w,h scale up
ptr d2d_zrender_sprite_sheet(const int n, const float dx, const float dy, const float dw, const float dh,
	const float sx, const float sy, const float sw, const float sh, const float scale) {

	if (n > bank_size - 1) {
		return Snil;
	}

	auto sprite_sheet = sprite_sheets[n];
	if (sprite_sheet == nullptr) {
		return Snil;
	}
	const auto size = sprite_sheet->GetPixelSize();
	const auto dest = D2D1::RectF(dx, dy, scale * (dx + dw), scale * (dy + dh));
	const auto source = D2D1::RectF(sx, sy, sx + sw, sy + sh);
	const auto opacity = 1.0f;
 
	active_render_target->DrawBitmap(sprite_sheet, dest, 1.0f,
		D2D1_BITMAP_INTERPOLATION_MODE::D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
 
	return Strue;
}

// from sheet n; at sx, sy to dx, dy, w,h scale up
ptr d2d_render_sprite_sheet(const int n, const float dx, const float dy, const float dw, const float dh,
	const float sx, const float sy, const float sw, const float sh, const float scale) {

	if (n > bank_size - 1) {
		return Snil;
	}

	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}

	auto sprite_sheet = sprite_sheets[n];
	if (sprite_sheet == nullptr) {
		return Snil;
	}
	const auto size = sprite_sheet->GetPixelSize();
	const auto dest = D2D1::RectF(dx, dy, scale * (dx + dw), scale * (dy + dh));
	const auto source = D2D1::RectF(sx, sy, sx + sw, sy + sh);
	const auto opacity = 1.0f;
	active_render_target->BeginDraw();
	active_render_target->DrawBitmap(sprite_sheet, dest, 1.0f,
		D2D1_BITMAP_INTERPOLATION_MODE::D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
	active_render_target->EndDraw();
	return Strue;
}

// from sheet n; at sx, sy to dx, dy, w,h scale up
ptr d2d_zrender_sprite_sheet_rot_scale(const int n, const float dx, const float dy, const float dw, const float dh,
	const float sx, const float sy, const float sw, const float sh, const float scale, const float a, const float x2, const float y2) {

	if (n > bank_size - 1) {
		return Snil;
	}
 
	auto sprite_sheet = sprite_sheets[n];
	if (sprite_sheet == nullptr) {
		return Snil;
	}
	const auto size = sprite_sheet->GetPixelSize();
	const auto dest = D2D1::RectF(dx, dy, scale * (dx + dw), scale * (dy + dh));
	const auto source = D2D1::RectF(sx, sy, sx + sw, sy + sh);
	const auto opacity = 1.0f;
	const D2D1::Matrix3x2F rot = D2D1::Matrix3x2F::Rotation(a, D2D1::Point2F(x2, y2));
 
	active_render_target->SetTransform(rot);
	active_render_target->DrawBitmap(sprite_sheet, dest, 1.0f,
		D2D1_BITMAP_INTERPOLATION_MODE::D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
	active_render_target->SetTransform(D2D1::Matrix3x2F::Identity());
 
	return Strue;
}

// from sheet n; at sx, sy to dx, dy, w,h scale up
ptr d2d_render_sprite_sheet_rot_scale(const int n, const float dx, const float dy, const float dw, const float dh,
	const float sx, const float sy, const float sw, const float sh, const float scale, const float a, const float x2, const float y2) {

	if (n > bank_size - 1) {
		return Snil;
	}
	if (window_render_target == nullptr || active_render_target == nullptr) {
		return Sfalse;
	}

	const auto sprite_sheet = sprite_sheets[n];
	if (sprite_sheet == nullptr) {
		return Snil;
	}
	const auto size = sprite_sheet->GetPixelSize();
	const auto dest = D2D1::RectF(dx, dy, scale * (dx + dw), scale * (dy + dh));
	const auto source = D2D1::RectF(sx, sy, sx + sw, sy + sh);
	const auto opacity = 1.0f;
	const D2D1::Matrix3x2F rot = D2D1::Matrix3x2F::Rotation(a, D2D1::Point2F(x2, y2));
	active_render_target->BeginDraw();
	active_render_target->SetTransform(rot);
	active_render_target->DrawBitmap(sprite_sheet, dest, 1.0f,
		D2D1_BITMAP_INTERPOLATION_MODE::D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
	active_render_target->SetTransform(D2D1::Matrix3x2F::Identity());
	active_render_target->EndDraw();
	return Strue;
}

void CreateFactory() {

	if (direct_2d_factory == nullptr) {
		HRESULT hr = D2D1CreateFactory(
			D2D1_FACTORY_TYPE_MULTI_THREADED, &direct_2d_factory);
		d2d_dpi_scale(direct_2d_factory);
	}
	if (direct_write_factory == nullptr) {
		HRESULT hr = DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(&direct_write_factory)
		);
	}
}

HRESULT Create_D2D_Device_Dep(HWND h)
{
	if (IsWindow(h)) {

		if (window_render_target == nullptr)
		{
			CreateFactory();
			RECT rc;
			GetClientRect(h, &rc);
			D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);
			HRESULT hr = direct_2d_factory->CreateHwndRenderTarget(
				D2D1::RenderTargetProperties(),
				D2D1::HwndRenderTargetProperties(h,
					D2D1::SizeU(static_cast<UINT32>(rc.right), static_cast<UINT32>(rc.bottom))),
				&window_render_target);

			if (FAILED(hr)) {
		
				return hr;
			}

			d2d_CreateGridPatternBrush(window_render_target, &brush_pattern);
			
			// create offscreen bitmap
			d2d_CreateOffscreenBitmap();

			d2d_line_brush(0, 0.0, 0.0, 0.0, 0.0);
			line_colour_brush = line_brushes[0];
			d2d_fill_brush(0, 0.0, 1.0, 0.0, 1.0);
			fill_colour_brush = fill_brushes[0];

			hr = direct_write_factory->CreateTextFormat(
				L"Consolas",
				nullptr,
				DWRITE_FONT_WEIGHT_REGULAR,
				DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL,
				32.0f,
				L"en-us",
				&current_text_font
			);

			if (FAILED(hr)) {
		
				return hr;
			}

			hr = window_render_target->CreateSolidColorBrush(
				D2D1::ColorF(D2D1::ColorF::Black),
				&black_brush
			);
 

			if (FAILED(hr)) {
	
				return hr;
			}

			return hr;
		}
	}
	else {
	
	}
	return 0;
}

void safe_release() {

	SafeRelease(&window_render_target);
	SafeRelease(&brush_pattern);
	SafeRelease(&black_brush);
	SafeRelease(&direct_2d_factory);
	SafeRelease(&current_text_font);
	SafeRelease(&bitmap_render_target);
	SafeRelease(&bitmap_render_target2);
	SafeRelease(&bitmap);
	SafeRelease(&bitmap2);
 
	for (int i = 0; i < brush_size - 1; i++ ) {
		SafeRelease(&linear_brushes[i]);
		SafeRelease(&radial_brushes[i]);
		SafeRelease(&line_brushes[i]);
	}
}

int complexity_limit = 500;
int commands_length;
const int ignore_clip = 200;
const int sprite_command_size=8192;

struct sprite_command {
	bool active=false;
	bool persist = false;
	float r;
	float g;
	float b;
	float a;
	float x;
	float y;
	float w;
	float h;
	float sx;
	float sy;
	float sw;
	float sh;
	float xn = 0.0f;
	float yn = 0.0f;
	float s;
	float angle;
	int	bank;
	int render_type;
	int f = 0;
	std::wstring text;
};

sprite_command sprite_commands[sprite_command_size];

ptr check_clip(const float x, const float y)
{
	if (x > prefer_width + ignore_clip) return Snil;
	if (x < -ignore_clip) return Snil;
	if (y < -ignore_clip) return Snil;
	if (y > prefer_height + ignore_clip) return Snil;
	return Strue;
}


ptr set_draw_sprite(int c, int n, const float x, const float y) {

	if (check_clip(x, y) == Snil) return Snil;

	if (c > sprite_command_size - 1) {
		return Snil;
	}
	if (n > bank_size - 1) {
		return Snil;
	}
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	sprite_commands[c].active = true;
	sprite_commands[c].bank = n;
	sprite_commands[c].x = x;
	sprite_commands[c].y = y;
	sprite_commands[c].render_type = 1;  
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_draw_sprite(const int n, const float x, const float y) {

	if (check_clip(x, y) == Snil) return Snil;

	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].bank = n;
	sprite_commands[c].x = x;
	sprite_commands[c].y = y;
	sprite_commands[c].render_type = 1;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_draw_rect(const float x, const float y, const float w, const float h) {
	
	if (check_clip(x, y) == Snil) return Snil;

	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	sprite_commands[c].active = true;
	sprite_commands[c].x = x;
	sprite_commands[c].y = y;
	sprite_commands[c].w = w;
	sprite_commands[c].h = h;
	sprite_commands[c].render_type = 20;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_fill_rect(const float x, const float y, const float w, const float h) {

	if (check_clip(x, y) == Snil) return Snil;

	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	sprite_commands[c].active = true;
	sprite_commands[c].x = x;
	sprite_commands[c].y = y;
	sprite_commands[c].w = w;
	sprite_commands[c].h = h;
	sprite_commands[c].render_type = 21;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}


ptr add_linear_gradient_fill_rect(
	const float x, const float y, const float w, const float h,
	const float x1, const float y1, const float x2, const float y2) {

	if (check_clip(x, y) == Snil) return Snil;

	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	sprite_commands[c].active = true;
	sprite_commands[c].x = x;
	sprite_commands[c].y = y;
	sprite_commands[c].w = w;
	sprite_commands[c].h = h;
	sprite_commands[c].sx = x1;
	sprite_commands[c].sy = y1;
	sprite_commands[c].xn = x2;
	sprite_commands[c].yn = y2;
	sprite_commands[c].render_type = 41;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_radial_gradient_fill_rect(
	const float x, const float y, const float w, const float h,
	const float x1, const float y1, const float x2, const float y2) {

	if (check_clip(x, y) == Snil) return Snil;

	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	sprite_commands[c].active = true;
	sprite_commands[c].x = x;
	sprite_commands[c].y = y;
	sprite_commands[c].w = w;
	sprite_commands[c].h = h;
	sprite_commands[c].sx = x1;
	sprite_commands[c].sy = y1;
	sprite_commands[c].xn = x2;
	sprite_commands[c].yn = y2;
	sprite_commands[c].render_type = 42;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}


ptr add_radial_gradient_fill_ellipse(
	const float x, const float y, const float w, const float h,
	const float x1, const float y1, const float x2, const float y2) {

	if (check_clip(x, y) == Snil) return Snil;

	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	sprite_commands[c].active = true;
	sprite_commands[c].x = x;
	sprite_commands[c].y = y;
	sprite_commands[c].w = w;
	sprite_commands[c].h = h;
	sprite_commands[c].sx = x1;
	sprite_commands[c].sy = y1;
	sprite_commands[c].xn = x2;
	sprite_commands[c].yn = y2;
	sprite_commands[c].render_type = 44;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_linear_gradient_fill_ellipse(
	const float x, const float y, const float w, const float h,
	const float x1, const float y1, const float x2, const float y2) {

	if (check_clip(x, y) == Snil) return Snil;

	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	sprite_commands[c].active = true;
	sprite_commands[c].x = x;
	sprite_commands[c].y = y;
	sprite_commands[c].w = w;
	sprite_commands[c].h = h;
	sprite_commands[c].sx = x1;
	sprite_commands[c].sy = y1;
	sprite_commands[c].xn = x2;
	sprite_commands[c].yn = y2;
	sprite_commands[c].render_type = 43;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_ellipse (const float x, const float y, const float w, const float h) {
	
	if (check_clip(x, y) == Snil) return Snil;

	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].x = x;
	sprite_commands[c].y = y;
	sprite_commands[c].w = w;
	sprite_commands[c].h = h;
	sprite_commands[c].render_type = 22;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_fill_ellipse(const float x, const float y, const float w, const float h) {

	
	if (check_clip(x, y) == Snil) return Snil;
	
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].x = x;
	sprite_commands[c].y = y;
	sprite_commands[c].w = w;
	sprite_commands[c].h = h;
	sprite_commands[c].render_type = 23;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_line(const float x, const float y, const float w, const float h) {

	if (check_clip(x, y) == Snil) return Snil;
	
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].x = x;
	sprite_commands[c].y = y;
	sprite_commands[c].w = w;
	sprite_commands[c].h = h;
	sprite_commands[c].render_type = 24;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);	
	return Strue;
}

ptr add_clear_image(const float r, const float g, const float b, const float a) {
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].r = r;
	sprite_commands[c].g = g;
	sprite_commands[c].b = b;
	sprite_commands[c].a = a;
	sprite_commands[c].render_type = 8;	
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_line_colour(const float r, const float g, const float b, const float a) {
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].r = r;
	sprite_commands[c].g = g;
	sprite_commands[c].b = b;
	sprite_commands[c].a = a;
	sprite_commands[c].render_type = 10;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_fill_colour(const float r, const float g, const float b, const float a) {
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].r = r;
	sprite_commands[c].g = g;
	sprite_commands[c].b = b;
	sprite_commands[c].a = a;
	sprite_commands[c].render_type = 11;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_pen_width(const float w) {
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].w = w;
	sprite_commands[c].render_type = 12;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_select_fill_brush(const int n) {
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].w = n;
	sprite_commands[c].render_type = 50;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_select_line_brush(const int n) {
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].w = n;
	sprite_commands[c].render_type = 51;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_select_radial_brush(const int n) {
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].w = n;
	sprite_commands[c].render_type = 52;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_select_linear_brush(const int n) {
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].w = n;
	sprite_commands[c].render_type = 52;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_write_text(const float x, const float y, char*s) {
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].x = x;
	sprite_commands[c].y = y;

	sprite_commands[c].r = fill_r;
	sprite_commands[c].g = fill_g;
	sprite_commands[c].b = fill_b;
	sprite_commands[c].a = fill_a;

	sprite_commands[c].text = Utility::widen(s);
	sprite_commands[c].render_type = 9;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr add_scaled_rotated_sprite(const int n, const float x, const float y, const float a, const float s) {

	if (check_clip(x, y) == Snil) return Snil;
	
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].bank = n;
	sprite_commands[c].x = x;
	sprite_commands[c].y = y;
	sprite_commands[c].angle = a;
	sprite_commands[c].s = s;
	sprite_commands[c].render_type = 2;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}
 

ptr add_render_sprite_sheet(const int n, 
	const float dx, const float dy, const float dw, const float dh,
	const float sx, const float sy, const float sw, const float sh, 
	const float scale) {

	if (check_clip(dx, dy) == Snil) return Snil;
	
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	int c = 1;
	while (c < sprite_command_size && sprite_commands[c].render_type > 0) c++;
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = true;
	sprite_commands[c].bank = n;
	sprite_commands[c].x = dx;
	sprite_commands[c].y = dy;
	sprite_commands[c].w = dw;
	sprite_commands[c].h = dh;
	sprite_commands[c].sx = sx;
	sprite_commands[c].sy = sy;
	sprite_commands[c].sw = sw;
	sprite_commands[c].sh = sh; 
	sprite_commands[c].s = scale;
	sprite_commands[c].render_type = 3;
	commands_length++;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

ptr clear_all_draw_sprite() {
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	for (int i = 0; i < sprite_command_size - 1; i++) {
		sprite_commands[i].active = false;
		sprite_commands[i].bank = bank_size + 1;
		sprite_commands[i].render_type = 0;
	}
	ReleaseMutex(g_sprite_commands_mutex);
	return Snil;
}

ptr clear_draw_sprite(int c) {
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	if (c > sprite_command_size - 1) {
		ReleaseMutex(g_sprite_commands_mutex);
		return Snil;
	}
	sprite_commands[c].active = false;
	sprite_commands[c].bank = bank_size+1;
	sprite_commands[c].render_type = 0;
	ReleaseMutex(g_sprite_commands_mutex);
	return Strue;
}

void do_write_text(const float x, const float y, std::wstring s) {
	const auto len = s.length();
	D2D1_RECT_F layoutRect = D2D1::RectF(x, y, prefer_width - x, prefer_height - y);
	active_render_target->DrawTextW(s.c_str(), len, current_text_font, layoutRect, fill_colour_brush);
}

void do_draw_sprite(const int n, const float x, const float y) {
	d2d_zrender_sprite(n, x, y);
}

void do_scaled_rotated_sprite(const int n, const float x, const float y, const float a, const float s) {
	d2d_zrender_sprite_rotscale(n, x, y, a, s);
}

void do_render_sprite_sheet(const int n, const float dx, const float dy, const float dw, const float dh,
	const float sx, const float sy, const float sw, const float sh, const float scale) {
	d2d_zrender_sprite_sheet(n, dx, dy, dw, dh,
		sx, sy, sw, sh, scale);
}


void render_sprite_commands() {
	 
	if (window_render_target == nullptr || active_render_target == nullptr) {
		d2d_image_size(static_cast<int>(prefer_width), static_cast<int>(prefer_height));
	}
	WaitForSingleObject(g_sprite_commands_mutex, INFINITE);
	d2d_CreateOffscreenBitmap();

	active_render_target->BeginDraw();

	for (int i = 1; i < commands_length+1; i++) {

		if (sprite_commands[i].active == true) {
			switch (sprite_commands[i].render_type) {
			case 1:
				do_draw_sprite(
					sprite_commands[i].bank,
					sprite_commands[i].x,
					sprite_commands[i].y);
				break;
			case 2:
				do_scaled_rotated_sprite(
					sprite_commands[i].bank,
					sprite_commands[i].x,
					sprite_commands[i].y,
					sprite_commands[i].angle,
					sprite_commands[i].s);
				break;
			case 3:
				do_render_sprite_sheet(
					sprite_commands[i].bank,
					sprite_commands[i].x,
					sprite_commands[i].y,
					sprite_commands[i].w,
					sprite_commands[i].h,
					sprite_commands[i].sx,
					sprite_commands[i].sy,
					sprite_commands[i].sw,
					sprite_commands[i].sh,
					sprite_commands[i].s);
				break;
			case 8:
				d2d_zclear(
					sprite_commands[i].r,
					sprite_commands[i].g,
					sprite_commands[i].b,
					sprite_commands[i].a);
				break;
			case 9:
				do_write_text(
					sprite_commands[i].x,
					sprite_commands[i].y,
					sprite_commands[i].text);
				break;
			case 10:
				d2d_color(
					sprite_commands[i].r,
					sprite_commands[i].g,
					sprite_commands[i].b,
					sprite_commands[i].a);
				break;
			case 11:
				d2d_fill_color(
					sprite_commands[i].r,
					sprite_commands[i].g,
					sprite_commands[i].b,
					sprite_commands[i].a);
				break;
			case 12:
				d2d_stroke_width = sprite_commands[i].w;
				break;
			case 20:
				d2d_zrectangle(
					sprite_commands[i].x,
					sprite_commands[i].y,
					sprite_commands[i].w,
					sprite_commands[i].h);
				break;
			case 21:
				d2d_zfill_rectangle(
					sprite_commands[i].x,
					sprite_commands[i].y,
					sprite_commands[i].w,
					sprite_commands[i].h);
				break;
			case 22:
				d2d_zellipse(
					sprite_commands[i].x,
					sprite_commands[i].y,
					sprite_commands[i].w,
					sprite_commands[i].h);
				break;
			case 23:
				d2d_zfill_ellipse(
					sprite_commands[i].x,
					sprite_commands[i].y,
					sprite_commands[i].w,
					sprite_commands[i].h);
				break;
			case 24:
				d2d_zline(
					sprite_commands[i].x,
					sprite_commands[i].y,
					sprite_commands[i].w,
					sprite_commands[i].h);
				break;
			case 41:
				d2d_zlinear_gradient_fill_rectangle(
					sprite_commands[i].x,
					sprite_commands[i].y,
					sprite_commands[i].w,
					sprite_commands[i].h,
					sprite_commands[i].sx,
					sprite_commands[i].sy,
					sprite_commands[i].xn,
					sprite_commands[i].yn);
				break;
			case 42:
				d2d_zradial_gradient_fill_rectangle(
					sprite_commands[i].x,
					sprite_commands[i].y,
					sprite_commands[i].w,
					sprite_commands[i].h,
					sprite_commands[i].sx,
					sprite_commands[i].sy,
					sprite_commands[i].xn,
					sprite_commands[i].yn);
				break;
			case 43:
				d2d_zlinear_gradient_fill_ellipse(
					sprite_commands[i].x,
					sprite_commands[i].y,
					sprite_commands[i].w,
					sprite_commands[i].h,
					sprite_commands[i].sx,
					sprite_commands[i].sy,
					sprite_commands[i].xn,
					sprite_commands[i].yn);
				break;
			case 44:
				d2d_zradial_gradient_fill_ellipse(
					sprite_commands[i].x,
					sprite_commands[i].y,
					sprite_commands[i].w,
					sprite_commands[i].h,
					sprite_commands[i].sx,
					sprite_commands[i].sy,
					sprite_commands[i].xn,
					sprite_commands[i].yn);
				break;

			case 50:
				d2d_select_fill_color(sprite_commands[i].w);
				break;
			case 51:
				d2d_select_color(sprite_commands[i].w);
				break;
			case 52:
				d2d_select_linear_brush(sprite_commands[i].w);
				break;
			case 53:
				d2d_select_radial_brush(sprite_commands[i].w);
				break;
			default:
				// unrecognized command.
				break;
			}
		}
		
		if ((i % complexity_limit) == 0) {
			active_render_target->EndDraw();
			active_render_target->BeginDraw();
		}
	}
	active_render_target->EndDraw();

	// clear for next step
	for (int i = 0; i < sprite_command_size - 1; i++) {
		sprite_commands[i].active = false;
		sprite_commands[i].bank = bank_size + 1;
		sprite_commands[i].render_type = 0;
		sprite_commands[i].text.clear();
	}
	commands_length = 0;
	ReleaseMutex(g_sprite_commands_mutex);
}

ptr d2d_image_size(int w, int h)
{

	prefer_width = static_cast<float>(w);
	prefer_height = static_cast<float>(h);
	safe_release();
	Create_D2D_Device_Dep(main_window);
	while (window_render_target == nullptr) {
		Sleep(10);
	}
	d2d_clear(0.0, 0.0, 0.0, 1.0);
	//d2d_fill_rectangle(0.0, 0.0, 1.0 * w, 1.0 * h);
	return Strue;
}


ptr d2d_release() {
	safe_release();
	return Strue;
}

void onPaint(HWND hWnd) {

	WaitForSingleObject(g_image_rotation_mutex, INFINITE);

	if (main_window == nullptr) {
		main_window = hWnd;
	}

	if (main_window != hWnd) {
		main_window = hWnd;
		safe_release();
	}

	RECT rc;
	::GetClientRect(hWnd, &rc);
	const D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);
	//
	HRESULT hr = Create_D2D_Device_Dep(hWnd);
	if (SUCCEEDED(hr))
	{
		window_render_target->Resize(size);
		window_render_target->BeginDraw();
		const D2D1_SIZE_F render_target_size = window_render_target->GetSize();
		window_render_target->Clear(D2D1::ColorF(D2D1::ColorF::LightGray));

		window_render_target->FillRectangle(
			D2D1::RectF(0.0f, 0.0f, render_target_size.width, render_target_size.height), brush_pattern);

		if (bitmap2 != nullptr) {
			window_render_target->DrawBitmap(bitmap2, D2D1::RectF(0.0f, 0.0f, prefer_width, prefer_height));
		}
		hr = window_render_target->EndDraw();

		if (FAILED(hr)) {

			safe_release();
		}
	}

	if (hr == D2DERR_RECREATE_TARGET)
	{
		safe_release();
	}
	if (FAILED(hr)) {
		safe_release();
	}
	else
	{
		::ValidateRect(hWnd, nullptr);
	}
	ReleaseMutex(g_image_rotation_mutex);
}

void step(ptr lpParam) {

	if (!IsWindow(main_window)) return;
	WaitForSingleObject(g_image_rotation_mutex, INFINITE);
	if (Sprocedurep(lpParam)) {
		Scall0(lpParam);
	}
	ReleaseMutex(g_image_rotation_mutex);
}

ptr step_func(ptr lpParam) {

	WaitForSingleObject(g_image_rotation_mutex, INFINITE);
	if (Sprocedurep(lpParam)) {
		Scall0(lpParam);
	}
	ReleaseMutex(g_image_rotation_mutex);
	if(main_window!=nullptr)
		PostMessageW(main_window, WM_USER + 501, (WPARAM)0, (LPARAM)0);
	return Strue;
}

// constructor
CD2DView::CD2DView()
{
}

// destructor
CD2DView::~CD2DView()
{
	safe_release();
}

// Create the resources used by OnRender
HRESULT CD2DView::CreateDeviceResources()
{
    HRESULT hr = S_OK;
    return hr;
}

void CD2DView::DiscardDeviceResources()
{
	safe_release();
}

HRESULT CD2DView::OnRender()
{
    HRESULT hr = S_OK;
    onPaint(GetHwnd());
    return 0;
}

void CD2DView::OnResize(UINT width, UINT height)
{
  
}

void CD2DView::Stop()
{	
	safe_release();
	ReleaseMutex(g_image_rotation_mutex);
}

void scan_keys();
void CD2DView::Step(ptr n)
{
	step(n);
}

void CD2DView::Swap(const int n)
{
	if(n == 2) render_sprite_commands();
	if(n == 3) render_sprite_commands();
	swap_buffers(n);
}

void CD2DView::PreCreate(CREATESTRUCT& cs)
{
    cs.cx = 640;
    cs.cy = 480;
}

void CD2DView::PreRegisterClass(WNDCLASS& wc)
{
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hCursor = ::LoadCursor(nullptr, IDI_APPLICATION);
    wc.lpszClassName = _T("Direct2D");
}

int CD2DView::OnCreate(CREATESTRUCT& cs)
{
    UNREFERENCED_PARAMETER(cs);
    // Set the window's icon
    SetIconSmall(IDW_MAIN);
    SetIconLarge(IDW_MAIN);
    return 0;
}

void CD2DView::OnDestroy()
{
	main_window = nullptr;
	safe_release();
}

LRESULT CD2DView::OnPaint(UINT, WPARAM, LPARAM)
{
    if (GetHwnd() != nullptr) {
        OnRender();
        return ValidateRect();
    }
    return 0;
}

LRESULT CD2DView::OnSize(UINT, WPARAM, LPARAM lparam)
{
	const UINT width = LOWORD(lparam);
	const UINT height = HIWORD(lparam);
    OnResize(width, height);
    return 0;
}

LRESULT CD2DView::OnDisplayChange(UINT, WPARAM, LPARAM)
{
    Invalidate();
    return 0;
}

struct kp {
    long long when;
    boolean left;
    boolean right;
    boolean up;
    boolean down;
    boolean ctrl;
    boolean space;
    int key_code;
} graphics_keypressed;

ptr cons_sbool(const char* symbol, bool value, ptr l)
{
    ptr a = Snil;
    if (value) {
        a = CALL2("cons", Sstring_to_symbol(symbol), Strue);
    }
    else
    {
        a = CALL2("cons", Sstring_to_symbol(symbol), Sfalse);
    }
    l = CALL2("cons", a, l);
    return l;
}
ptr cons_sfixnum(const char* symbol, const int value, ptr l)
{
    ptr a = Snil;
    a = CALL2("cons", Sstring_to_symbol(symbol), Sfixnum(value));
    l = CALL2("cons", a, l);
    return l;
}

long long  debounce_delay = 80;
long long  debounce = 0;

void scan_keys() {
	if (debounce_delay == 0) return;
	if (GetTickCount64() - debounce > debounce_delay) {
		if (GetAsyncKeyState(VK_LEFT) != 0)
			graphics_keypressed.left = true;
		if (GetAsyncKeyState(VK_RIGHT) != 0)
			graphics_keypressed.right = true;
		if (GetAsyncKeyState(VK_UP) != 0)
			graphics_keypressed.up = true;
		if (GetAsyncKeyState(VK_DOWN) != 0)
			graphics_keypressed.up = true;
		if (GetAsyncKeyState(VK_SPACE) != 0)
			graphics_keypressed.space = true;
		if (GetAsyncKeyState(VK_CONTROL) != 0)
			graphics_keypressed.ctrl = true;
		debounce = GetTickCount64();
	}
}

ptr graphics_keys(void) {
	ptr a = Snil;
	a = cons_sbool("left", graphics_keypressed.left, a);
	a = cons_sbool("right", graphics_keypressed.right, a);
	a = cons_sbool("up", graphics_keypressed.up, a);
	a = cons_sbool("down", graphics_keypressed.down, a);
	a = cons_sbool("ctrl", graphics_keypressed.ctrl, a);
	a = cons_sbool("space", graphics_keypressed.space, a);
	a = cons_sfixnum("key", graphics_keypressed.key_code, a);
	a = cons_sfixnum("recent", GetTickCount64() - graphics_keypressed.when, a);
	graphics_keypressed.ctrl = false;
	graphics_keypressed.left = false;
	graphics_keypressed.right = false;
	graphics_keypressed.down = false;
	graphics_keypressed.up = false;
	graphics_keypressed.space = false;
	graphics_keypressed.key_code = 0;
	graphics_keypressed.when = GetTickCount64();
	return a;
}

ptr keyboard_debounce(const int n) {
	debounce_delay = n;
	
	return Snil;
}

LRESULT CD2DView::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_KEYDOWN:
        graphics_keypressed.key_code = wparam;
        graphics_keypressed.when = GetTickCount64();
        break;

    case WM_DISPLAYCHANGE: 
		return OnDisplayChange(msg, wparam, lparam);
    case WM_SIZE:          
		return OnSize(msg, wparam, lparam);
    }

    return WndProcDefault(msg, wparam, lparam);
}


void add_d2d_commands() {

	Sforeign_symbol("d2d_matrix_identity",					static_cast<ptr>(d2d_matrix_identity));
	Sforeign_symbol("d2d_matrix_rotate",					static_cast<ptr>(d2d_matrix_rotate));
	Sforeign_symbol("d2d_render_sprite",					static_cast<ptr>(d2d_render_sprite));
	Sforeign_symbol("d2d_render_sprite_rotscale",			static_cast<ptr>(d2d_render_sprite_rotscale));
	Sforeign_symbol("d2d_render_sprite_sheet",				static_cast<ptr>(d2d_render_sprite_sheet));
	Sforeign_symbol("d2d_render_sprite_sheet_rot_scale",	static_cast<ptr>(d2d_render_sprite_sheet_rot_scale));
	Sforeign_symbol("sprite_size",							static_cast<ptr>(sprite_size));
	Sforeign_symbol("d2d_load_sprites",						static_cast<ptr>(d2d_load_sprites));
	Sforeign_symbol("d2d_FreeAllSprites",					static_cast<ptr>(d2d_FreeAllSprites));
	Sforeign_symbol("d2d_FreeSpriteInBank",					static_cast<ptr>(d2d_FreeSpriteInBank));
	Sforeign_symbol("d2d_MakeSpriteInBank",					static_cast<ptr>(d2d_MakeSpriteInBank));
	Sforeign_symbol("d2d_write_text",						static_cast<ptr>(d2d_write_text));
	Sforeign_symbol("d2d_zwrite_text",						static_cast<ptr>(d2d_zwrite_text));
	Sforeign_symbol("d2d_text_mode",						static_cast<ptr>(d2d_text_mode));
	Sforeign_symbol("d2d_set_font",							static_cast<ptr>(d2d_set_font));

	Sforeign_symbol("d2d_color",							static_cast<ptr>(d2d_color));
	Sforeign_symbol("d2d_line_brush",						static_cast<ptr>(d2d_line_brush));
	Sforeign_symbol("d2d_select_color",						static_cast<ptr>(d2d_select_color));
	Sforeign_symbol("d2d_fill_color",						static_cast<ptr>(d2d_fill_color));
	Sforeign_symbol("d2d_fill_brush",						static_cast<ptr>(d2d_fill_brush));
	Sforeign_symbol("d2d_select_fill_color",				static_cast<ptr>(d2d_select_fill_color));
	

	Sforeign_symbol("d2d_linear_gradient_brush",			static_cast<ptr>(d2d_linear_gradient_brush));
	Sforeign_symbol("d2d_select_linear_brush",				static_cast<ptr>(d2d_select_linear_brush));
	Sforeign_symbol("d2d_linear_gradient_color_list",		static_cast<ptr>(d2d_linear_gradient_color_list));

	Sforeign_symbol("d2d_radial_gradient_brush",			static_cast<ptr>(d2d_radial_gradient_brush));
	Sforeign_symbol("d2d_select_radial_brush",				static_cast<ptr>(d2d_select_radial_brush));
	Sforeign_symbol("d2d_radial_gradient_color_list",		static_cast<ptr>(d2d_radial_gradient_color_list));
	

	Sforeign_symbol("d2d_rectangle",						static_cast<ptr>(d2d_rectangle));
	Sforeign_symbol("d2d_zrectangle",						static_cast<ptr>(d2d_zrectangle));
	Sforeign_symbol("d2d_fill_rectangle",					static_cast<ptr>(d2d_fill_rectangle));
	Sforeign_symbol("d2d_zfill_rectangle",					static_cast<ptr>(d2d_zfill_rectangle));
	Sforeign_symbol("d2d_zradial_gradient_fill_rectangle",	static_cast<ptr>(d2d_zradial_gradient_fill_rectangle));
	Sforeign_symbol("d2d_zradial_gradient_fill_ellipse",	static_cast<ptr>(d2d_zradial_gradient_fill_ellipse));
	Sforeign_symbol("d2d_radial_gradient_fill_rectangle",	static_cast<ptr>(d2d_radial_gradient_fill_rectangle));
	Sforeign_symbol("d2d_radial_gradient_fill_ellipse",		static_cast<ptr>(d2d_radial_gradient_fill_ellipse));
	Sforeign_symbol("d2d_zlinear_gradient_fill_rectangle",	static_cast<ptr>(d2d_zlinear_gradient_fill_rectangle));
	Sforeign_symbol("d2d_zlinear_gradient_fill_ellipse",	static_cast<ptr>(d2d_zlinear_gradient_fill_ellipse));
	Sforeign_symbol("d2d_linear_gradient_fill_rectangle",	static_cast<ptr>(d2d_linear_gradient_fill_rectangle));
	Sforeign_symbol("d2d_linear_gradient_fill_ellipse",		static_cast<ptr>(d2d_linear_gradient_fill_ellipse));

	Sforeign_symbol("d2d_ellipse",							static_cast<ptr>(d2d_ellipse));
	Sforeign_symbol("d2d_zellipse",							static_cast<ptr>(d2d_zellipse));
	Sforeign_symbol("d2d_fill_ellipse",						static_cast<ptr>(d2d_fill_ellipse));
	Sforeign_symbol("d2d_zfill_ellipse",					static_cast<ptr>(d2d_zfill_ellipse));
	Sforeign_symbol("d2d_line",								static_cast<ptr>(d2d_line));
	Sforeign_symbol("d2d_zline",							static_cast<ptr>(d2d_zline));
	Sforeign_symbol("d2d_render",							static_cast<ptr>(d2d_render));
	Sforeign_symbol("d2d_show",								static_cast<ptr>(d2d_show));
	Sforeign_symbol("d2d_save",								static_cast<ptr>(d2d_save)); 
	Sforeign_symbol("d2d_clear",							static_cast<ptr>(d2d_clear));
	Sforeign_symbol("d2d_zclear",							static_cast<ptr>(d2d_zclear));
	Sforeign_symbol("d2d_set_stroke_width",					static_cast<ptr>(d2d_set_stroke_width));
	Sforeign_symbol("d2d_image_size",						static_cast<ptr>(d2d_image_size));
	Sforeign_symbol("d2d_release",							static_cast<ptr>(d2d_release));
	Sforeign_symbol("d2d_draw_func",						static_cast<ptr>(d2d_draw_func));
	Sforeign_symbol("step",									static_cast<ptr>(step_func));
	Sforeign_symbol("set_draw_sprite",						static_cast<ptr>(set_draw_sprite));
	Sforeign_symbol("clear_draw_sprite",					static_cast<ptr>(clear_draw_sprite));
	Sforeign_symbol("clear_all_draw_sprite",				static_cast<ptr>(clear_all_draw_sprite));
	Sforeign_symbol("add_clear_image",						static_cast<ptr>(add_clear_image));
	Sforeign_symbol("add_write_text",						static_cast<ptr>(add_write_text));
	Sforeign_symbol("add_draw_sprite",						static_cast<ptr>(add_draw_sprite));
	Sforeign_symbol("add_scaled_rotated_sprite",			static_cast<ptr>(add_scaled_rotated_sprite));
	
	Sforeign_symbol("add_ellipse",							static_cast<ptr>(add_ellipse));
	Sforeign_symbol("add_fill_colour",						static_cast<ptr>(add_fill_colour));
	Sforeign_symbol("add_line_colour",						static_cast<ptr>(add_line_colour));
	Sforeign_symbol("add_fill_ellipse",						static_cast<ptr>(add_fill_ellipse));
	Sforeign_symbol("add_linear_gradient_fill_ellipse",		static_cast<ptr>(add_linear_gradient_fill_ellipse));
	Sforeign_symbol("add_radial_gradient_fill_ellipse",		static_cast<ptr>(add_radial_gradient_fill_ellipse));
	Sforeign_symbol("add_draw_rect",						static_cast<ptr>(add_draw_rect));
	Sforeign_symbol("add_draw_line",						static_cast<ptr>(add_line));
	Sforeign_symbol("add_fill_rect",						static_cast<ptr>(add_fill_rect));
	Sforeign_symbol("add_linear_gradient_fill_rect",		static_cast<ptr>(add_linear_gradient_fill_rect));
	Sforeign_symbol("add_radial_gradient_fill_rect",		static_cast<ptr>(add_radial_gradient_fill_rect));
	Sforeign_symbol("add_pen_width",						static_cast<ptr>(add_pen_width));
	Sforeign_symbol("add_select_fill_brush",				static_cast<ptr>(add_select_fill_brush));
	Sforeign_symbol("add_select_line_brush",				static_cast<ptr>(add_select_line_brush));
	Sforeign_symbol("add_select_radial_brush",				static_cast<ptr>(add_select_radial_brush));
	Sforeign_symbol("add_select_linear_brush",				static_cast<ptr>(add_select_linear_brush));

	Sforeign_symbol("d2d_zmatrix_skew",						static_cast<ptr>(d2d_zmatrix_skew));
	Sforeign_symbol("d2d_zmatrix_translate",				static_cast<ptr>(d2d_zmatrix_translate));
	Sforeign_symbol("d2d_zmatrix_transrot",					static_cast<ptr>(d2d_zmatrix_transrot));
	Sforeign_symbol("d2d_zmatrix_rotrans",					static_cast<ptr>(d2d_zmatrix_identity));
	Sforeign_symbol("d2d_zmatrix_identity",					static_cast<ptr>(d2d_zmatrix_rotrans));
	Sforeign_symbol("graphics_keys",						static_cast<ptr>(graphics_keys));
	Sforeign_symbol("keyboard_debounce",					static_cast<ptr>(keyboard_debounce));
}

void CD2DView::AddCommands()
{
	if (g_image_rotation_mutex == nullptr) {
		g_image_rotation_mutex = CreateMutex(nullptr, FALSE, nullptr);
	}
	if (g_sprite_commands_mutex == nullptr) {
		g_sprite_commands_mutex = CreateMutex(nullptr, FALSE, nullptr);
	}

 
	add_d2d_commands();
	for (int i = 0; i < brush_size - 1; i++) {
		fill_brushes[i] = nullptr;
		linear_brushes[i] = nullptr;
		radial_brushes[i] = nullptr;
	}
}

void CD2DView::ScanKeys()
{
	scan_keys();
}
