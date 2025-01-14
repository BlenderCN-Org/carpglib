#include "EnginePch.h"
#include "EngineCore.h"
#include "GetNumberDialog.h"
#include "Input.h"

//-----------------------------------------------------------------------------
GetNumberDialog* GetNumberDialog::self;

//=================================================================================================
GetNumberDialog::GetNumberDialog(const DialogInfo& info) : DialogBox(info), scrollbar(true)
{
}

//=================================================================================================
void GetNumberDialog::Draw(ControlDrawData*)
{
	gui->DrawArea(Box2d::Create(Int2::Zero, gui->wnd_size), layout->background);
	gui->DrawArea(Box2d::Create(global_pos, size), layout->box);

	for(int i = 0; i < 2; ++i)
		bts[i].Draw();

	Rect r = { global_pos.x + 16,global_pos.y + 16,global_pos.x + size.x,global_pos.y + size.y };
	gui->DrawText(layout->font, text, DTF_CENTER, Color::Black, r);

	textBox.Draw();
	scrollbar.Draw();

	Rect r2 = { global_pos.x + 16,global_pos.y + 120,global_pos.x + size.x - 16,global_pos.y + size.y };
	gui->DrawText(layout->font, Format("%d", min_value), DTF_LEFT, Color::Black, r2);
	gui->DrawText(layout->font, Format("%d", max_value), DTF_RIGHT, Color::Black, r2);
}

//=================================================================================================
void GetNumberDialog::Update(float dt)
{
	textBox.mouse_focus = focus;

	if(input->Focus() && focus)
	{
		for(int i = 0; i < 2; ++i)
		{
			bts[i].mouse_focus = focus;
			bts[i].Update(dt);
		}

		bool moving = scrollbar.clicked;
		float prev_offset = scrollbar.offset;
		scrollbar.ApplyMouseWheel();
		scrollbar.mouse_focus = focus;
		scrollbar.Update(dt);
		bool changed = false;
		if(scrollbar.change != 0 || gui->mouse_wheel != 0.f)
		{
			int num = atoi(textBox.GetText().c_str());
			if(gui->mouse_wheel != 0.f)
			{
				int change = 1;
				if(input->Down(Key::Shift))
					change = max(1, (max_value - min_value) / 20);
				if(gui->mouse_wheel < 0.f)
					change = -change;
				num += change;
				if(num < min_value)
					num = min_value;
				else if(num > max_value)
					num = max_value;
			}
			else
				num += scrollbar.change;
			textBox.SetText(Format("%d", num));
			scrollbar.offset = float(num - min_value) / (max_value - min_value) * (scrollbar.total - scrollbar.part);
			changed = true;
		}
		else if(!Equal(scrollbar.offset, prev_offset))
		{
			textBox.SetText(Format("%d", (int)Lerp(float(min_value), float(max_value), scrollbar.offset / (scrollbar.total - scrollbar.part))));
			changed = true;
		}
		if(moving)
		{
			if(!scrollbar.clicked)
			{
				textBox.focus = true;
				textBox.Event(GuiEvent_GainFocus);
			}
			else
				changed = true;
		}
		else if(scrollbar.clicked)
		{
			textBox.focus = false;
			textBox.Event(GuiEvent_LostFocus);
		}
		textBox.Update(dt);
		if(!changed)
		{
			int num = atoi(textBox.GetText().c_str());
			if(!scrollbar.clicked)
				scrollbar.offset = float(num - min_value) / (max_value - min_value) * (scrollbar.total - scrollbar.part);
		}
		if(textBox.GetText().empty())
			bts[1].state = Button::DISABLED;
		else if(bts[1].state == Button::DISABLED)
			bts[1].state = Button::NONE;

		if(result == -1)
		{
			if(input->PressedRelease(Key::Escape))
				result = BUTTON_CANCEL;
			else if(input->PressedRelease(Key::Enter))
				result = BUTTON_OK;
		}

		if(result != -1)
		{
			if(result == BUTTON_OK)
				*value = atoi(textBox.GetText().c_str());
			gui->CloseDialog(this);
			if(event)
				event(result);
		}
	}
}

//=================================================================================================
void GetNumberDialog::Event(GuiEvent e)
{
	if(e == GuiEvent_GainFocus)
	{
		textBox.focus = true;
		textBox.Event(GuiEvent_GainFocus);
	}
	else if(e == GuiEvent_LostFocus || e == GuiEvent_Close)
	{
		textBox.focus = false;
		textBox.Event(GuiEvent_LostFocus);
		scrollbar.LostFocus();
	}
	else if(e == GuiEvent_WindowResize)
	{
		self->pos = self->global_pos = (gui->wnd_size - self->size) / 2;
		self->bts[0].global_pos = self->bts[0].pos + self->global_pos;
		self->bts[1].global_pos = self->bts[1].pos + self->global_pos;
		self->textBox.global_pos = self->textBox.pos + self->global_pos;
		self->scrollbar.global_pos = self->scrollbar.pos + self->global_pos;
	}
	else if(e >= GuiEvent_Custom)
	{
		if(e == Result_Ok)
			result = BUTTON_OK;
		else if(e == Result_Cancel)
			result = BUTTON_CANCEL;
	}
}

//=================================================================================================
GetNumberDialog* GetNumberDialog::Show(Control* parent, DialogEvent event, cstring text, int min_value, int max_value, int* value)
{
	if(!self)
	{
		DialogInfo info;
		info.event = nullptr;
		info.name = "GetNumberDialog";
		info.parent = nullptr;
		info.pause = false;
		info.order = ORDER_NORMAL;
		info.type = DIALOG_CUSTOM;

		self = new GetNumberDialog(info);
		self->size = Int2(300, 200);
		self->bts.resize(2);

		Button& bt1 = self->bts[0],
			&bt2 = self->bts[1];

		bt1.text = gui->txCancel;
		bt1.id = Result_Cancel;
		bt1.size = Int2(100, 40);
		bt1.pos = Int2(300 - 100 - 16, 200 - 40 - 16);
		bt1.parent = self;

		bt2.text = gui->txOk;
		bt2.id = Result_Ok;
		bt2.size = Int2(100, 40);
		bt2.pos = Int2(16, 200 - 40 - 16);
		bt2.parent = self;

		self->textBox.size = Int2(200, 35);
		self->textBox.pos = Int2(50, 60);
		self->textBox.SetNumeric(true);
		self->textBox.limit = 10;

		Scrollbar& scroll = self->scrollbar;
		scroll.pos = Int2(32, 100);
		scroll.size = Int2(300 - 64, 16);
		scroll.total = 100;
		scroll.part = 5;
	}

	self->result = -1;
	self->parent = parent;
	self->order = static_cast<DialogBox*>(parent)->order;
	self->event = event;
	self->text = text;
	self->min_value = min_value;
	self->max_value = max_value;
	self->value = value;
	self->pos = self->global_pos = (gui->wnd_size - self->size) / 2;
	self->bts[0].global_pos = self->bts[0].pos + self->global_pos;
	self->bts[1].global_pos = self->bts[1].pos + self->global_pos;
	self->textBox.global_pos = self->textBox.pos + self->global_pos;
	self->textBox.num_min = min_value;
	self->textBox.num_max = max_value;
	self->scrollbar.global_pos = self->scrollbar.pos + self->global_pos;
	self->scrollbar.offset = 0;
	self->scrollbar.manual_change = true;
	self->textBox.SetText(Format("%d", *value));

	gui->ShowDialog(self);

	return self;
}
