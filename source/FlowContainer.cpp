#include "EnginePch.h"
#include "EngineCore.h"
#include "FlowContainer.h"
#include "Input.h"

//-----------------------------------------------------------------------------
ObjectPool<FlowItem> FlowItem::Pool;

void FlowItem::Set(cstring _text)
{
	type = Section;
	text = _text;
	state = Button::NONE;
	group = -1;
	id = -1;
}

void FlowItem::Set(cstring _text, int _group, int _id)
{
	type = Item;
	text = _text;
	group = _group;
	id = _id;
	state = Button::NONE;
}

void FlowItem::Set(int _group, int _id, int _tex_id, bool disabled)
{
	type = Button;
	group = _group;
	id = _id;
	tex_id = _tex_id;
	state = (disabled ? Button::DISABLED : Button::NONE);
}

//=================================================================================================
FlowContainer::FlowContainer() : id(-1), group(-1), on_button(nullptr), button_size(0, 0), word_warp(true), allow_select(false), selected(nullptr),
batch_changes(false), button_tex(nullptr)
{
	size = Int2(-1, -1);
}

//=================================================================================================
FlowContainer::~FlowContainer()
{
	Clear();
}

//=================================================================================================
void FlowContainer::Update(float dt)
{
	bool ok = false;
	group = -1;
	id = -1;

	if(mouse_focus)
	{
		if(IsInside(gui->cursor_pos))
		{
			ok = true;

			if(input->Focus())
				scroll.ApplyMouseWheel();

			Int2 off(0, (int)scroll.offset);
			bool have_button = false;

			for(FlowItem* fi : items)
			{
				Int2 p = fi->pos - off + global_pos;
				if(fi->type == FlowItem::Item)
				{
					if(have_button)
					{
						p.y -= 2;
						have_button = false;
					}
					if(fi->group != -1 && PointInRect(gui->cursor_pos, p, fi->size))
					{
						group = fi->group;
						id = fi->id;
						if(allow_select && fi->state != Button::DISABLED)
						{
							gui->cursor_mode = CURSOR_HOVER;
							if(on_select && input->Pressed(Key::LeftButton))
							{
								selected = fi;
								on_select();
								return;
							}
						}
					}
				}
				else if(fi->type == FlowItem::Button && fi->state != Button::DISABLED)
				{
					have_button = true;
					if(PointInRect(gui->cursor_pos, p, fi->size))
					{
						group = fi->group;
						id = fi->id;
						gui->cursor_mode = CURSOR_HOVER;
						if(fi->state == Button::DOWN)
						{
							if(input->Up(Key::LeftButton))
							{
								fi->state = Button::HOVER;
								on_button(fi->group, fi->id);
								return;
							}
						}
						else if(input->Pressed(Key::LeftButton))
							fi->state = Button::DOWN;
						else
							fi->state = Button::HOVER;
					}
					else
						fi->state = Button::NONE;
				}
				else
					have_button = false;
			}
		}

		scroll.mouse_focus = mouse_focus;
		scroll.Update(dt);
	}

	if(!ok)
	{
		for(FlowItem* fi : items)
		{
			if(fi->type == FlowItem::Button && fi->state != Button::DISABLED)
				fi->state = Button::NONE;
		}
	}
}

//=================================================================================================
void FlowContainer::Draw(ControlDrawData*)
{
	gui->DrawArea(Box2d::Create(global_pos, size - Int2(16, 0)), layout->box);

	scroll.Draw();

	int sizex = size.x - 16;

	Rect rect;
	Rect clip = Rect::Create(global_pos + Int2(2, 2), Int2(sizex - 2, size.y - 2));
	int offset = (int)scroll.offset;
	uint flags = (word_warp ? 0 : DTF_SINGLELINE) | DTF_PARSE_SPECIAL;

	for(FlowItem* fi : items)
	{
		if(fi->type != FlowItem::Button)
		{
			rect = Rect::Create(global_pos + fi->pos - Int2(0, offset), fi->size);

			// text above box
			if(rect.Bottom() < global_pos.y)
				continue;

			if(fi->state == Button::DOWN)
			{
				Rect rs = { global_pos.x + 2, rect.Top(), global_pos.x + sizex, rect.Bottom() };
				Rect out;
				if(Rect::Intersect(rs, clip, out))
					gui->DrawArea(Box2d(out), layout->selection);
			}

			if(!gui->DrawText(fi->type == FlowItem::Section ? layout->font_section : layout->font, fi->text, flags,
				(fi->state != Button::DISABLED ? Color::Black : Color(64, 64, 64)), rect, &clip))
				break;
		}
		else
		{
			// button above or below box
			if(global_pos.y + fi->pos.y - offset + fi->size.y < global_pos.y
				|| global_pos.y + fi->pos.y - offset > global_pos.y + size.y)
				continue;

			const AreaLayout& area = button_tex[fi->tex_id].tex[fi->state];
			gui->DrawArea(Box2d::Create(global_pos + fi->pos - Int2(0, offset), area.size), area, &Box2d(clip));
		}
	}
}

//=================================================================================================
FlowItem* FlowContainer::Add()
{
	FlowItem* item = FlowItem::Pool.Get();
	items.push_back(item);
	return item;
}

//=================================================================================================
void FlowContainer::Clear()
{
	FlowItem::Pool.Free(items);
	batch_changes = false;
}

//=================================================================================================
void FlowContainer::GetSelected(int& _group, int& _id)
{
	if(group != -1)
	{
		_group = group;
		_id = id;
	}
}

//=================================================================================================
void FlowContainer::UpdateSize(const Int2& _pos, const Int2& _size, bool _visible)
{
	global_pos = pos = _pos;
	if(size.y != _size.y && _visible)
	{
		size = _size;
		Reposition();
	}
	else
		size = _size;
	scroll.global_pos = scroll.pos = global_pos + Int2(size.x - 17, 0);
	scroll.size = Int2(16, size.y);
	scroll.part = size.y;
}

//=================================================================================================
void FlowContainer::UpdatePos(const Int2& parent_pos)
{
	global_pos = pos + parent_pos;
	scroll.global_pos = scroll.pos = global_pos + Int2(size.x - 17, 0);
	scroll.size = Int2(16, size.y);
	scroll.part = size.y;
}

//=================================================================================================
void FlowContainer::Reposition()
{
	int sizex = (word_warp ? size.x - 20 : 10000);
	int y = 2;
	bool have_button = false;

	for(FlowItem* fi : items)
	{
		if(fi->type != FlowItem::Button)
		{
			if(fi->type != FlowItem::Section)
			{
				if(have_button)
				{
					fi->size = layout->font->CalculateSize(fi->text, sizex - 2 - button_size.x);
					fi->pos = Int2(4 + button_size.x, y);
				}
				else
				{
					fi->size = layout->font->CalculateSize(fi->text, sizex);
					fi->pos = Int2(2, y);
				}
			}
			else
			{
				fi->size = layout->font_section->CalculateSize(fi->text, sizex);
				fi->pos = Int2(2, y);
			}
			have_button = false;
			y += fi->size.y;
		}
		else
		{
			fi->size = button_size;
			fi->pos = Int2(2, y);
			have_button = true;
		}
	}

	UpdateScrollbar(y);
}

//=================================================================================================
FlowItem* FlowContainer::Find(int _group, int _id)
{
	for(FlowItem* fi : items)
	{
		if(fi->group == _group && fi->id == _id)
			return fi;
	}

	return nullptr;
}

//=================================================================================================
void FlowContainer::SetItems(vector<FlowItem*>& _items)
{
	Clear();
	items = std::move(_items);
	Reposition();
	ResetScrollbar();
}

//=================================================================================================
void FlowContainer::UpdateText(FlowItem* item, cstring text, bool batch)
{
	assert(item && text);

	item->text = text;

	int sizex = (word_warp ? size.x - 20 : 10000);
	Int2 new_size = layout->font->CalculateSize(text, (item->pos.x == 2 ? sizex : sizex - 2 - button_size.x));

	if(new_size.y != item->size.y)
	{
		item->size = new_size;

		if(batch)
			batch_changes = true;
		else
			UpdateText();
	}
	else
	{
		// only width changed, no need to recalculate positions
		item->size = new_size;
	}
}

//=================================================================================================
void FlowContainer::UpdateText()
{
	batch_changes = false;

	int y = 2;
	bool have_button = false;

	for(FlowItem* fi : items)
	{
		if(fi->type != FlowItem::Button)
		{
			if(fi->type != FlowItem::Section && have_button)
				fi->pos = Int2(4 + button_size.x, y);
			else
				fi->pos = Int2(2, y);
			have_button = false;
			y += fi->size.y;
		}
		else
		{
			fi->pos = Int2(2, y);
			have_button = true;
		}
	}

	scroll.total = y;
}

//=================================================================================================
void FlowContainer::UpdateScrollbar(int new_size)
{
	scroll.total = new_size;
	if(scroll.offset + scroll.part > scroll.total)
		scroll.offset = max(0.f, float(scroll.total - scroll.part));
}
