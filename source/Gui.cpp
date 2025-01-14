#include "EnginePch.h"
#include "EngineCore.h"
#include "Gui.h"
#include "Layout.h"
#include "Container.h"
#include "DialogBox.h"
#include "GuiRect.h"
#include "Engine.h"
#include "Profiler.h"
#include "Render.h"
#include "Input.h"
#include "ResourceManager.h"
#include "DirectX.h"

Gui* app::gui;

//=================================================================================================
Gui::Gui() : tFontTarget(nullptr), vb(nullptr), vb2(nullptr), cursor_mode(CURSOR_NORMAL), vb2_locked(false), focused_ctrl(nullptr), tPixel(nullptr),
master_layout(nullptr), layout(nullptr), overlay(nullptr), grayscale(false), vertex_decl(nullptr), effect(nullptr)
{
}

//=================================================================================================
Gui::~Gui()
{
	DeleteElements(created_dialogs);
	SafeRelease(tPixel);
	delete master_layout;
}

//=================================================================================================
void Gui::Init()
{
	device = app::render->GetDevice();
	sprite = app::render->GetSprite();
	Control::input = app::input;
	Control::gui = this;
	tFontTarget = nullptr;
	wnd_size = app::engine->GetWindowSize();
	cursor_pos = wnd_size / 2;

	CreateVertexBuffer();

	color_table[1] = Vec4(1, 0, 0, 1);
	color_table[2] = Vec4(0, 1, 0, 1);
	color_table[3] = Vec4(1, 1, 0, 1);
	color_table[4] = Vec4(1, 1, 1, 1);
	color_table[5] = Vec4(0, 0, 0, 1);

	layer = new Container;
	layer->auto_focus = true;
	dialog_layer = new Container;
	dialog_layer->focus_top = true;

	// create pixel texture
	V(D3DXCreateTexture(device, 1, 1, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tPixel));
	D3DLOCKED_RECT lock;
	V(tPixel->LockRect(0, &lock, nullptr, 0));
	*((DWORD*)lock.pBits) = Color(255, 255, 255).value;
	V(tPixel->UnlockRect(0));

	// create vertex declaration
	const D3DVERTEXELEMENT9 v[] = {
		{ 0, 0,  D3DDECLTYPE_FLOAT3,	D3DDECLMETHOD_DEFAULT,	D3DDECLUSAGE_POSITION,		0 },
		{ 0, 12, D3DDECLTYPE_FLOAT2,	D3DDECLMETHOD_DEFAULT,	D3DDECLUSAGE_TEXCOORD,		0 },
		{ 0, 20, D3DDECLTYPE_FLOAT4,	D3DDECLMETHOD_DEFAULT,	D3DDECLUSAGE_COLOR,			0 },
		D3DDECL_END()
	};
	V(device->CreateVertexDeclaration(v, &vertex_decl));

	app::render->RegisterShader(this);
}

//=================================================================================================
void Gui::OnInit()
{
	effect = app::render->CompileShader("gui.fx");
	techGui = effect->GetTechniqueByName("gui");
	techGui2 = effect->GetTechniqueByName("gui2");
	techGuiGrayscale = effect->GetTechniqueByName("gui_grayscale");
	hGuiSize = effect->GetParameterByName(nullptr, "size");
	hGuiTex = effect->GetParameterByName(nullptr, "tex0");
	assert(techGui && techGui2 && techGuiGrayscale && hGuiSize && hGuiTex);
}

//=================================================================================================
void Gui::OnReset()
{
	if(effect)
		effect->OnLostDevice();
	SafeRelease(vb);
	SafeRelease(vb2);
	SafeRelease(tFontTarget);
}

//=================================================================================================
void Gui::OnReload()
{
	if(effect)
		effect->OnResetDevice();
	CreateVertexBuffer();
}

//=================================================================================================
void Gui::OnRelease()
{
	SafeRelease(effect);
}

//=================================================================================================
void Gui::SetText(cstring ok, cstring yes, cstring no, cstring cancel)
{
	txOk = ok;
	txYes = yes;
	txNo = no;
	txCancel = cancel;
}

//=================================================================================================
bool Gui::AddFont(cstring filename)
{
	cstring path = Format("data/fonts/%s", filename);
	int result = AddFontResourceExA(path, FR_PRIVATE, nullptr);
	if(result == 0)
	{
		Error("Failed to load font '%s' (%d)!", filename, GetLastError());
		return false;
	}
	else
	{
		Info("Added font resource '%s'.", filename);
		return true;
	}
}

//=================================================================================================
Font* Gui::CreateFont(cstring name, int size, int weight, int tex_size, int outline)
{
	assert(name && size > 0 && IsPow2(tex_size) && outline >= 0);

	string res_name = Format("%s;%d;%d;%d", name, size, weight, outline);
	Font* existing_font = app::res_mgr->TryGet<Font>(res_name);
	if(existing_font)
		return existing_font;

	// oblicz rozmiar czcionki
	HDC hdc = GetDC(nullptr);
	//							hack na skalowanie dpi
	int logic_size = -MulDiv(size, 96/*GetDeviceCaps(hdc, LOGPIXELSX)*/, 72);

	// stw�rz czcionk� directx
	FONT dx_font;
	HRESULT hr = D3DXCreateFont(device, logic_size, 0, weight, 0, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
		DEFAULT_QUALITY, PROOF_QUALITY | FF_DONTCARE, name, &dx_font);
	if(FAILED(hr))
	{
		ReleaseDC(nullptr, hdc);
		Error("Failed to create directx font (%s, size:%d, weight:%d, code:%d).", name, size, weight, hr);
		return nullptr;
	}

	// stw�rz czcionk� winapi
	HFONT font = ::CreateFontA(logic_size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
		CLIP_DEFAULT_PRECIS, PROOF_QUALITY, DEFAULT_PITCH | FF_DONTCARE, name);
	if(!font)
	{
		DWORD error = GetLastError();
		ReleaseDC(nullptr, hdc);
		dx_font->Release();
		Error("Failed to create font (%s, size:%d, weight:%d, code:%d).", name, size, weight, error);
		return nullptr;
	}

	// pobierz szeroko�ci znak�w i wysoko�� czcionki
	int glyph_w[256];
	HGDIOBJ prev = SelectObject(hdc, (HGDIOBJ)font);
	if(GetCharWidth32(hdc, 0, 255, glyph_w) == 0)
	{
		ABC abc[256];
		if(GetCharABCWidths(hdc, 0, 255, abc) == 0)
		{
			Error("Failed to get font glyphs (%s, size:%d, weight:%d, error:%d).", name, size, weight, GetLastError());
			SelectObject(hdc, prev);
			DeleteObject(font);
			ReleaseDC(nullptr, hdc);
			dx_font->Release();
			return nullptr;
		}
		for(int i = 0; i <= 255; ++i)
		{
			ABC& a = abc[i];
			glyph_w[i] = a.abcA + a.abcB + a.abcC;
		}
	}
	TEXTMETRIC tm;
	GetTextMetricsA(hdc, &tm);
	int height = tm.tmHeight;
	SelectObject(hdc, prev);
	DeleteObject(font);
	ReleaseDC(nullptr, hdc);

	// stw�rz czcionk�
	Font* f = new Font;
	int extra = outline + 1;

	// ustaw znaki
	Int2 offset(extra, extra);
	bool warn_once = true;

	for(int i = 32; i <= 255; ++i)
	{
		int sum = glyph_w[i];
		if(sum)
		{
			if(offset.x + sum >= tex_size - 3)
			{
				offset.x = extra;
				offset.y += height + extra;
				if(warn_once && offset.y + height > tex_size)
				{
					warn_once = false;
					Warn("Font %s (%d) it too large for texture %d.", name, size, tex_size);
				}
			}
			Font::Glyph& g = f->glyph[i];
			g.ok = true;
			g.uv.v1 = Vec2(float(offset.x) / tex_size, float(offset.y) / tex_size);
			g.uv.v2 = g.uv.v1 + Vec2(float(sum) / tex_size, float(height) / tex_size);
			g.width = sum;
			offset.x += sum + 2 + extra;
		}
		else
			f->glyph[i].ok = false;
	}

	// tab
	Font::Glyph& tab = f->glyph['\t'];
	tab.ok = true;
	tab.width = 32;
	tab.uv = f->glyph[' '].uv;

	f->height = height;
	f->outline_shift = float(outline) / tex_size;

	bool error = !CreateFontInternal(f, dx_font, tex_size, 0, outline);
	if(!error && outline)
		error = !CreateFontInternal(f, dx_font, tex_size, outline, outline);

	// zwolnij czcionk�
	SafeRelease(dx_font);

	// przywr�� render target
	SURFACE surf;
	V(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &surf));
	V(device->SetRenderTarget(0, surf));
	surf->Release();

	if(!error)
	{
		// zapisz wygenerowan� czcionk� do pliku
		/*D3DXSaveTextureToFile(Format("%s-%d.png", name, size), D3DXIFF_PNG, f->tex, nullptr);
		if(outline > 0)
			D3DXSaveTextureToFile(Format("%s-%d-outline.png", name, size), D3DXIFF_PNG, f->texOutline, nullptr);*/

		f->type = ResourceType::Font;
		f->state = ResourceState::Loaded;
		f->path = res_name;
		f->filename = f->path.c_str();
		app::res_mgr->AddResource(f);

		return f;
	}
	else
	{
		if(f->tex)
			f->tex->Release();
		if(f->texOutline)
			f->texOutline->Release();
		delete f;
		return nullptr;
	}
}

//=================================================================================================
bool Gui::CreateFontInternal(Font* font, ID3DXFont* dx_font, int tex_size, int outline, int max_outline)
{
	while(true)
	{
		int result = TryCreateFontInternal(font, dx_font, tex_size, outline, max_outline);
		if(result == 0)
			return true;
		else if(result == 1)
			return false;
	}
}

//=================================================================================================
// 0-ok, 1-failed, 2-retry
int Gui::TryCreateFontInternal(Font* font, ID3DXFont* dx_font, int tex_size, int outline, int max_outline)
{
	// stw�rz render target
	if(!tFontTarget || tex_size > max_tex_size)
	{
		SafeRelease(tFontTarget);
		HRESULT hr = device->CreateTexture(tex_size, tex_size, 0, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tFontTarget, nullptr);
		if(FAILED(hr))
		{
			Error("Failed to create font render target texture (size:%d, error:%d).", tex_size, hr);
			return 1;
		}
		max_tex_size = tex_size;
	}

	// rozpocznij renderowanie do tekstury
	SURFACE surf;
	V(tFontTarget->GetSurfaceLevel(0, &surf));
	V(device->SetRenderTarget(0, surf));
	V(device->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET, 0, 0, 0));
	V(device->BeginScene());
	V(sprite->Begin(D3DXSPRITE_ALPHABLEND));

	int extra = max_outline + 1;

	// renderuj do tekstury
	Int2 offset(extra, extra);
	char cbuf[2] = { 0,0 };
	Rect rect = Rect::Zero;

	if(outline)
	{
		for(int i = 32; i <= 255; ++i)
		{
			cbuf[0] = (char)i;
			const Font::Glyph& g = font->glyph[i];
			if(g.ok)
			{
				if(offset.x + g.width >= tex_size - 3)
				{
					offset.x = extra;
					offset.y += font->height + extra;
				}

				for(int j = 0; j < 8; ++j)
				{
					const float a = float(j)*PI / 4;
					rect.Left() = offset.x + int(outline*sin(a));
					rect.Top() = offset.y + int(outline*cos(a));
					dx_font->DrawTextA(sprite, cbuf, 1, (RECT*)&rect, DT_LEFT | DT_NOCLIP, Color::White.value);
				}

				offset.x += g.width + 2 + extra;
			}
		}
	}
	else
	{
		for(int i = 32; i <= 255; ++i)
		{
			cbuf[0] = (char)i;
			const Font::Glyph& g = font->glyph[i];
			if(g.ok)
			{
				if(offset.x + g.width >= tex_size - 3)
				{
					offset.x = extra;
					offset.y += font->height + extra;
				}
				rect.Left() = offset.x;
				rect.Top() = offset.y;
				dx_font->DrawTextA(sprite, cbuf, 1, (RECT*)&rect, DT_LEFT | DT_NOCLIP, Color::White.value);
				offset.x += g.width + 2 + extra;
			}
		}
	}

	// koniec renderowania
	V(sprite->End());
	V(device->EndScene());

	TEX tex;

	// stw�rz tekstur� na now� czcionk�
	HRESULT hr = device->CreateTexture(tex_size, tex_size, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
	if(FAILED(hr))
	{
		Error("Failed to create font texture (size: %d, error: %d).", tex_size, hr);
		return 1;
	}

	// kopiuj do nowej tekstury
	SURFACE out_surf;
	V(tex->GetSurfaceLevel(0, &out_surf));
	hr = D3DXLoadSurfaceFromSurface(out_surf, nullptr, nullptr, surf, nullptr, nullptr, D3DX_DEFAULT, 0);
	if(hr == D3DERR_DEVICELOST)
	{
		SURFACE backbuffer;
		V(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer));
		V(device->SetRenderTarget(0, backbuffer));
		backbuffer->Release();
		surf->Release();
		app::render->WaitReset();
		return 2;
	}
	else if(FAILED(hr))
	{
		Error("Failed to copy font to texture (size: %d, error: %d).", tex_size, hr);
		return 1;
	}
	surf->Release();
	out_surf->Release();

	if(outline)
		font->texOutline = tex;
	else
		font->tex = tex;

	return 0;
}

//=================================================================================================
// Draw text - rewritten from TFQ
bool Gui::DrawText(Font* font, Cstring str, uint flags, Color color, const Rect& rect, const Rect* clipping, vector<Hitbox>* hitboxes,
	int* hitbox_counter, const vector<TextLine>* lines)
{
	assert(font);

	uint line_begin, line_end, line_index = 0;
	int line_width, width = rect.SizeX();
	cstring text = str;
	uint text_end = strlen(str);
	Vec4 current_color = Color(color);
	Vec4 default_color = current_color;
	outline_alpha = current_color.w;
	const Vec2 scale(1, 1);

	bool outline = (IsSet(flags, DTF_OUTLINE) && font->texOutline);
	bool parse_special = IsSet(flags, DTF_PARSE_SPECIAL);
	bool bottom_clip = false;

	tCurrent = font->tex;
	if(outline)
		tCurrent2 = font->texOutline;

	HitboxContext* hc;
	if(hitboxes)
	{
		hc = &tmpHitboxContext;
		hc->hitbox = hitboxes;
		hc->counter = (hitbox_counter ? *hitbox_counter : 0);
		hc->open = HitboxOpen::No;
	}
	else
		hc = nullptr;

	Lock(outline);

	typedef void (Gui::*DrawLineF)(Font* font, cstring text, uint line_begin, uint line_end, const Vec4& def_color,
		Vec4& color, int x, int y, const Rect* clipping, HitboxContext* hc, bool parse_special, const Vec2& scale);
	DrawLineF call;
	if(outline)
		call = &Gui::DrawLineOutline;
	else
		call = &Gui::DrawLine;

#define CALL (this->*call)

	if(!IsSet(flags, DTF_VCENTER | DTF_BOTTOM))
	{
		int y = rect.Top();

		if(!lines)
		{
			// tekst pionowo po �rodku lub na dole
			while(font->SplitLine(line_begin, line_end, line_width, line_index, text, text_end, flags, width))
			{
				// pocz�tkowa pozycja x w tej linijce
				int x;
				if(IsSet(flags, DTF_CENTER))
					x = rect.Left() + (width - line_width) / 2;
				else if(IsSet(flags, DTF_RIGHT))
					x = rect.Right() - line_width;
				else
					x = rect.Left();

				int clip_result = (clipping ? Clip(x, y, line_width, font->height, clipping) : 0);

				// znaki w tej linijce
				if(clip_result == 0)
					CALL(font, text, line_begin, line_end, default_color, current_color, x, y, nullptr, hc, parse_special, scale);
				else if(clip_result == 5)
					CALL(font, text, line_begin, line_end, default_color, current_color, x, y, clipping, hc, parse_special, scale);
				else if(clip_result == 2)
				{
					// tekst jest pod widocznym regionem, przerwij rysowanie
					bottom_clip = true;
					break;
				}
				else
				{
					// pomi� hitbox
					SkipLine(text, line_begin, line_end, hc);
				}

				// zmie� y na kolejn� linijk�
				y += font->height;
			}
		}
		else
		{
			for(vector<TextLine>::const_iterator it = lines->begin(), end = lines->end(); it != end; ++it)
			{
				// pocz�tkowa pozycja x w tej linijce
				int x;
				if(IsSet(flags, DTF_CENTER))
					x = rect.Left() + (width - it->width) / 2;
				else if(IsSet(flags, DTF_RIGHT))
					x = rect.Right() - it->width;
				else
					x = rect.Left();

				int clip_result = (clipping ? Clip(x, y, it->width, font->height, clipping) : 0);

				// znaki w tej linijce
				if(clip_result == 0)
					CALL(font, text, it->begin, it->end, default_color, current_color, x, y, nullptr, hc, parse_special, scale);
				else if(clip_result == 5)
					CALL(font, text, it->begin, it->end, default_color, current_color, x, y, clipping, hc, parse_special, scale);
				else if(clip_result == 2)
				{
					// tekst jest pod widocznym regionem, przerwij rysowanie
					bottom_clip = true;
					break;
				}
				else
				{
					// pomi� hitbox
					SkipLine(text, it->begin, it->end, hc);
				}

				// zmie� y na kolejn� linijk�
				y += font->height;
			}
		}
	}
	else
	{
		// tekst u g�ry
		if(!lines)
		{
			static vector<TextLine> lines_data;
			lines_data.clear();

			// oblicz wszystkie linijki
			while(font->SplitLine(line_begin, line_end, line_width, line_index, text, text_end, flags, width))
				lines_data.push_back(TextLine(line_begin, line_end, line_width));

			lines = &lines_data;
		}

		// pocz�tkowa pozycja y
		int y;
		if(IsSet(flags, DTF_BOTTOM))
			y = rect.Bottom() - lines->size()*font->height;
		else
			y = rect.Top() + (rect.SizeY() - int(lines->size())*font->height) / 2;

		for(vector<TextLine>::const_iterator it = lines->begin(), end = lines->end(); it != end; ++it)
		{
			// pocz�tkowa pozycja x w tej linijce
			int x;
			if(IsSet(flags, DTF_CENTER))
				x = rect.Left() + (width - it->width) / 2;
			else if(IsSet(flags, DTF_RIGHT))
				x = rect.Right() - it->width;
			else
				x = rect.Left();

			int clip_result = (clipping ? Clip(x, y, it->width, font->height, clipping) : 0);

			// znaki w tej linijce
			if(clip_result == 0)
				CALL(font, text, it->begin, it->end, default_color, current_color, x, y, nullptr, hc, parse_special, scale);
			else if(clip_result == 5)
				CALL(font, text, it->begin, it->end, default_color, current_color, x, y, clipping, hc, parse_special, scale);
			else if(clip_result == 2)
			{
				// tekst jest pod widocznym regionem, przerwij rysowanie
				bottom_clip = true;
				break;
			}
			else if(hitboxes)
			{
				// pomi� hitbox
				SkipLine(text, it->begin, it->end, hc);
			}

			// zmie� y na kolejn� linijk�
			y += font->height;
		}
	}

	Flush();

	if(hitbox_counter)
		*hitbox_counter = hc->counter;

	return !bottom_clip;
}

//=================================================================================================
void Gui::DrawLine(Font* font, cstring text, uint line_begin, uint line_end, const Vec4& default_color, Vec4& current_color,
	int x, int y, const Rect* clipping, HitboxContext* hc, bool parse_special, const Vec2& scale)
{
	for(uint i = line_begin; i < line_end; ++i)
	{
		char c = text[i];
		if(c == '$' && parse_special)
		{
			++i;
			assert(i < line_end);
			c = text[i];
			if(c == 'c')
			{
				// kolor
				++i;
				assert(i < line_end);
				c = text[i];
				switch(c)
				{
				case '-':
					current_color = default_color;
					break;
				case 'r':
					current_color = Vec4(1, 0, 0, default_color.w);
					break;
				case 'g':
					current_color = Vec4(0, 1, 0, default_color.w);
					break;
				case 'b':
					current_color = Vec4(0, 0, 1, default_color.w);
					break;
				case 'y':
					current_color = Vec4(1, 1, 0, default_color.w);
					break;
				case 'w':
					current_color = Vec4(1, 1, 1, default_color.w);
					break;
				case 'k':
					current_color = Vec4(0, 0, 0, default_color.w);
					break;
				case '0':
					current_color = Vec4(0.5f, 1, 0, default_color.w);
					break;
				case '1':
					current_color = Vec4(1, 0.5f, 0, default_color.w);
					break;
				default:
					// nieznany kolor
					assert(0);
					break;
				}

				continue;
			}
			else if(c == 'h')
			{
				++i;
				assert(i < line_end);
				c = text[i];
				if(c == '+')
				{
					// rozpocznij hitbox
					if(hc)
					{
						assert(hc->open == HitboxOpen::No);
						hc->open = HitboxOpen::Yes;
						hc->region.Left() = INT_MAX;
					}
				}
				else if(c == '-')
				{
					// zamknij hitbox
					if(hc)
					{
						assert(hc->open == HitboxOpen::Yes);
						hc->open = HitboxOpen::No;
						if(hc->region.Left() != INT_MAX)
						{
							Hitbox& h = Add1(hc->hitbox);
							h.rect = hc->region;
							h.index = hc->counter;
							h.index2 = -1;
						}
						++hc->counter;
					}
				}
				else
				{
					// nieznana opcja hitboxa
					assert(0);
				}

				continue;
			}
			else if(c == 'g')
			{
				// group hitbox
				// $g+123[,123];
				// $g-
				++i;
				assert(i < line_end);
				c = text[i];
				if(c == '+')
				{
					// start group hitbox
					int index, index2;
					++i;
					assert(i < line_end);
					if(font->ParseGroupIndex(text, line_end, i, index, index2) && hc)
					{
						assert(hc->open == HitboxOpen::No);
						hc->open = HitboxOpen::Group;
						hc->region.Left() = INT_MAX;
						hc->group_index = index;
						hc->group_index2 = index2;
					}
				}
				else if(c == '-')
				{
					// close group hitbox
					if(hc)
					{
						assert(hc->open == HitboxOpen::Group);
						hc->open = HitboxOpen::No;
						if(hc->region.Left() != INT_MAX)
						{
							Hitbox& h = Add1(hc->hitbox);
							h.rect = hc->region;
							h.index = hc->group_index;
							h.index2 = hc->group_index2;
						}
					}
				}
				else
				{
					// invalid format
					assert(0);
				}

				continue;
			}
			else if(c == '$')
			{
				// dwa znaki $$ to $
			}
			else
			{
				// nieznana opcja
				assert(0);
			}
		}

		Font::Glyph& g = font->glyph[byte(c)];
		Int2 glyph_size = Int2(g.width, font->height) * scale;

		int clip_result = (clipping ? Clip(x, y, glyph_size.x, glyph_size.y, clipping) : 0);

		if(clip_result == 0)
		{
			// dodaj znak do bufora
			v->pos = Vec3(float(x), float(y), 0);
			v->color = current_color;
			v->tex = g.uv.LeftTop();
			++v;

			v->pos = Vec3(float(x + glyph_size.x), float(y), 0);
			v->color = current_color;
			v->tex = g.uv.RightTop();
			++v;

			v->pos = Vec3(float(x), float(y + glyph_size.y), 0);
			v->color = current_color;
			v->tex = g.uv.LeftBottom();
			++v;

			v->pos = Vec3(float(x + glyph_size.x), float(y), 0);
			v->color = current_color;
			v->tex = g.uv.RightTop();
			++v;

			v->pos = Vec3(float(x + glyph_size.x), float(y + glyph_size.y), 0);
			v->color = current_color;
			v->tex = g.uv.RightBottom();
			++v;

			v->pos = Vec3(float(x), float(y + glyph_size.y), 0);
			v->color = current_color;
			v->tex = g.uv.LeftBottom();
			++v;

			if(hc && hc->open != HitboxOpen::No)
			{
				Rect r_clip = Rect::Create(Int2(x, y), glyph_size);
				if(hc->region.Left() == INT_MAX)
					hc->region = r_clip;
				else
					hc->region.Resize(r_clip);
			}

			++in_buffer;
		}
		else if(clip_result == 5)
		{
			// przytnij znak
			Box2d orig_pos = Box2d::Create(Int2(x, y), glyph_size);
			Box2d clip_pos(float(max(x, clipping->Left())), float(max(y, clipping->Top())),
				float(min(x + glyph_size.x, clipping->Right())), float(min(y + glyph_size.y, clipping->Bottom())));
			Vec2 orig_size = orig_pos.Size();
			Vec2 clip_size = clip_pos.Size();
			Vec2 s(clip_size.x / orig_size.x, clip_size.y / orig_size.y);
			Vec2 shift = clip_pos.v1 - orig_pos.v1;
			shift.x /= orig_size.x;
			shift.y /= orig_size.y;
			Vec2 uv_size = g.uv.Size();
			Box2d clip_uv(g.uv.v1 + Vec2(shift.x*uv_size.x, shift.y*uv_size.y));
			clip_uv.v2 += Vec2(uv_size.x*s.x, uv_size.y*s.y);

			// dodaj znak do bufora
			v->pos = clip_pos.LeftTop().XY();
			v->color = current_color;
			v->tex = clip_uv.LeftTop();
			++v;

			v->pos = clip_pos.RightTop().XY();
			v->color = current_color;
			v->tex = clip_uv.RightTop();
			++v;

			v->pos = clip_pos.LeftBottom().XY();
			v->color = current_color;
			v->tex = clip_uv.LeftBottom();
			++v;

			v->pos = clip_pos.RightTop().XY();
			v->color = current_color;
			v->tex = clip_uv.RightTop();
			++v;

			v->pos = clip_pos.RightBottom().XY();
			v->color = current_color;
			v->tex = clip_uv.RightBottom();
			++v;

			v->pos = clip_pos.LeftBottom().XY();
			v->color = current_color;
			v->tex = clip_uv.LeftBottom();
			++v;

			if(hc && hc->open != HitboxOpen::No)
			{
				Rect r_clip(clip_pos);
				if(hc->region.Left() == INT_MAX)
					hc->region = r_clip;
				else
					hc->region.Resize(r_clip);
			}

			++in_buffer;
		}
		else if(clip_result == 3)
		{
			// tekst jest ju� poza regionem z prawej, mo�na przerwa�
			break;
		}

		x += glyph_size.x;
		if(in_buffer == 256)
			Flush(true);
	}

	// zamknij region
	if(hc && hc->open != HitboxOpen::No && hc->region.Left() != INT_MAX)
	{
		Hitbox& h = Add1(hc->hitbox);
		h.rect = hc->region;
		if(hc->open == HitboxOpen::Yes)
			h.index = hc->counter;
		else
		{
			h.index = hc->group_index;
			h.index2 = hc->group_index2;
		}
		hc->region.Left() = INT_MAX;
	}
}

//=================================================================================================
void Gui::DrawLineOutline(Font* font, cstring text, uint line_begin, uint line_end, const Vec4& default_color, Vec4& current_color,
	int x, int y, const Rect* clipping, HitboxContext* hc, bool parse_special, const Vec2& scale)
{
	// scale is TODO here
	assert(scale == Vec2(1, 1));

	Vec4 col(0, 0, 0, outline_alpha);
	int prev_x = x, prev_y = y;

	// przesuni�cie glifu przez outline
	const float osh = font->outline_shift;

	for(uint i = line_begin; i < line_end; ++i)
	{
		char c = text[i];
		if(c == '$' && parse_special)
		{
			++i;
			assert(i < line_end);
			c = text[i];
			if(c == 'c')
			{
				// kolor
				++i;
				assert(i < line_end);
				continue;
			}
			else if(c == 'h')
			{
				++i;
				assert(i < line_end);
				continue;
			}
			else if(c == 'g')
			{
				++i;
				assert(i < line_end);
				c = text[i];
				if(c == '+')
				{
					++i;
					assert(i < line_end);
					int tmp;
					font->ParseGroupIndex(text, line_end, i, tmp, tmp);
				}
				else if(c == '-')
					continue;
				else
				{
					// invalid group format
					assert(0);
				}
			}
			else if(c == '$')
			{
				// dwa znaki $$ to $
			}
			else
			{
				// nieznana opcja
				assert(0);
			}
		}

		Font::Glyph& g = font->glyph[byte(c)];

		int clip_result = (clipping ? Clip(x, y, g.width, font->height, clipping) : 0);

		if(clip_result == 0)
		{
			// dodaj znak do bufora
			v2->pos = Vec3(float(x) - osh, float(y) - osh, 0);
			v2->color = col;
			v2->tex = g.uv.LeftTop();
			++v2;

			v2->pos = Vec3(float(x + g.width) + osh, float(y) - osh, 0);
			v2->color = col;
			v2->tex = g.uv.RightTop();
			++v2;

			v2->pos = Vec3(float(x) - osh, float(y + font->height) + osh, 0);
			v2->color = col;
			v2->tex = g.uv.LeftBottom();
			++v2;

			v2->pos = Vec3(float(x + g.width) + osh, float(y) - osh, 0);
			v2->color = col;
			v2->tex = g.uv.RightTop();
			++v2;

			v2->pos = Vec3(float(x + g.width) + osh, float(y + font->height) + osh, 0);
			v2->color = col;
			v2->tex = g.uv.RightBottom();
			++v2;

			v2->pos = Vec3(float(x) - osh, float(y + font->height) + osh, 0);
			v2->color = col;
			v2->tex = g.uv.LeftBottom();
			++v2;

			if(hc && hc->open != HitboxOpen::No)
			{
				Rect r_clip(x, y, x + g.width, y + font->height);
				if(hc->region.Left() == INT_MAX)
					hc->region = r_clip;
				else
					hc->region.Resize(r_clip);
			}

			++in_buffer2;
			x += g.width;
		}
		else if(clip_result == 5)
		{
			// przytnij znak
			Box2d orig_pos(float(x) - osh, float(y) - osh, float(x + g.width) + osh, float(y + font->height) + osh);
			Box2d clip_pos(float(max(x, clipping->Left())), float(max(y, clipping->Top())),
				float(min(x + g.width, clipping->Right())), float(min(y + font->height, clipping->Bottom())));
			Vec2 orig_size = orig_pos.Size();
			Vec2 clip_size = clip_pos.Size();
			Vec2 s(clip_size.x / orig_size.x, clip_size.y / orig_size.y);
			Vec2 shift = clip_pos.v1 - orig_pos.v1;
			shift.x /= orig_size.x;
			shift.y /= orig_size.y;
			Vec2 uv_size = g.uv.Size();
			Box2d clip_uv(g.uv.v1 + Vec2(shift.x*uv_size.x, shift.y*uv_size.y));
			clip_uv.v2 += Vec2(uv_size.x*s.x, uv_size.y*s.y);

			// dodaj znak do bufora
			v2->pos = clip_pos.LeftTop().XY();
			v2->color = col;
			v2->tex = clip_uv.LeftTop();
			++v2;

			v2->pos = clip_pos.RightTop().XY();
			v2->color = col;
			v2->tex = clip_uv.RightTop();
			++v2;

			v2->pos = clip_pos.LeftBottom().XY();
			v2->color = col;
			v2->tex = clip_uv.LeftBottom();
			++v2;

			v2->pos = clip_pos.RightTop().XY();
			v2->color = col;
			v2->tex = clip_uv.RightTop();
			++v2;

			v2->pos = clip_pos.RightBottom().XY();
			v2->color = col;
			v2->tex = clip_uv.RightBottom();
			++v2;

			v2->pos = clip_pos.LeftBottom().XY();
			v2->color = col;
			v2->tex = clip_uv.LeftBottom();
			++v2;

			if(hc && hc->open != HitboxOpen::No)
			{
				Rect r_clip(clip_pos);
				if(hc->region.Left() == INT_MAX)
					hc->region = r_clip;
				else
					hc->region.Resize(r_clip);
			}

			++in_buffer2;
			x += g.width;
		}
		else if(clip_result == 3)
		{
			// tekst jest ju� poza regionem z prawej, mo�na przerwa�
			break;
		}
		else
			x += g.width;

		if(in_buffer2 == 256)
			Flush(true);
	}

	DrawLine(font, text, line_begin, line_end, default_color, current_color, prev_x, prev_y, clipping, hc, parse_special, scale);
}

//=================================================================================================
void Gui::Lock(bool outline)
{
	V(vb->Lock(0, 0, (void**)&v, D3DLOCK_DISCARD));
	in_buffer = 0;

	if(outline)
	{
		V(vb2->Lock(0, 0, (void**)&v2, D3DLOCK_DISCARD));
		in_buffer2 = 0;
		vb2_locked = true;
	}
	else
		vb2_locked = false;
}

//=================================================================================================
void Gui::Flush(bool lock)
{
	if(vb2_locked)
	{
		// odblokuj drugi bufor
		V(vb2->Unlock());

		// rysuj o ile jest co
		if(in_buffer2)
		{
			// ustaw tekstur�
			if(tCurrent2 != tSet)
			{
				tSet = tCurrent2;
				V(effect->SetTexture(hGuiTex, tSet));
				V(effect->CommitChanges());
			}

			// rysuj
			V(device->SetStreamSource(0, vb2, 0, sizeof(VParticle)));
			V(device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, in_buffer * 2));
		}
	}

	V(vb->Unlock());

	if(in_buffer)
	{
		// ustaw tekstur�
		if(tCurrent != tSet)
		{
			tSet = tCurrent;
			V(effect->SetTexture(hGuiTex, tSet));
			V(effect->CommitChanges());
		}

		// rysuj
		V(device->SetStreamSource(0, vb, 0, sizeof(VParticle)));
		V(device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, in_buffer * 2));
	}

	if(lock)
		Lock(vb2_locked);
	else
		vb2_locked = false;
}

//=================================================================================================
void Gui::Draw(bool draw_layers, bool draw_dialogs)
{
	PROFILER_BLOCK("DrawGui");

	wnd_size = app::engine->GetWindowSize();

	if(!draw_layers && !draw_dialogs)
		return;

	app::render->SetAlphaTest(false);
	app::render->SetAlphaBlend(true);
	app::render->SetNoCulling(true);
	app::render->SetNoZWrite(false);

	V(device->SetVertexDeclaration(vertex_decl));

	tSet = nullptr;
	tCurrent = nullptr;
	tCurrent2 = nullptr;

	uint passes;

	V(effect->SetTechnique(techGui));
	Vec4 wnd_s(float(wnd_size.x), float(wnd_size.y), 0, 0);
	V(effect->SetVector(hGuiSize, (D3DXVECTOR4*)&wnd_s));
	V(effect->Begin(&passes, 0));
	V(effect->BeginPass(0));

	// rysowanie
	if(draw_layers)
		layer->Draw();
	if(draw_dialogs)
		dialog_layer->Draw();

	// draw cursor
	if(NeedCursor())
	{
		Int2 pos = cursor_pos;
		if(cursor_mode == CURSOR_TEXT)
			pos -= Int2(3, 8);
		DrawSprite(layout->cursor[cursor_mode], pos);
	}

	V(effect->EndPass());
	V(effect->End());
}

//=================================================================================================
void Gui::Add(Control* ctrl)
{
	layer->Add(ctrl);
}

//=================================================================================================
void Gui::DrawItem(Texture* t, const Int2& item_pos, const Int2& item_size, Color color, int corner, int size, const Box2d* clip_rect)
{
	assert(t && t->IsLoaded());

	GuiRect gui_rect;
	gui_rect.Set(item_pos, item_size);

	bool require_clip = false;
	if(clip_rect)
	{
		int result = gui_rect.RequireClip(*clip_rect);
		if(result == -1)
			return;
		else if(result == 1)
			require_clip = true;
	}

	if(item_size.x < corner || item_size.y < corner)
	{
		if(item_size.x == 0 || item_size.y == 0)
			return;

		Rect r = { item_pos.x, item_pos.y, item_pos.x + item_size.x, item_pos.y + item_size.y };
		assert(!clip_rect);
		DrawSpriteRect(t, r, color);
		return;
	}

	tCurrent = t->tex;
	Lock();

	Vec4 col = Color(color);

	/*
		0---1----------2---3
		| 1 |     2    | 3 |
		|   |          |   |
		4---5----------6---7
		|   |          |   |
		| 4 |     5    | 6 |
		|   |          |   |
		|   |          |   |
		8---9---------10--11
		|   |          |   |
		| 7 |     8    | 9 |
		12-13---------14--15
	*/

	// top left & bottom right indices for each rectangle
	int ids[9 * 2] = {
		0, 5,
		1, 6,
		2, 7,
		4, 9,
		5, 10,
		6, 11,
		8, 13,
		9, 14,
		10, 15
	};

	Vec2 ipos[16] = {
		Vec2(float(item_pos.x), float(item_pos.y)),
		Vec2(float(item_pos.x + corner), float(item_pos.y)),
		Vec2(float(item_pos.x + item_size.x - corner), float(item_pos.y)),
		Vec2(float(item_pos.x + item_size.x), float(item_pos.y)),

		Vec2(float(item_pos.x), float(item_pos.y + corner)),
		Vec2(float(item_pos.x + corner), float(item_pos.y + corner)),
		Vec2(float(item_pos.x + item_size.x - corner), float(item_pos.y + corner)),
		Vec2(float(item_pos.x + item_size.x), float(item_pos.y + corner)),

		Vec2(float(item_pos.x), float(item_pos.y + item_size.y - corner)),
		Vec2(float(item_pos.x + corner), float(item_pos.y + item_size.y - corner)),
		Vec2(float(item_pos.x + item_size.x - corner), float(item_pos.y + item_size.y - corner)),
		Vec2(float(item_pos.x + item_size.x), float(item_pos.y + item_size.y - corner)),

		Vec2(float(item_pos.x), float(item_pos.y + item_size.y)),
		Vec2(float(item_pos.x + corner), float(item_pos.y + item_size.y)),
		Vec2(float(item_pos.x + item_size.x - corner), float(item_pos.y + item_size.y)),
		Vec2(float(item_pos.x + item_size.x), float(item_pos.y + item_size.y)),
	};

	Vec2 itex[16] = {
		Vec2(0,0),
		Vec2(float(corner) / size,0),
		Vec2(float(size - corner) / size,0),
		Vec2(1,0),

		Vec2(0,float(corner) / size),
		Vec2(float(corner) / size,float(corner) / size),
		Vec2(float(size - corner) / size,float(corner) / size),
		Vec2(1,float(corner) / size),

		Vec2(0,float(size - corner) / size),
		Vec2(float(corner) / size,float(size - corner) / size),
		Vec2(float(size - corner) / size,float(size - corner) / size),
		Vec2(1,float(size - corner) / size),

		Vec2(0,1),
		Vec2(float(corner) / size,1),
		Vec2(float(size - corner) / size,1),
		Vec2(1,1),
	};

	if(require_clip)
	{
		in_buffer = 0;

		for(int i = 0; i < 9; ++i)
		{
			int index1 = ids[i * 2 + 0];
			int index2 = ids[i * 2 + 1];
			gui_rect.Set(ipos[index1], ipos[index2], itex[index1], itex[index2]);
			if(gui_rect.Clip(*clip_rect))
			{
				gui_rect.Populate(v, col);
				++in_buffer;
			}
		}

		assert(in_buffer > 0);
		Flush();
	}
	else
	{
		for(int i = 0; i < 9; ++i)
		{
			int index1 = ids[i * 2 + 0];
			int index2 = ids[i * 2 + 1];
			gui_rect.Set(ipos[index1], ipos[index2], itex[index1], itex[index2]);
			gui_rect.Populate(v, col);
		}

		in_buffer = 9;
		Flush();
	}
}

//=================================================================================================
void Gui::Update(float dt, float mouse_speed)
{
	PROFILER_BLOCK("UpdateGui");

	// update cursor
	cursor_mode = CURSOR_NORMAL;
	mouse_wheel = app::input->GetMouseWheel();
	prev_cursor_pos = cursor_pos;
	if(NeedCursor() && mouse_speed > 0)
	{
		cursor_pos += app::input->GetMouseDif() * mouse_speed;
		if(cursor_pos.x < 0)
			cursor_pos.x = 0;
		if(cursor_pos.y < 0)
			cursor_pos.y = 0;
		if(cursor_pos.x >= wnd_size.x)
			cursor_pos.x = wnd_size.x - 1;
		if(cursor_pos.y >= wnd_size.y)
			cursor_pos.y = wnd_size.y - 1;
		app::engine->SetUnlockPoint(cursor_pos);
	}
	else
		app::engine->SetUnlockPoint(wnd_size / 2);

	layer->focus = dialog_layer->Empty();

	if(focused_ctrl)
	{
		if(!focused_ctrl->visible)
			focused_ctrl = nullptr;
		else if(dialog_layer->Empty())
		{
			layer->dont_focus = true;
			layer->Update(dt);
			layer->dont_focus = false;
		}
		else
		{
			focused_ctrl->LostFocus();
			focused_ctrl = nullptr;
		}
	}

	if(!focused_ctrl)
	{
		dialog_layer->focus = true;
		dialog_layer->Update(dt);
		layer->Update(dt);
	}

	app::engine->SetUnlockPoint(wnd_size / 2);
}

//=================================================================================================
void Gui::DrawSprite(Texture* t, const Int2& pos, Color color, const Rect* clipping)
{
	assert(t && t->IsLoaded());

	Int2 size = t->GetSize();

	int clip_result = (clipping ? Clip(pos.x, pos.y, size.x, size.y, clipping) : 0);
	if(clip_result > 0 && clip_result < 5)
		return;

	tCurrent = t->tex;
	Lock();

	Vec4 col = Color(color);

	if(clip_result == 0)
	{
		v->pos = Vec3(float(pos.x), float(pos.y), 0);
		v->color = col;
		v->tex = Vec2(0, 0);
		++v;

		v->pos = Vec3(float(pos.x + size.x), float(pos.y), 0);
		v->color = col;
		v->tex = Vec2(1, 0);
		++v;

		v->pos = Vec3(float(pos.x), float(pos.y + size.y), 0);
		v->color = col;
		v->tex = Vec2(0, 1);
		++v;

		v->pos = Vec3(float(pos.x), float(pos.y + size.y), 0);
		v->color = col;
		v->tex = Vec2(0, 1);
		++v;

		v->pos = Vec3(float(pos.x + size.x), float(pos.y), 0);
		v->color = col;
		v->tex = Vec2(1, 0);
		++v;

		v->pos = Vec3(float(pos.x + size.x), float(pos.y + size.y), 0);
		v->color = col;
		v->tex = Vec2(1, 1);
		++v;

		in_buffer = 1;
		Flush();
	}
	else
	{
		Box2d orig_pos(float(pos.x), float(pos.y), float(pos.x + size.x), float(pos.y + size.y));
		Box2d clip_pos(float(max(pos.x, clipping->Left())), float(max(pos.y, clipping->Top())),
			float(min(pos.x + (int)size.x, clipping->Right())), float(min(pos.y + (int)size.y, clipping->Bottom())));
		Vec2 orig_size = orig_pos.Size();
		Vec2 clip_size = clip_pos.Size();
		Vec2 s(clip_size.x / orig_size.x, clip_size.y / orig_size.y);
		Vec2 shift = clip_pos.v1 - orig_pos.v1;
		shift.x /= orig_size.x;
		shift.y /= orig_size.y;
		Box2d clip_uv(Vec2(shift.x, shift.y));
		clip_uv.v2 += Vec2(s.x, s.y);

		v->pos = clip_pos.LeftTop().XY();
		v->color = col;
		v->tex = clip_uv.LeftTop();
		++v;

		v->pos = clip_pos.RightTop().XY();
		v->color = col;
		v->tex = clip_uv.RightTop();
		++v;

		v->pos = clip_pos.LeftBottom().XY();
		v->color = col;
		v->tex = clip_uv.LeftBottom();
		++v;

		v->pos = clip_pos.LeftBottom().XY();
		v->color = col;
		v->tex = clip_uv.LeftBottom();
		++v;

		v->pos = clip_pos.RightTop().XY();
		v->color = col;
		v->tex = clip_uv.RightTop();
		++v;

		v->pos = clip_pos.RightBottom().XY();
		v->color = col;
		v->tex = clip_uv.RightBottom();
		++v;

		in_buffer = 1;
		Flush();
	}
}

//=================================================================================================
void Gui::OnClean()
{
	OnReset();

	delete layer;
	delete dialog_layer;
}

//=================================================================================================
void Gui::CreateVertexBuffer()
{
	if(device)
	{
		V(device->CreateVertexBuffer(sizeof(VParticle) * 6 * 256, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 0, D3DPOOL_DEFAULT, &vb, nullptr));
		V(device->CreateVertexBuffer(sizeof(VParticle) * 6 * 256, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 0, D3DPOOL_DEFAULT, &vb2, nullptr));
	}
}

//=================================================================================================
/*
Przycinanie tekstu do wybranego regionu, zwraca:
0 - tekst w ca�o�ci w regionie
1 - tekst nad regionem
2 - tekst pod regionem
3 - tekst poza regionem z prawej
4 - tekst poza regionem z lewej
5 - wymaga cz�ciowego clippingu, cz�ciowo w regionie
*/
int Gui::Clip(int x, int y, int w, int h, const Rect* clipping)
{
	if(x >= clipping->Left() && y >= clipping->Top() && x + w < clipping->Right() && y + h < clipping->Bottom())
	{
		// tekst w ca�o�ci w regionie
		return 0;
	}
	else if(y + h < clipping->Top())
	{
		// tekst nad regionem
		return 1;
	}
	else if(y > clipping->Bottom())
	{
		// tekst pod regionem
		return 2;
	}
	else if(x > clipping->Right())
	{
		// tekst poza regionem z prawej
		return 3;
	}
	else if(x + w < clipping->Left())
	{
		// tekst poza regionem z lewej
		return 4;
	}
	else
	{
		// wymaga cz�ciowego clippingu
		return 5;
	}
}

//=================================================================================================
void Gui::SkipLine(cstring text, uint line_begin, uint line_end, HitboxContext* hc)
{
	for(uint i = line_begin; i < line_end; ++i)
	{
		char c = text[i];
		if(c == '$')
		{
			++i;
			c = text[i];
			if(c == 'h')
			{
				++i;
				c = text[i];
				if(c == '+')
				{
					assert(hc->open == HitboxOpen::No);
					hc->open = HitboxOpen::Yes;
				}
				else if(c == '-')
				{
					assert(hc->open == HitboxOpen::Yes);
					hc->open = HitboxOpen::No;
					++hc->counter;
				}
				else
					assert(0);
			}
			else if(c == 'g')
			{
				++i;
				c = text[i];
				if(c == '+')
				{
					assert(hc->open == HitboxOpen::No);
					hc->open = HitboxOpen::Group;
					int tmp;
					++i;
					Font::ParseGroupIndex(text, line_end, i, tmp, tmp);
				}
				else if(c == '-')
				{
					assert(hc->open == HitboxOpen::Group);
					hc->open = HitboxOpen::No;
				}
				else
					assert(0);
			}
		}
	}
}

//=================================================================================================
DialogBox* Gui::ShowDialog(const DialogInfo& info)
{
	assert(!(info.have_tick && info.img)); // not allowed together

	DialogBox* d;
	int extra_limit = 0;
	Int2 min_size(0, 0);

	// create dialog
	if(info.have_tick)
		d = new DialogWithCheckbox(info);
	else if(info.img)
	{
		DialogWithImage* dwi = new DialogWithImage(info);
		Int2 size = dwi->GetImageSize();
		extra_limit = size.x + 8;
		min_size.y = size.y;
		d = dwi;
	}
	else
		d = new DialogBox(info);
	created_dialogs.push_back(d);

	// calculate size
	Font* font = d->layout->font;
	Int2 text_size;
	if(!info.auto_wrap)
		text_size = font->CalculateSize(info.text);
	else
		text_size = font->CalculateSizeWrap(info.text, wnd_size, 24 + 32 + extra_limit);
	d->size = text_size + Int2(24 + extra_limit, 24 + max(0, min_size.y - text_size.y));

	// set buttons
	if(info.type == DIALOG_OK)
	{
		Button& bt = Add1(d->bts);
		bt.text = txOk;
		bt.id = GuiEvent_Custom + BUTTON_OK;
		bt.size = font->CalculateSize(bt.text) + Int2(24, 24);
		bt.parent = d;

		min_size.x = bt.size.x + 24;
	}
	else
	{
		d->bts.resize(2);
		Button& bt1 = d->bts[0],
			&bt2 = d->bts[1];

		if(info.custom_names)
		{
			bt1.text = (info.custom_names[0] ? info.custom_names[0] : txYes);
			bt2.text = (info.custom_names[1] ? info.custom_names[1] : txNo);
		}
		else
		{
			bt1.text = txYes;
			bt2.text = txNo;
		}

		bt1.id = GuiEvent_Custom + BUTTON_YES;
		bt1.size = font->CalculateSize(bt1.text) + Int2(24, 24);
		bt1.parent = d;

		bt2.id = GuiEvent_Custom + BUTTON_NO;
		bt2.size = font->CalculateSize(bt2.text) + Int2(24, 24);
		bt2.parent = d;

		bt1.size = bt2.size = Int2::Max(bt1.size, bt2.size);
		min_size.x = bt1.size.x * 2 + 24 + 16;
	}

	// powi�ksz rozmiar okna o przyciski
	if(d->size.x < min_size.x)
		d->size.x = min_size.x;
	d->size.y += d->bts[0].size.y + 8;

	// checkbox
	if(info.have_tick)
	{
		d->size.y += 32;
		DialogWithCheckbox* dwc = static_cast<DialogWithCheckbox*>(d);
		dwc->checkbox.bt_size = Int2(32, 32);
		dwc->checkbox.checked = info.ticked;
		dwc->checkbox.id = GuiEvent_Custom + BUTTON_CHECKED;
		dwc->checkbox.parent = dwc;
		dwc->checkbox.text = info.tick_text;
		dwc->checkbox.pos = Int2(12, 40);
		dwc->checkbox.size = Int2(d->size.x - 24, 32);
	}

	// ustaw przyciski
	if(d->bts.size() == 1)
	{
		Button& bt = d->bts[0];
		bt.pos.x = (d->size.x - bt.size.x) / 2;
		bt.pos.y = d->size.y - 8 - bt.size.y;
	}
	else
	{
		Button& bt1 = d->bts[0],
			&bt2 = d->bts[1];
		bt1.pos.y = bt2.pos.y = d->size.y - 8 - bt1.size.y;
		bt1.pos.x = 12;
		bt2.pos.x = d->size.x - bt2.size.x - 12;
	}

	// dodaj
	d->need_delete = true;
	d->Setup(text_size);
	ShowDialog(d);

	return d;
}

//=================================================================================================
void Gui::ShowDialog(DialogBox* d)
{
	d->visible = true;
	d->Event(GuiEvent_Show);

	if(dialog_layer->Empty())
	{
		// nie ma �adnych innych dialog�w, aktywuj
		dialog_layer->Add(d);
		d->focus = true;
		d->Event(GuiEvent_GainFocus);
	}
	else if(d->order == ORDER_TOPMOST)
	{
		// dezaktywuj aktualny i aktywuj nowy
		Control* prev_d = dialog_layer->Top();
		prev_d->focus = false;
		prev_d->Event(GuiEvent_LostFocus);
		dialog_layer->Add(d);
		d->focus = true;
		d->Event(GuiEvent_GainFocus);
	}
	else
	{
		// szukaj pierwszego dialogu kt�ry jest wy�ej ni� ten
		DialogOrder above_order = DialogOrder(d->order + 1);
		vector<DialogBox*>& ctrls = (vector<DialogBox*>&)dialog_layer->GetControls();
		vector<DialogBox*>::iterator first_above = ctrls.end();
		for(vector<DialogBox*>::iterator it = ctrls.begin(), end = ctrls.end(); it != end; ++it)
		{
			if((*it)->order >= above_order)
			{
				first_above = it;
				break;
			}
		}

		if(first_above == ctrls.end())
		{
			// brak nadrz�dnego dialogu, dezaktywuj aktualny i aktywuj nowy
			Control* prev_d = dialog_layer->Top();
			prev_d->focus = false;
			prev_d->Event(GuiEvent_LostFocus);
			dialog_layer->Add(d);
			d->focus = true;
			d->Event(GuiEvent_GainFocus);
		}
		else
		{
			// jest nadrz�dny dialog, dodaj przed nim i nie aktywuj
			ctrls.insert(first_above, d);
		}
	}

	dialog_layer->inside_loop = false;
}

//=================================================================================================
bool Gui::CloseDialog(DialogBox* d)
{
	assert(d);

	if(dialog_layer->Empty() || !HaveDialog(d))
		return false;

	Control* prev_top = dialog_layer->Top();
	CloseDialogInternal(d);
	if(!dialog_layer->Empty() && prev_top != dialog_layer->Top())
	{
		Control* next_d = dialog_layer->Top();
		next_d->focus = true;
		next_d->Event(GuiEvent_GainFocus);
	}

	return true;
}

//=================================================================================================
void Gui::CloseDialogInternal(DialogBox* d)
{
	assert(d);

	d->Event(GuiEvent_Close);
	d->visible = false;
	dialog_layer->Remove(d);

	if(!dialog_layer->Empty())
	{
		vector<DialogBox*>& dialogs = (vector<DialogBox*>&)dialog_layer->GetControls();
		static vector<DialogBox*> to_remove;
		for(vector<DialogBox*>::iterator it = dialogs.begin(), end = dialogs.end(); it != end; ++it)
		{
			if((*it)->parent == d)
				to_remove.push_back(*it);
		}
		if(!to_remove.empty())
		{
			for(vector<DialogBox*>::iterator it = to_remove.begin(), end = to_remove.end(); it != end; ++it)
				CloseDialogInternal(*it);
			to_remove.clear();
		}
	}

	if(d->need_delete)
	{
		RemoveElement(created_dialogs, d);
		delete d;
	}
}

//=================================================================================================
bool Gui::HaveTopDialog(cstring name) const
{
	assert(name);

	if(dialog_layer->Empty())
		return false;

	DialogBox* d = static_cast<DialogBox*>(dialog_layer->Top());
	return d->name == name;
}

//=================================================================================================
bool Gui::HaveDialog() const
{
	return !dialog_layer->Empty();
}

//=================================================================================================
void Gui::DrawSpriteFull(Texture* t, const Color color)
{
	assert(t && t->IsLoaded());

	tCurrent = t->tex;
	Lock();

	Vec4 col = Color(color);

	v->pos = Vec3(0, 0, 0);
	v->color = col;
	v->tex = Vec2(0, 0);
	++v;

	v->pos = Vec3(float(wnd_size.x), 0, 0);
	v->color = col;
	v->tex = Vec2(1, 0);
	++v;

	v->pos = Vec3(0, float(wnd_size.y), 0);
	v->color = col;
	v->tex = Vec2(0, 1);
	++v;

	v->pos = Vec3(0, float(wnd_size.y), 0);
	v->color = col;
	v->tex = Vec2(0, 1);
	++v;

	v->pos = Vec3(float(wnd_size.x), 0, 0);
	v->color = col;
	v->tex = Vec2(1, 0);
	++v;

	v->pos = Vec3(float(wnd_size.x), float(wnd_size.y), 0);
	v->color = col;
	v->tex = Vec2(1, 1);
	++v;

	in_buffer = 1;
	Flush();
}

//=================================================================================================
void Gui::OnChar(char c)
{
	if((c != (char)Key::Backspace && c != (char)Key::Enter && byte(c) < 0x20) || c == '`')
		return;

	for(vector<OnCharHandler*>::iterator it = on_char.begin(), end = on_char.end(); it != end; ++it)
	{
		Control* ctrl = dynamic_cast<Control*>(*it);
		if(ctrl->visible)
			(*it)->OnChar(c);
	}
}

//=================================================================================================
void Gui::SimpleDialog(cstring text, Control* parent, cstring name)
{
	DialogInfo di;
	di.event = nullptr;
	di.name = name;
	di.parent = parent;
	di.pause = false;
	di.text = text;
	di.order = ORDER_NORMAL;
	di.type = DIALOG_OK;

	if(parent)
	{
		DialogBox* d = dynamic_cast<DialogBox*>(parent);
		if(d)
			di.order = d->order;
	}

	ShowDialog(di);
}

//=================================================================================================
void Gui::DrawSpriteRect(Texture* t, const Rect& rect, Color color)
{
	assert(t && t->IsLoaded());

	tCurrent = t->tex;
	Lock();

	Vec4 col = Color(color);

	v->pos = Vec3(float(rect.Left()), float(rect.Top()), 0);
	v->color = col;
	v->tex = Vec2(0, 0);
	++v;

	v->pos = Vec3(float(rect.Right()), float(rect.Top()), 0);
	v->color = col;
	v->tex = Vec2(1, 0);
	++v;

	v->pos = Vec3(float(rect.Left()), float(rect.Bottom()), 0);
	v->color = col;
	v->tex = Vec2(0, 1);
	++v;

	v->pos = Vec3(float(rect.Left()), float(rect.Bottom()), 0);
	v->color = col;
	v->tex = Vec2(0, 1);
	++v;

	v->pos = Vec3(float(rect.Right()), float(rect.Top()), 0);
	v->color = col;
	v->tex = Vec2(1, 0);
	++v;

	v->pos = Vec3(float(rect.Right()), float(rect.Bottom()), 0);
	v->color = col;
	v->tex = Vec2(1, 1);
	++v;

	in_buffer = 1;
	Flush();
}

//=================================================================================================
bool Gui::HaveDialog(cstring name)
{
	assert(name);
	vector<DialogBox*>& dialogs = (vector<DialogBox*>&)dialog_layer->GetControls();
	for(DialogBox* dialog : dialogs)
	{
		if(dialog->name == name)
			return true;
	}
	return false;
}

//=================================================================================================
bool Gui::HaveDialog(DialogBox* dialog)
{
	assert(dialog);
	vector<DialogBox*>& dialogs = (vector<DialogBox*>&)dialog_layer->GetControls();
	for(auto d : dialogs)
	{
		if(d == dialog)
			return true;
	}
	return false;
}

//=================================================================================================
bool Gui::AnythingVisible() const
{
	return !dialog_layer->Empty() || layer->AnythingVisible();
}

//=================================================================================================
void Gui::OnResize()
{
	wnd_size = app::engine->GetWindowSize();
	cursor_pos = wnd_size / 2;
	app::engine->SetUnlockPoint(cursor_pos);
	layer->Event(GuiEvent_WindowResize);
	dialog_layer->Event(GuiEvent_WindowResize);
}

//=================================================================================================
void Gui::DrawSpriteRectPart(Texture* t, const Rect& rect, const Rect& part, Color color)
{
	assert(t && t->IsLoaded());

	tCurrent = t->tex;
	Lock();

	Int2 size = t->GetSize();
	Vec4 col = Color(color);
	Box2d uv(float(part.Left()) / size.x, float(part.Top()) / size.y, float(part.Right()) / size.x, float(part.Bottom()) / size.y);

	v->pos = Vec3(float(rect.Left()), float(rect.Top()), 0);
	v->color = col;
	v->tex = uv.LeftTop();
	++v;

	v->pos = Vec3(float(rect.Right()), float(rect.Top()), 0);
	v->color = col;
	v->tex = uv.RightTop();
	++v;

	v->pos = Vec3(float(rect.Left()), float(rect.Bottom()), 0);
	v->color = col;
	v->tex = uv.LeftBottom();
	++v;

	v->pos = Vec3(float(rect.Left()), float(rect.Bottom()), 0);
	v->color = col;
	v->tex = uv.LeftBottom();
	++v;

	v->pos = Vec3(float(rect.Right()), float(rect.Top()), 0);
	v->color = col;
	v->tex = uv.RightTop();
	++v;

	v->pos = Vec3(float(rect.Right()), float(rect.Bottom()), 0);
	v->color = col;
	v->tex = uv.RightBottom();
	++v;

	in_buffer = 1;
	Flush();
}

//=================================================================================================
void Gui::DrawSpriteTransform(Texture* t, const Matrix& mat, Color color)
{
	assert(t && t->IsLoaded());

	Int2 size = t->GetSize();

	tCurrent = t->tex;
	Lock();

	Vec4 col = Color(color);

	Vec2 leftTop(0, 0),
		rightTop(float(size.x), 0),
		leftBottom(0, float(size.y)),
		rightBottom(float(size.x), float(size.y));

	leftTop = Vec2::Transform(leftTop, mat);
	rightTop = Vec2::Transform(rightTop, mat);
	leftBottom = Vec2::Transform(leftBottom, mat);
	rightBottom = Vec2::Transform(rightBottom, mat);

	v->pos = leftTop.XY();
	v->color = col;
	v->tex = Vec2(0, 0);
	++v;

	v->pos = rightTop.XY();
	v->color = col;
	v->tex = Vec2(1, 0);
	++v;

	v->pos = leftBottom.XY();
	v->color = col;
	v->tex = Vec2(0, 1);
	++v;

	v->pos = leftBottom.XY();
	v->color = col;
	v->tex = Vec2(0, 1);
	++v;

	v->pos = rightTop.XY();
	v->color = col;
	v->tex = Vec2(1, 0);
	++v;

	v->pos = rightBottom.XY();
	v->color = col;
	v->tex = Vec2(1, 1);
	++v;

	in_buffer = 1;
	Flush();
}

//=================================================================================================
void Gui::DrawLine(const Vec2* lines, uint count, Color color, bool strip)
{
	assert(lines && count);

	Lock();

	Vec4 col = Color(color);
	uint counter = count;

	if(strip)
	{
		v->pos = (*lines++).XY();
		v->color = col;
		++v;

		while(counter--)
		{
			v->pos = (*lines++).XY();
			v->color = col;
			++v;
		}
	}
	else
	{
		while(counter--)
		{
			v->pos = (*lines++).XY();
			v->color = col;
			++v;

			v->pos = (*lines++).XY();
			v->color = col;
			++v;
		}
	}

	V(vb->Unlock());
	V(device->SetVertexDeclaration(vertex_decl));
	V(device->SetStreamSource(0, vb, 0, sizeof(VParticle)));
	V(device->DrawPrimitive(strip ? D3DPT_LINESTRIP : D3DPT_LINELIST, 0, count));
}

//=================================================================================================
void Gui::LineBegin()
{
	effect->EndPass();
	effect->End();
	effect->SetTechnique(techGui2);
	uint passes;
	effect->Begin(&passes, 0);
	effect->BeginPass(0);
}

//=================================================================================================
void Gui::LineEnd()
{
	effect->EndPass();
	effect->End();
	effect->SetTechnique(techGui);
	uint passes;
	effect->Begin(&passes, 0);
	effect->BeginPass(0);
}

//=================================================================================================
bool Gui::NeedCursor()
{
	if(!dialog_layer->Empty())
		return true;
	else
		return layer->visible && layer->NeedCursor();
}

//=================================================================================================
bool Gui::DrawText3D(Font* font, Cstring text, uint flags, Color color, const Vec3& pos, Rect* text_rect)
{
	assert(font);

	Int2 pt;
	if(!To2dPoint(pos, pt))
		return false;

	Int2 size = font->CalculateSize(text);
	Rect r = { pt.x - size.x / 2, pt.y - size.y - 4, pt.x + size.x / 2 + 1, pt.y - 4 };
	if(!IsSet(flags, DTF_DONT_DRAW))
		DrawText(font, text, flags, color, r);

	if(text_rect)
		*text_rect = r;

	return true;
}

//=================================================================================================
bool Gui::To2dPoint(const Vec3& pos, Int2& pt)
{
	Vec4 v4;
	Vec3::Transform(pos, mViewProj, v4);

	if(v4.z < 0)
	{
		// jest poza kamer�
		return false;
	}

	Vec3 v3;

	// see if we are in world space already
	v3 = Vec3(v4.x, v4.y, v4.z);
	if(v4.w != 1)
	{
		if(v4.w == 0)
			v4.w = 0.00001f;
		v3 /= v4.w;
	}

	pt.x = int(v3.x*(wnd_size.x / 2) + (wnd_size.x / 2));
	pt.y = -int(v3.y*(wnd_size.y / 2) - (wnd_size.y / 2));

	return true;
}

//=================================================================================================
bool Gui::Intersect(vector<Hitbox>& hitboxes, const Int2& pt, int* index, int* index2)
{
	for(vector<Hitbox>::iterator it = hitboxes.begin(), end = hitboxes.end(); it != end; ++it)
	{
		if(it->rect.IsInside(pt))
		{
			if(index)
				*index = it->index;
			if(index2)
				*index2 = it->index2;
			return true;
		}
	}

	return false;
}

//=================================================================================================
void Gui::DrawSpriteTransformPart(Texture* t, const Matrix& mat, const Rect& part, Color color)
{
	assert(t && t->IsLoaded());

	Int2 size = t->GetSize();

	tCurrent = t->tex;
	Lock();

	Box2d uv(float(part.Left()) / size.x, float(part.Top() / size.y), float(part.Right()) / size.x, float(part.Bottom()) / size.y);

	Vec4 col = Color(color);

	Vec2 leftTop(part.LeftTop()),
		rightTop(part.RightTop()),
		leftBottom(part.LeftBottom()),
		rightBottom(part.RightBottom());

	leftTop = Vec2::Transform(leftTop, mat);
	rightTop = Vec2::Transform(rightTop, mat);
	leftBottom = Vec2::Transform(leftBottom, mat);
	rightBottom = Vec2::Transform(rightBottom, mat);

	v->pos = leftTop.XY();
	v->color = col;
	v->tex = uv.LeftTop();
	++v;

	v->pos = rightTop.XY();
	v->color = col;
	v->tex = uv.RightTop();
	++v;

	v->pos = leftBottom.XY();
	v->color = col;
	v->tex = uv.LeftBottom();
	++v;

	v->pos = leftBottom.XY();
	v->color = col;
	v->tex = uv.LeftBottom();
	++v;

	v->pos = rightTop.XY();
	v->color = col;
	v->tex = uv.RightTop();
	++v;

	v->pos = rightBottom.XY();
	v->color = col;
	v->tex = uv.RightBottom();
	++v;

	in_buffer = 1;
	Flush();
}

//=================================================================================================
void Gui::CloseDialogs()
{
	vector<DialogBox*>& dialogs = (vector<DialogBox*>&)dialog_layer->GetControls();
	for(DialogBox* dialog : dialogs)
	{
		if(!OR2_EQ(dialog->type, DIALOG_OK, DIALOG_YESNO))
			dialog->Event(GuiEvent_Close);
		if(dialog->need_delete)
		{
			DEBUG_DO(RemoveElementTry(created_dialogs, dialog));
			delete dialog;
		}
	}
	dialogs.clear();
	dialog_layer->inside_loop = false;
	assert(created_dialogs.empty());
	created_dialogs.clear();
}

//=================================================================================================
bool Gui::HavePauseDialog() const
{
	vector<DialogBox*>& dialogs = (vector<DialogBox*>&)dialog_layer->GetControls();
	for(vector<DialogBox*>::iterator it = dialogs.begin(), end = dialogs.end(); it != end; ++it)
	{
		if((*it)->pause)
			return true;
	}
	return false;
}

//=================================================================================================
DialogBox* Gui::GetDialog(cstring name)
{
	assert(name);
	if(dialog_layer->Empty())
		return nullptr;
	vector<DialogBox*>& dialogs = (vector<DialogBox*>&)dialog_layer->GetControls();
	for(vector<DialogBox*>::iterator it = dialogs.begin(), end = dialogs.end(); it != end; ++it)
	{
		if((*it)->name == name)
			return *it;
	}
	return nullptr;
}

//=================================================================================================
void Gui::DrawSprite2(Texture* t, const Matrix& mat, const Rect* part, const Rect* clipping, Color color)
{
	assert(t && t->IsLoaded());

	Int2 size = t->GetSize();
	GuiRect rect;

	// set pos & uv
	if(part)
		rect.Set(size.x, size.y, *part);
	else
		rect.Set(size.x, size.y);

	// transform
	rect.Transform(mat);

	// clipping
	if(clipping && !rect.Clip(*clipping))
		return;

	tCurrent = t->tex;
	Lock();

	// fill vertex buffer
	Vec4 col = color;
	rect.Populate(v, col);
	in_buffer = 1;
	Flush();
}

//=================================================================================================
// Rotation is generaly ignored and shouldn't be used here
Rect Gui::GetSpriteRect(Texture* t, const Matrix& mat, const Rect* part, const Rect* clipping)
{
	assert(t && t->IsLoaded());

	Int2 size = t->GetSize();
	GuiRect rect;

	// set pos & uv
	if(part)
		rect.Set(size.x, size.y, *part);
	else
		rect.Set(size.x, size.y);

	// transform
	rect.Transform(mat);

	// clipping
	if(clipping && !rect.Clip(*clipping))
		return Rect::Zero;

	return rect.ToRect();
}

//=================================================================================================
void Gui::DrawArea(Color color, const Int2& pos, const Int2& size, const Box2d* clip_rect)
{
	GuiRect gui_rect;
	gui_rect.Set(pos, size);
	if(!clip_rect || gui_rect.Clip(*clip_rect))
	{
		Vec4 col = Color(color);
		tCurrent = tPixel;
		Lock();
		gui_rect.Populate(v, col);
		in_buffer = 1;
		Flush();
	}
}

//=================================================================================================
void Gui::DrawArea(const Box2d& rect, const AreaLayout& area_layout, const Box2d* clip_rect, Color* tint)
{
	if(area_layout.mode == AreaLayout::Mode::None)
		return;

	Color color = area_layout.color;
	if(tint)
		color *= *tint;

	if(area_layout.mode == AreaLayout::Mode::Item)
	{
		DrawItem(area_layout.tex, Int2(rect.LeftTop()), Int2(rect.Size()), color, area_layout.size.x, area_layout.size.y, clip_rect);
	}
	else
	{
		// background
		if(area_layout.mode == AreaLayout::Mode::Image && area_layout.background_color != Color::None)
		{
			assert(!clip_rect);
			tCurrent = tPixel;
			Lock();
			AddRect(rect.LeftTop(), rect.RightBottom(), Color(area_layout.background_color));
			in_buffer = 1;
			Flush();
		}

		// image/color
		GuiRect gui_rect;
		if(area_layout.mode >= AreaLayout::Mode::Image)
		{
			tCurrent = area_layout.tex->tex;
			gui_rect.Set(rect, &area_layout.region);
		}
		else
		{
			tCurrent = tPixel;
			gui_rect.Set(rect, nullptr);
		}
		if(clip_rect)
		{
			if(!gui_rect.Clip(*clip_rect))
				return;
		}

		Lock();
		Vec4 col = color;
		gui_rect.Populate(v, col);
		in_buffer = 1;
		Flush();

		if(area_layout.mode != AreaLayout::Mode::BorderColor)
			return;

		// border
		assert(!clip_rect);
		tCurrent = tPixel;
		col = area_layout.border_color;
		Lock();

		float s = (float)area_layout.width;
		AddRect(rect.LeftTop(), rect.RightTop() + Vec2(-s, s), col);
		AddRect(rect.LeftTop(), rect.LeftBottom() + Vec2(s, 0), col);
		AddRect(rect.RightTop() + Vec2(-s, 0), rect.RightBottom(), col);
		AddRect(rect.LeftBottom() + Vec2(0, -s), rect.RightBottom(), col);

		in_buffer = 4;
		Flush();
	}
}

//=================================================================================================
void Gui::AddRect(const Vec2& left_top, const Vec2& right_bottom, const Vec4& color)
{
	v->pos = Vec3(left_top.x, left_top.y, 0);
	v->tex = Vec2(0, 0);
	v->color = color;
	++v;

	v->pos = Vec3(right_bottom.x, left_top.y, 0);
	v->tex = Vec2(1, 0);
	v->color = color;
	++v;

	v->pos = Vec3(right_bottom.x, right_bottom.y, 0);
	v->tex = Vec2(1, 1);
	v->color = color;
	++v;

	v->pos = Vec3(right_bottom.x, right_bottom.y, 0);
	v->tex = Vec2(1, 1);
	v->color = color;
	++v;

	v->pos = Vec3(left_top.x, right_bottom.y, 0);
	v->tex = Vec2(0, 1);
	v->color = color;
	++v;

	v->pos = Vec3(left_top.x, left_top.y, 0);
	v->tex = Vec2(0, 0);
	v->color = color;
	++v;
}

//=================================================================================================
void Gui::SetClipboard(cstring text)
{
	assert(text);

	if(OpenClipboard(app::engine->GetWindowHandle()))
	{
		EmptyClipboard();
		uint length = strlen(text) + 1;
		HANDLE mem = GlobalAlloc(GMEM_FIXED, length);
		char* str = (char*)GlobalLock(mem);
		memcpy(str, text, length);
		GlobalUnlock(mem);
		SetClipboardData(CF_TEXT, mem);
		CloseClipboard();
	}
}

//=================================================================================================
cstring Gui::GetClipboard()
{
	cstring result = nullptr;

	if(OpenClipboard(app::engine->GetWindowHandle()))
	{
		if(IsClipboardFormatAvailable(CF_TEXT) != FALSE)
		{
			HANDLE mem = GetClipboardData(CF_TEXT);
			cstring str = (cstring)GlobalLock(mem);
			result = Format("%s", str);
			GlobalUnlock(mem);
		}
		CloseClipboard();
	}

	return result;
}

//=================================================================================================
void Gui::UseGrayscale(bool grayscale)
{
	assert(grayscale != this->grayscale);
	this->grayscale = grayscale;

	effect->EndPass();
	effect->End();
	effect->SetTechnique(grayscale ? techGuiGrayscale : techGui);
	uint passes;
	effect->Begin(&passes, 0);
	effect->BeginPass(0);
}

//=================================================================================================
bool Gui::DrawText2(DrawTextOptions& options)
{
	uint line_begin, line_end, line_index = 0;
	int line_width, width = options.rect.SizeX();
	Vec4 current_color = Color(options.color);
	Vec4 default_color = current_color;
	outline_alpha = current_color.w;

	bool outline = (IsSet(options.flags, DTF_OUTLINE) && options.font->texOutline);
	bool parse_special = IsSet(options.flags, DTF_PARSE_SPECIAL);
	bool bottom_clip = false;

	tCurrent = options.font->tex;
	if(outline)
		tCurrent2 = options.font->texOutline;

	HitboxContext* hc;
	if(options.hitboxes)
	{
		hc = &tmpHitboxContext;
		hc->hitbox = options.hitboxes;
		hc->counter = (options.hitbox_counter ? *options.hitbox_counter : 0);
		hc->open = HitboxOpen::No;
	}
	else
		hc = nullptr;

	Lock(outline);

	typedef void (Gui::*DrawLineF)(Font* font, cstring text, uint line_begin, uint line_end, const Vec4& def_color,
		Vec4& color, int x, int y, const Rect* clipping, HitboxContext* hc, bool parse_special, const Vec2& scale);
	DrawLineF call;
	if(outline)
		call = &Gui::DrawLineOutline;
	else
		call = &Gui::DrawLine;

#define CALL (this->*call)

	if(!IsSet(options.flags, DTF_VCENTER | DTF_BOTTOM))
	{
		int y = options.rect.Top();

		if(!options.lines)
		{
			// tekst pionowo po �rodku lub na dole
			while(options.font->SplitLine(line_begin, line_end, line_width, line_index, options.str, options.str_length, options.flags, width))
			{
				// pocz�tkowa pozycja x w tej linijce
				int x;
				if(IsSet(options.flags, DTF_CENTER))
					x = options.rect.Left() + (width - line_width) / 2;
				else if(IsSet(options.flags, DTF_RIGHT))
					x = options.rect.Right() - line_width;
				else
					x = options.rect.Left();

				int clip_result = (options.clipping ? Clip(x, y, line_width, options.font->height, options.clipping) : 0);
				Int2 scaled_pos(int(options.scale.x * (x - options.rect.Left())) + options.rect.Left(),
					int(options.scale.y * (y - options.rect.Top())) + options.rect.Top());

				// znaki w tej linijce
				if(clip_result == 0)
					CALL(options.font, options.str, line_begin, line_end, default_color, current_color, scaled_pos.x, scaled_pos.y, nullptr, hc, parse_special,
						options.scale);
				else if(clip_result == 5)
					CALL(options.font, options.str, line_begin, line_end, default_color, current_color, scaled_pos.x, scaled_pos.y, options.clipping, hc, parse_special,
						options.scale);
				else if(clip_result == 2)
				{
					// tekst jest pod widocznym regionem, przerwij rysowanie
					bottom_clip = true;
					break;
				}
				else
				{
					// pomi� hitbox
					SkipLine(options.str, line_begin, line_end, hc);
				}

				// zmie� y na kolejn� linijk�
				y += options.font->height;
			}
		}
		else
		{
			for(uint line_index = options.lines_start, lines_max = min(options.lines_end, options.lines->size()); line_index < lines_max; ++line_index)
			{
				auto& line = options.lines->at(line_index);

				// pocz�tkowa pozycja x w tej linijce
				int x;
				if(IsSet(options.flags, DTF_CENTER))
					x = options.rect.Left() + (width - line.width) / 2;
				else if(IsSet(options.flags, DTF_RIGHT))
					x = options.rect.Right() - line.width;
				else
					x = options.rect.Left();

				int clip_result = (options.clipping ? Clip(x, y, line.width, options.font->height, options.clipping) : 0);
				Int2 scaled_pos(int(options.scale.x * (x - options.rect.Left())) + options.rect.Left(),
					int(options.scale.y * (y - options.rect.Top())) + options.rect.Top());

				// znaki w tej linijce
				if(clip_result == 0)
					CALL(options.font, options.str, line.begin, line.end, default_color, current_color, scaled_pos.x, scaled_pos.y, nullptr, hc, parse_special,
						options.scale);
				else if(clip_result == 5)
					CALL(options.font, options.str, line.begin, line.end, default_color, current_color, scaled_pos.x, scaled_pos.y, options.clipping, hc, parse_special,
						options.scale);
				else if(clip_result == 2)
				{
					// tekst jest pod widocznym regionem, przerwij rysowanie
					bottom_clip = true;
					break;
				}
				else
				{
					// pomi� hitbox
					SkipLine(options.str, line.begin, line.end, hc);
				}

				// zmie� y na kolejn� linijk�
				y += options.font->height;
			}
		}
	}
	else
	{
		// tekst u g�ry
		if(!options.lines)
		{
			static vector<TextLine> lines_data;
			lines_data.clear();

			// oblicz wszystkie linijki
			while(options.font->SplitLine(line_begin, line_end, line_width, line_index, options.str, options.str_length, options.flags, width))
				lines_data.push_back(TextLine(line_begin, line_end, line_width));

			options.lines = &lines_data;
		}

		// pocz�tkowa pozycja y
		int y;
		if(IsSet(options.flags, DTF_BOTTOM))
			y = options.rect.Bottom() - options.lines->size()*options.font->height;
		else
			y = options.rect.Top() + (options.rect.SizeY() - int(options.lines->size())*options.font->height) / 2;

		for(uint line_index = options.lines_start, lines_max = min(options.lines_end, options.lines->size()); line_index < lines_max; ++line_index)
		{
			auto& line = options.lines->at(line_index);

			// pocz�tkowa pozycja x w tej linijce
			int x;
			if(IsSet(options.flags, DTF_CENTER))
				x = options.rect.Left() + (width - line.width) / 2;
			else if(IsSet(options.flags, DTF_RIGHT))
				x = options.rect.Right() - line.width;
			else
				x = options.rect.Left();

			int clip_result = (options.clipping ? Clip(x, y, line.width, options.font->height, options.clipping) : 0);
			Int2 scaled_pos(int(options.scale.x * (x - options.rect.Left())) + options.rect.Left(),
				int(options.scale.y * (y - options.rect.Top())) + options.rect.Top());

			// znaki w tej linijce
			if(clip_result == 0)
				CALL(options.font, options.str, line.begin, line.end, default_color, current_color, scaled_pos.x, scaled_pos.y, nullptr, hc, parse_special,
					options.scale);
			else if(clip_result == 5)
				CALL(options.font, options.str, line.begin, line.end, default_color, current_color, scaled_pos.x, scaled_pos.y, options.clipping, hc, parse_special,
					options.scale);
			else if(clip_result == 2)
			{
				// tekst jest pod widocznym regionem, przerwij rysowanie
				bottom_clip = true;
				break;
			}
			else if(options.hitboxes)
			{
				// pomi� hitbox
				SkipLine(options.str, line.begin, line.end, hc);
			}

			// zmie� y na kolejn� linijk�
			y += options.font->height;
		}
	}

	Flush();

	if(options.hitbox_counter)
		*options.hitbox_counter = hc->counter;

	return !bottom_clip;
}

//=================================================================================================
void Gui::SetLayout(Layout* master_layout)
{
	assert(master_layout);
	this->master_layout = master_layout;
	if(!layout)
		layout = master_layout->Get<layout::Gui>();
}
