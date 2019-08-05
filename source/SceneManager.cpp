#include "EnginePch.h"
#include "EngineCore.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Render.h"
#include "DirectX.h"

SceneManager* app::scene_mgr;

SceneManager::SceneManager() : camera(nullptr), active_scene(nullptr)
{
}

SceneManager::~SceneManager()
{
	DeleteElements(scenes);
}

void SceneManager::Draw()
{
	IDirect3DDevice9* device = app::render->GetDevice();

	if(!camera || !active_scene)
	{
		V(device->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET | D3DCLEAR_STENCIL, Color::Black, 1.f, 0));
		return;
	}

	visible_nodes.clear();
	scene->ListNodes(*camera, visible_nodes);

	V(device->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET | D3DCLEAR_STENCIL, scene->GetClearColor(), 1.f, 0));

	if(visible_nodes.empty())
		return;

	V(device->BeginScene());

	V(device->EndScene());
}
