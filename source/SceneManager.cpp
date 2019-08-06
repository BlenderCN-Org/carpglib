#include "EnginePch.h"
#include "EngineCore.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Render.h"
#include "SuperShader.h"
#include "DirectX.h"

SceneManager* app::scene_mgr;

SceneManager::SceneManager() : shader(nullptr), camera(nullptr), active_scene(nullptr), use_fog(true), use_lighting(true)
{
}

SceneManager::~SceneManager()
{
	DeleteElements(scenes);
}

void SceneManager::Init()
{
	shader = new SuperShader;
}

void SceneManager::Draw()
{
	IDirect3DDevice9* device = app::render->GetDevice();

	if(!camera || !active_scene)
	{
		V(device->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET | D3DCLEAR_STENCIL, Color::Black, 1.f, 0));
		return;
	}

	Scene* scene = active_scene;

	visible_nodes.clear();
	scene->ListNodes(*camera, visible_nodes);

	V(device->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET | D3DCLEAR_STENCIL, scene->GetClearColor(), 1.f, 0));

	if(visible_nodes.empty())
		return;

	V(device->BeginScene());

	shader->SetAmbientColor(scene->ambient_color);
	if(!use_fog || !scene->use_fog)
		shader->SetFogDisabled();
	else
		shader->SetFog(scene->fog_color, scene->fog_range);
	if(use_lighting)
	{
		if(scene->use_light_dir)
			shader->SetLightDir(scene->light_dir, scene->light_color);
	}
	else
		shader->SetLightDisabled();

	V(device->EndScene());
}
