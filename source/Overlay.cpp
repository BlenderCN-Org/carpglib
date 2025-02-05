#include "EnginePch.h"
#include "EngineCore.h"
#include "DialogBox.h"
#include "GuiDialog.h"
#include "Input.h"
#include "MenuStrip.h"
#include "Overlay.h"

Overlay::Overlay() : Container(true), focused(nullptr), to_add(nullptr)
{
}

Overlay::~Overlay()
{
	// prevent deleting twice
	for(MenuStrip* menu : menus)
		RemoveElement(ctrls, static_cast<Control*>(menu));
}

void Overlay::Draw(ControlDrawData*)
{
	Container::Draw();

	for(GuiDialog* dialog : dialogs)
	{
		gui->DrawArea(Box2d::Create(Int2::Zero, gui->wnd_size), layout->background);
		dialog->Draw();
	}

	for(MenuStrip* menu : menus)
		menu->Draw();
}

void Overlay::Update(float dt)
{
	mouse_focus = true;
	clicked = nullptr;

	// close menu if old dialog window is open
	if(gui->HaveDialog())
	{
		mouse_focus = false;
		CloseMenus();
	}

	for(MenuStrip* menu : menus) // kaplica jak co� usunie
		UpdateControl(menu, dt);

	// update dialogs
	for(auto it = dialogs.rbegin(), end = dialogs.rend(); it != end; ++it)
		UpdateControl(*it, dt);
	if(!dialogs.empty())
		mouse_focus = false;
	for(GuiDialog* dialog : dialogs_to_close)
		RemoveElement(dialogs, dialog);
	dialogs_to_close.clear();

	// update controls
	Container::Update(dt);

	// close menu if clicked outside
	if(clicked)
	{
		MenuStrip* leftover = nullptr;
		for(MenuStrip* menu : menus)
		{
			if(menu != clicked)
				menu->OnClose();
			else
				leftover = menu;
		}
		menus.clear();
		if(leftover)
			menus.push_back(leftover);
	}

	for(MenuStrip* menu : menus_to_close)
		RemoveElement(menus, menu);
	menus_to_close.clear();

	if(to_add)
	{
		menus.push_back(to_add);
		if(focused)
		{
			focused->focus = false;
			if(focused->IsOnCharHandler())
				gui->RemoveOnCharHandler(dynamic_cast<OnCharHandler*>(focused));
		}
		to_add->focus = true;
		focused = to_add;
		if(to_add->IsOnCharHandler())
			gui->AddOnCharHandler(dynamic_cast<OnCharHandler*>(to_add));
		to_add = nullptr;
	}
}

void Overlay::ShowDialog(GuiDialog* dialog)
{
	assert(dialog);
	CloseMenus();
	dialogs.push_back(dialog);
	dialog->Initialize();
	dialog->SetPosition((gui->wnd_size - dialog->GetSize()) / 2);
}

void Overlay::CloseDialog(GuiDialog* dialog)
{
	assert(dialog);
	dialogs_to_close.push_back(dialog);
}

void Overlay::ShowMenu(MenuStrip* menu, const Int2& _pos)
{
	assert(menu);
	CloseMenus();
	to_add = menu;
	menu->ShowAt(_pos);
}

void Overlay::CloseMenu(MenuStrip* menu)
{
	assert(menu);
	menu->OnClose();
	menus_to_close.push_back(menu);
}

void Overlay::CheckFocus(Control* ctrl, bool pressed)
{
	assert(ctrl);
	if(!ctrl->mouse_focus)
		return;

	ctrl->mouse_focus = false;

	if(input->PressedRelease(Key::LeftButton)
		|| input->PressedRelease(Key::RightButton)
		|| input->PressedRelease(Key::MiddleButton)
		|| pressed)
	{
		assert(!clicked);
		clicked = ctrl;
		SetFocus(ctrl);
	}
}

void Overlay::SetFocus(Control* ctrl)
{
	assert(ctrl);
	if(focused)
	{
		if(focused == ctrl)
			return;
		focused->focus = false;
		focused->Event(GuiEvent_LostFocus);
		if(focused->IsOnCharHandler())
			gui->RemoveOnCharHandler(dynamic_cast<OnCharHandler*>(focused));
	}
	ctrl->focus = true;
	ctrl->Event(GuiEvent_GainFocus);
	focused = ctrl;
	if(ctrl->IsOnCharHandler())
		gui->AddOnCharHandler(dynamic_cast<OnCharHandler*>(ctrl));
}

bool Overlay::IsOpen(MenuStrip* menu)
{
	assert(menu);

	for(MenuStrip* m : menus)
	{
		if(m == menu)
			return true;
	}

	return false;
}

void Overlay::CloseMenus()
{
	for(MenuStrip* menu : menus)
		menu->OnClose();
	menus.clear();
	if(to_add)
	{
		to_add->OnClose();
		to_add = nullptr;
	}
}
