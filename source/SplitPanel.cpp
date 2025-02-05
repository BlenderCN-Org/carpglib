#include "EnginePch.h"
#include "EngineCore.h"
#include "SplitPanel.h"

SplitPanel::SplitPanel() : min_size1(0), min_size2(0), panel1(nullptr), panel2(nullptr), allow_move(true), horizontal(true), splitter_size(3)
{
}

SplitPanel::~SplitPanel()
{
	delete panel1;
	delete panel2;
}

void SplitPanel::Draw(ControlDrawData*)
{
	gui->DrawArea(Box2d::Create(global_pos, size), layout->background);
	gui->DrawArea(Box2d(split_global), horizontal ? layout->horizontal : layout->vertical);

	panel1->Draw();
	panel2->Draw();
}

void SplitPanel::Event(GuiEvent e)
{
	switch(e)
	{
	case GuiEvent_Initialize:
		assert(parent);
		if(!panel1)
			panel1 = new Panel;
		panel1->parent = this;
		if(!panel2)
			panel2 = new Panel;
		panel2->parent = this;
		// tmp
		/*panel1->custom_color = Color::Red;
		panel1->use_custom_color = true;
		panel2->custom_color = Color::Blue;
		panel2->use_custom_color = true;
		custom_color = Color::Green;
		use_custom_color = true;*/
		Update(e, true, true);
		panel1->Initialize();
		panel2->Initialize();
		break;
	case GuiEvent_Moved:
		Update(e, false, true);
		break;
	case GuiEvent_Resize:
		Update(e, true, false);
		break;
	case GuiEvent_Show:
	case GuiEvent_Hide:
		panel1->Event(e);
		panel2->Event(e);
		break;
	case GuiEvent_LostMouseFocus:
		break;
	}
}

void SplitPanel::Update(float dt)
{
}

void SplitPanel::Update(GuiEvent e, bool resize, bool move)
{
	if(resize)
	{
		const Int2& padding = layout->padding;
		Int2 size_left = size;
		if(horizontal)
		{
			size_left.x -= splitter_size;
			panel1->size = Int2(size_left.x / 2 - padding.x * 2, size_left.y - padding.y * 2);
			panel1->pos = padding;
			split = Rect::Create(Int2(panel1->size.x + padding.x * 2, 0), Int2(splitter_size, size.y));
			panel2->size = Int2(size_left.x - panel1->size.x - padding.x * 2, size_left.y - padding.y * 2);
			panel2->pos = Int2(split.p1.x + padding.x, padding.y);
		}
		else
		{
			size_left.y -= splitter_size;
			panel1->size = Int2(size_left.x - padding.x * 2, size_left.y / 2 - padding.y * 2);
			panel1->pos = padding;
			split = Rect::Create(Int2(0, panel1->size.y + padding.y), Int2(size.x, splitter_size));
			panel2->size = Int2(size_left.x - padding.x * 2, size_left.y - panel1->size.y - padding.y * 2);
			panel2->pos = Int2(padding.x, split.p1.y + padding.y);
		}
	}

	if(move)
	{
		global_pos = pos + parent->global_pos;
		panel1->global_pos = panel1->pos + global_pos;
		panel2->global_pos = panel2->pos + global_pos;
		split_global += global_pos;
	}

	if(e != GuiEvent_Initialize)
	{
		panel1->Event(e);
		panel2->Event(e);
	}
}

void SplitPanel::SetPanel1(Panel* panel)
{
	assert(panel);
	assert(!panel1);
	panel1 = panel;
}

void SplitPanel::SetPanel2(Panel* panel)
{
	assert(panel);
	assert(!panel2);
	panel2 = panel;
}

void SplitPanel::SetSplitterSize(uint _splitter_size)
{
	assert(!initialized);
	splitter_size = _splitter_size;
}
