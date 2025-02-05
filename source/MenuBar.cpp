#include "EnginePch.h"
#include "EngineCore.h"
#include "MenuBar.h"
#include "MenuStrip.h"
#include "Input.h"

MenuBar::MenuBar() : selected(nullptr), handler(nullptr)
{
}

MenuBar::~MenuBar()
{
	DeleteElements(items);
}

void MenuBar::Draw(ControlDrawData*)
{
	// backgroud
	gui->DrawArea(rect, layout->background);

	// items
	Rect rect;
	for(Item* item : items)
	{
		AreaLayout* area_layout;
		Color font_color;
		switch(item->mode)
		{
		case Item::Up:
		default:
			area_layout = &layout->button;
			font_color = layout->font_color;
			break;
		case Item::Hover:
			area_layout = &layout->button_hover;
			font_color = layout->font_color_hover;
			break;
		case Item::Down:
			area_layout = &layout->button_down;
			font_color = layout->font_color_down;
			break;
		}

		// item background
		gui->DrawArea(item->rect, *area_layout);

		// item text
		gui->DrawText(layout->font, item->text, DTF_CENTER | DTF_VCENTER, font_color, Rect(item->rect));
	}
}

void MenuBar::Update(float dt)
{
	bool down = false;
	if(selected)
	{
		if(selected->mode != Item::Down)
			selected->mode = Item::Up;
		else
			down = true;
	}

	if(!mouse_focus || !rect.IsInside(gui->cursor_pos))
		return;

	for(Item* item : items)
	{
		if(item->rect.IsInside(gui->cursor_pos))
		{
			if(down || input->Pressed(Key::LeftButton))
			{
				if((item != selected || item->mode != Item::Down) && (input->Pressed(Key::LeftButton) || gui->MouseMoved()))
				{
					EnsureMenu(item);
					item->menu->ShowMenu(Int2(item->rect.LeftBottom()));
					selected = item;
					selected->mode = Item::Down;
				}
			}
			else
			{
				selected = item;
				selected->mode = Item::Hover;
			}
			break;
		}
	}
}

void MenuBar::Event(GuiEvent e)
{
	switch(e)
	{
	case GuiEvent_Initialize:
		// control initialization, menubar must be attacked to other control (like Window)
		Update(true, true);
		break;
	case GuiEvent_Moved:
		// parent control moved
		Update(true, false);
		break;
	case GuiEvent_Resize:
		// parent window resized
		Update(false, true);
		break;
	case GuiEvent_Show:
		// control is shown, forget old selected item
		selected = nullptr;
		for(Item* item : items)
			item->mode = Item::Up;
		break;
	}
}

void MenuBar::AddMenu(cstring text, std::initializer_list<SimpleMenuCtor> const & _items)
{
	assert(text);

	float item_height = (float)layout->font->height + layout->item_padding.y * 2;
	float item_width = (float)layout->font->CalculateSize(text).x + layout->item_padding.x * 2;

	Item* item = new Item;
	item->text = text;
	item->rect = Box2d(0, 0, item_width, item_height);
	item->rect += Vec2(layout->padding) / 2;
	if(!items.empty())
		item->rect += Vec2(items.back()->rect.v2.x, 0);
	item->index = items.size();
	item->items = _items;
	items.push_back(item);
}

void MenuBar::Update(bool move, bool resize)
{
	assert(parent);
	Int2 prev_pos = global_pos;
	if(move)
		global_pos = parent->global_pos;
	if(resize)
		size = Int2(parent->size.x, layout->font->height + layout->padding.y + layout->item_padding.y * 2);
	rect = Box2d::Create(global_pos, size);
	if(move)
	{
		Vec2 offset = Vec2(global_pos - prev_pos);
		for(Item* item : items)
			item->rect += offset;
	}
}

void MenuBar::OnCloseMenuStrip()
{
	if(selected)
		selected->mode = Item::Up;
}

// offset must be -1/+1, menu must be open
void MenuBar::ChangeMenu(int offset)
{
	assert(offset == -1 || offset == 1);
	assert(selected);

	bool prev = (offset == -1);
	int index = Modulo(selected->index + (prev ? -1 : 1), items.size());
	Item* item = items[index];

	// showing new menu should close the old one
	EnsureMenu(item);
	item->menu->ShowMenu(Int2(item->rect.LeftBottom()));
	item->menu->SetSelectedIndex(0);
	selected = item;
	selected->mode = Item::Down;
}

void MenuBar::EnsureMenu(Item* item)
{
	if(item->menu)
		return;
	MenuStrip* menu = new MenuStrip(item->items, (int)item->rect.SizeX());
	menu->SetOwner(this, item->index);
	menu->SetHandler(handler);
	menu->SetOnCloseHandler(MenuStrip::OnCloseHandler(this, &MenuBar::OnCloseMenuStrip));
	menu->Initialize();
	item->menu = menu;
}
